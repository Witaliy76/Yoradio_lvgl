#include "options.h"

#include "player.h"

#include "config.h"
#include "mem_watchdog.h"
#include "telnet.h"
#include "display.h"
#include "sdmanager.h"
#include "netserver.h"
#include "../i18n/i18n.h"
#include "../platform/storage/storage_owner.h"
#include "../platform/storage/storage_engineering_log.h"
#include "folder_queue.h"

#include <freertos/portmacro.h>

// Serializes only bounded command copies and confirmed-context publication.
// No Audio/FS call or wait is performed while this lock is held.
// Блокировка только для коротких копий команд и публикации подтверждённого состояния.
namespace {
class PlayerRequestGuard {
  portMUX_TYPE& mux_;
public:
  explicit PlayerRequestGuard(portMUX_TYPE& mux) : mux_(mux) { portENTER_CRITICAL(&mux_); }
  ~PlayerRequestGuard() { portEXIT_CRITICAL(&mux_); }
};
}

Player player;
QueueHandle_t playerQueue;

// PR_VOL overflow merge — non-blocking send leaves latest volume here for next loop() — avoids deadlock / merge при полной очереди
static portMUX_TYPE s_vol_merge_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_vol_merge_pending = false;
static volatile int s_vol_merge_payload = 0;

#if !I2S_INTERNAL
    Player::Player() {}
  #else
    Player::Player(): Audio(true, I2S_DAC_CHANNEL_BOTH_EN)  {}
  #endif


void Player::init() {
  Serial.print("##[BOOT]#\tplayer.init\t");
  playerQueue=NULL;
  _resumeFilePos = 0;
  playerQueue = xQueueCreate(PLAYER_QUEUE_LENGTH, sizeof(playerRequestParams_t));
  _pathQueue = xQueueCreate(2, sizeof(yoradio::playback::PathPlayRequest));
  /* pinMode BEFORE the first write: digitalWrite on a pin whose output driver is still off
   * does not reach the amplifier, which left the boot-time mute assert ineffective.
   * pinMode ДО первой записи: digitalWrite по пину с выключенным драйвером не доходит
   * до усилителя, из-за чего стартовый mute не срабатывал. */
  if(MUTE_PIN!=255) pinMode(MUTE_PIN, OUTPUT);
  setOutputPins(false);
  delay(50);
  memset(_plError, 0, PLERR_LN);
#ifdef MQTT_ROOT_TOPIC
  memset(burl, 0, MQTT_BURL_SIZE);
#endif
  #if I2S_DOUT!=255
    #if !I2S_INTERNAL
      setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    #endif
  #endif
  setBalance(config.store.balance);
  setTone(config.store.bass, config.store.middle, config.store.trebble);
  setVolume(0);
  _status = STOPPED;
  //setOutputPins(false);
  _volTimer=false;
  //randomSeed(analogRead(0));
  #if PLAYER_FORCE_MONO
    forceMono(true);
  #endif
  /* NVS written before "0 == MUTE" existed can hold volume 0. Adopt it as MUTE and restore a
   * meaningful remembered level (runtime only - nothing is written back here), so the
   * forbidden "0 but not muted" state cannot survive a reboot.
   * NVS, записанный до правила "0 == MUTE", может содержать 0. Трактуем как MUTE и
   * восстанавливаем осмысленный запомненный уровень (только в RAM, без записи в NVS). */
  if (config.store.volume == 0) {
    _muted = true;
    config.store.volume = PLAYER_VOLUME_FACTORY_DEFAULT;
    Serial.printf("[MUTE] semantic ON (legacy volume=0 adopted at boot)\n");
  }
  _applyVolumeToAudio();
  setConnectionTimeout(1700, 3700);
  // Set Audio Task core from platformio.ini define or default to 1
  // Установка ядра Audio Task из define platformio.ini или по умолчанию 1
  #ifdef AUDIOTASK_CORE
    setAudioTaskCore(AUDIOTASK_CORE);
  #endif
  Serial.println("done");
}

bool Player::sendCommand(playerRequestParams_t request) {
  if(!playerQueue) return false;
  if(request.type == PR_VOL) {
    if(xQueueSend(playerQueue,&request,0)==pdTRUE) return true;
    portENTER_CRITICAL(&s_vol_merge_mux);
    s_vol_merge_payload=request.payload;s_vol_merge_pending=true;
    portEXIT_CRITICAL(&s_vol_merge_mux);
    return true;
  }
  PlayerRequestGuard guard(_controlMux);
  if(request.type == PR_STOP) {
    // Epoch bump invalidates an in-flight next search; owner cancels queue state.
    // Bump epoch инвалидирует поиск next; owner снимет состояние очереди.
    const auto epoch=_pathEpoch.load();
    _pathEpoch.store(epoch && epoch!=UINT32_MAX ? epoch+1 : 0);
    _stopPending=true;
    return true;
  }
  const bool selection=request.type==PR_PLAY || request.type==PR_MODE || request.type==PR_TOGGLE;
  const bool relative=request.type==PR_NEXT || request.type==PR_PREV || request.type==PR_SEEK;
  if((selection || relative) && hasStopFailure() && request.type!=PR_TOGGLE) return false;
  if(selection) {
    const auto epoch=_pathEpoch.load();
    if(!epoch || epoch==UINT32_MAX) return false;
    request.epoch=epoch+1;
  } else if(relative) {
    request.epoch=_pathEpoch.load();request.sourceEpoch=_context.epoch;
    if(!request.epoch) return false;
  }
  if(xQueueSend(playerQueue,&request,0)!=pdTRUE) return false;
  if(selection) _pathEpoch.store(request.epoch);
  return true;
}

void Player::resetQueue() {
  {
    PlayerRequestGuard guard(_controlMux);
    const auto epoch=_pathEpoch.load();
    _pathEpoch.store(epoch && epoch!=UINT32_MAX ? epoch+1 : 0);
    if(playerQueue) xQueueReset(playerQueue);
    if(_pathQueue) xQueueReset(_pathQueue);
    // A previously accepted STOP remains pending even when other work is reset.
  }
  portENTER_CRITICAL(&s_vol_merge_mux);
  s_vol_merge_pending=false;
  portEXIT_CRITICAL(&s_vol_merge_mux);
}

void Player::_invalidatePathEpoch(uint32_t expected) {
  PlayerRequestGuard guard(_controlMux);
  const auto epoch=_pathEpoch.load();
  if(expected && expected!=epoch) return;
  _pathEpoch.store(epoch && epoch!=UINT32_MAX ? epoch+1 : 0);
}

yoradio::playback::PathPlayResult Player::requestPlay(const yoradio::playback::PathPlayRequest& ref) {
  using namespace yoradio::storage;
  using namespace yoradio::playback;
  const auto result=validatePathPlayback(ref.media.path);
  if(result!=PathPlayResult::None) return result;
  if(hasStopFailure()) return PathPlayResult::StopFailed;
  const auto status=StorageOwner::instance().status();
  if(!status.ready) return status.mounted?PathPlayResult::StaleMedia:PathPlayResult::NoMedia;
  if(status.generation!=ref.media.generation) return PathPlayResult::StaleMedia;
  // Rejected admission must not cancel an active search. Only Accepted bumps epoch.
  // Отклонённый запрос не отменяет поиск. Epoch поднимает только Accepted.
  PlayerRequestGuard guard(_controlMux);
  if(!ref.epoch || ref.epoch!=_pathEpoch.load() || ref.epoch==UINT32_MAX) return PathPlayResult::StaleEpoch;
  if(!_pathQueue) return PathPlayResult::NotReady;
  auto owned=ref;owned.epoch=ref.epoch+1;
  if(xQueueSend(_pathQueue,&owned,0)!=pdTRUE) return PathPlayResult::Busy;
  _pathEpoch.store(owned.epoch);
  return PathPlayResult::Accepted;
}

bool Player::hasPathWork() const {
  return _singleFile.load() || (_pathQueue && uxQueueMessagesWaiting(_pathQueue));
}

bool Player::onSingleFileEof(uint32_t epoch) {
  const auto context=playbackContext();
  // File EOF is owner-local. Always consume it; never map a late file event to
  // Radio Next. Future queued events must carry the captured playback epoch.
  // EOF файла не превращается в Radio Next; отложенное событие несёт свой epoch.
  if(context.source==yoradio::playback::PlaybackSource::SingleFile &&
     context.state==yoradio::playback::PlaybackState::Playing &&
     (!epoch || epoch==context.epoch)) _singleEof.store(context.epoch);
  return true;
}

void Player::_playPath(const yoradio::playback::PathPlayRequest& ref, bool adopt_queue) {
  using namespace yoradio::storage;
  using namespace yoradio::playback;
  auto abandonLaunch = [&]() {
    folderQueueCancel();
    folderQueueClearLaunchFlags();
  };
  if(!ref.epoch || ref.epoch!=_pathEpoch.load()) {
    abandonLaunch();_pathResult=PathPlayResult::StaleEpoch;return;
  }
  if(hasStopFailure()) {
    abandonLaunch();_reportStopFailure();_pathResult=PathPlayResult::StopFailed;return;
  }
  auto status=StorageOwner::instance().status();
  if(!status.ready || status.generation!=ref.media.generation) {
    abandonLaunch();
    _pathResult=status.mounted?PathPlayResult::StaleMedia:PathPlayResult::NoMedia;return;
  }
  cancelWebstreamReconnect("file selection");
  if(!_stop(true)) { abandonLaunch();_pathResult=PathPlayResult::StopFailed;return; }
  // After STOP of the previous source: this request's epoch must still own pathEpoch.
  // A concurrent newer STOP/Radio/path wins; do not invalidate that newer intent.
  // После STOP прежнего источника: epoch этого запроса всё ещё должен владеть pathEpoch.
  // Параллельный новый STOP/Radio/path побеждает; его намерение не инвалидируем.
  if(ref.epoch!=_pathEpoch.load() || _stopPending) {
    abandonLaunch();
    _pathResult=PathPlayResult::StaleEpoch;
    return;
  }
  resumeAfterUrl=false;setError("");
  if(!_pathFs.bind(ref.media.generation)) {
    abandonLaunch();
    _pathResult=PathPlayResult::StaleMedia;setError("SD media generation changed");return;
  }
  if(!connecttoFS(_pathFs,ref.media.path,-1,_pathFs.fileStatus())) {
    setError("SD file open failed");
    // Keep history; do not auto-skip. Clear only launch/search residue.
    // Историю сохранить; без auto-skip. Снять только launch/search.
    folderQueueCancel();
    folderQueueClearLaunchFlags();
    _pathResult=_stop(true)?PathPlayResult::OpenFailed:PathPlayResult::StopFailed;
    yoradio::storage::g48("E-after-attempt");
    return;
  }
  status=StorageOwner::instance().status();
  if(!status.ready || status.generation!=ref.media.generation) {
    setError("SD media generation changed");
    folderQueueCancel();
    folderQueueClearLaunchFlags();
    _pathResult=_stop(true)?PathPlayResult::StaleMedia:PathPlayResult::StopFailed;
    yoradio::storage::g48("E-after-attempt");
    return;
  }
  if(!_commitPlayback(PlaybackSource::SingleFile,ref.epoch,&ref.media)) {
    folderQueueCancel();
    folderQueueClearLaunchFlags();
    _pathResult=_stop(true)?PathPlayResult::StaleEpoch:PathPlayResult::StopFailed;
    yoradio::storage::g48("E-after-attempt");
    return;
  }
  if(adopt_queue) {
    if(folderQueueConsumeHistoryFlag()) folderQueueOnHistoryPlay(ref.media);
    else if(folderQueueConsumeAdvanceFlag()) folderQueueOnAdvanced(ref.media);
    else folderQueueOnSelected(ref.media);
  }
  remoteStationName=false;
  config.setStation(strrchr(ref.media.path,'/')+1);
  config.setTitle("");
  _applyVolumeToAudio();_pathResult=PathPlayResult::Playing;
  setOutputPins(true);
  netserver.requestOnChange(GETPLAYERMODE,0);netserver.requestOnChange(MODE,0);
  // SDPATH before STATION: WS may drop later frames under write pressure.
  // SDPATH раньше STATION: WS может отбросить поздние кадры при давлении записи.
  // Basename for chrome; full path also embedded in nameset (STATIONNAME).
  // Имя для шапки; полный путь также встраивается в nameset (STATIONNAME).
  netserver.requestOnChange(SDPATH,0);
  netserver.requestOnChange(STATION,0);
  display.putRequest(NEWSTATION);
  if(player_on_start_play) player_on_start_play();
  yoradio::storage::g48("E-after-attempt");
}

void Player::_pollPathTerminal() {
  using namespace yoradio::playback;
  using namespace yoradio::storage;
  if(hasStopFailure()) { _pathResult=PathPlayResult::StopFailed;return; }
  const auto context=playbackContext();
  if(context.source!=PlaybackSource::SingleFile || context.state!=PlaybackState::Playing) return;
  const auto io=_pathFs.fileStatus();
  const auto status=StorageOwner::instance().status();
  const auto eof=_singleEof.exchange(0);
  PathPlayResult result=PathPlayResult::None;
  const char* error=nullptr;
  if(!status.ready || status.generation!=context.generation) {
    result=status.mounted?PathPlayResult::StaleMedia:PathPlayResult::NoMedia;error="SD media unavailable";
  } else if(io && io->failed()) {
    const auto file_error=io->error.load();
    result=file_error==FileError::ReadFailed?PathPlayResult::ReadFailed:
           file_error==FileError::SeekFailed?PathPlayResult::SeekFailed:PathPlayResult::IoError;
    error=file_error==FileError::ReadFailed?"SD file read failed":
          file_error==FileError::SeekFailed?"SD file seek failed":"SD file IO failed";
  } else if(eof==context.epoch) result=PathPlayResult::EndOfFile;
  else if(!isRunning()) { result=PathPlayResult::IoError;error="SD playback stopped unexpectedly"; }
  if(result==PathPlayResult::None) return;
  MediaRef ended;
  const bool have_ended=currentMedia(ended);
  if(error) setError(error);
  if(result==PathPlayResult::EndOfFile && have_ended) {
    // Natural EOF shares the Next search. Open failure never auto-skips.
    // Естественный EOF использует тот же поиск, что Next. Ошибка open не перебирает файлы.
    // Keep the playing epoch through STOP; do not adopt a newer STOP/Radio/path epoch.
    // Сохраняем epoch воспроизведения через STOP; не подхватываем epoch нового STOP/Radio/path.
    const uint32_t intent_epoch=context.epoch;
    if(!_stop(true)) { _pathResult=PathPlayResult::StopFailed;yoradio::storage::g48("E-after-stop");return; }
    if(!_intentStillCurrent(intent_epoch)) {
      // Newer STOP/Radio/path already owns pathEpoch — do not restart or bump again.
      // Новый STOP/Radio/path уже владеет pathEpoch — не перезапускать и не bump'ать снова.
      return;
    }
    _startFolderNext(ended,true,intent_epoch);
    return;
  }
  folderQueueCancel();
  _invalidatePathEpoch(context.epoch); // cannot cancel a concurrently accepted newer intent
  _pathResult=_stop(true)?result:PathPlayResult::StopFailed;
  yoradio::storage::g48("E-after-stop");
}

bool Player::_admitPathEpoch(uint32_t required_epoch, uint32_t& play_epoch) {
  PlayerRequestGuard guard(_controlMux);
  if(!required_epoch || required_epoch==UINT32_MAX) return false;
  if(required_epoch!=_pathEpoch.load()) return false;
  play_epoch=required_epoch+1;
  if(!play_epoch) return false;
  _pathEpoch.store(play_epoch);
  return true;
}

void Player::_playAdmittedPath(const yoradio::storage::MediaRef& media, uint32_t play_epoch) {
  using namespace yoradio::playback;
  PathPlayRequest ref;
  const auto built=PathPlayRequest::copy(media.path,media.generation,play_epoch,ref);
  if(built!=PathPlayResult::None) {
    folderQueueCancel();
    folderQueueClearLaunchFlags();
    _pathResult=built;
    return;
  }
  // copy() stores the caller epoch; play uses the already-admitted play_epoch.
  // copy() сохраняет epoch вызывающего; play использует уже допущенный play_epoch.
  ref.epoch=play_epoch;
  _playPath(ref);
}

bool Player::_intentStillCurrent(uint32_t intent_epoch) const {
  // Relative Next/Prev/EOF keep the pre-STOP epoch. A concurrent STOP/Radio/path
  // bumps pathEpoch and/or latches _stopPending — the old transition must stop.
  // Относительные Next/Prev/EOF держат epoch до STOP. Параллельный STOP/Radio/path
  // поднимает pathEpoch и/или _stopPending — старый переход должен оборваться.
  if(!intent_epoch || intent_epoch==UINT32_MAX) return false;
  if(_stopPending) return false;
  return intent_epoch==_pathEpoch.load();
}

void Player::_replayCurrentPath(const yoradio::storage::MediaRef& media, uint32_t intent_epoch) {
  using namespace yoradio::playback;
  uint32_t play_epoch=0;
  if(!_admitPathEpoch(intent_epoch,play_epoch)) { _pathResult=PathPlayResult::StaleEpoch;return; }
  PathPlayRequest ref;
  const auto built=PathPlayRequest::copy(media.path,media.generation,play_epoch,ref);
  if(built!=PathPlayResult::None) { _pathResult=built;return; }
  ref.epoch=play_epoch;
  _playPath(ref,false);
}

bool Player::_startFolderNext(const yoradio::storage::MediaRef& cursor, bool from_eof,
                              uint32_t intent_epoch) {
  using namespace yoradio::playback;
  _folderSearchFromEof=from_eof;
  if(!_intentStillCurrent(intent_epoch)) {
    _pathResult=PathPlayResult::StaleEpoch;
    return false;
  }
  if(from_eof && sdRepeatMode()==SdRepeatMode::One) {
    Serial.printf("[SD-PLAY] Repeat One -> %s\n", cursor.path);
    telnet.printf("##SD.MODE#: Repeat One replay\n");
    _replayCurrentPath(cursor,intent_epoch);
    return true;
  }
  // Bind search to the same intent epoch — never _pathEpoch.load() after STOP.
  // Поиск привязан к тому же intent epoch — не к _pathEpoch.load() после STOP.
  if(!folderQueueStartNext(cursor,intent_epoch,sdRepeatMode(),sdShuffle())) {
    _pathResult=PathPlayResult::NoAdjacentTrack;
    return false;
  }
  _pathResult=PathPlayResult::SearchingNext;
  return true;
}

void Player::_restartFolderSearchIfNeeded() {
  using namespace yoradio::playback;
  const bool searching=_pathResult.load()==PathPlayResult::SearchingNext || folderQueueIsSearching();
  if(!searching) return;
  yoradio::storage::MediaRef cursor;
  if(!folderQueueCurrent(cursor)) { folderQueueCancel();return; }
  const bool from_eof=_folderSearchFromEof;
  // Mode change restarts under the epoch that is current now (not a post-STOP reload of an old op).
  // Смена режима перезапускает под текущим epoch (не подхватывает чужой bump после STOP).
  const uint32_t intent_epoch=_pathEpoch.load();
  folderQueueCancel();
  if(!_intentStillCurrent(intent_epoch)) return;
  _startFolderNext(cursor,from_eof,intent_epoch);
}

void Player::_pollFolderQueue() {
  using namespace yoradio::playback;
  using namespace yoradio::storage;
  if(hasStopFailure()) return;
  MediaRef found;
  PathPlayResult detail=PathPlayResult::None;
  const FolderQueuePoll poll=folderQueuePoll(found,detail);
  if(poll==FolderQueuePoll::Idle || poll==FolderQueuePoll::Cancelled) return;
  if(poll==FolderQueuePoll::Pending) { _pathResult=PathPlayResult::SearchingNext;return; }
  if(poll==FolderQueuePoll::End) { _pathResult=PathPlayResult::EndOfFile;return; }
  if(poll==FolderQueuePoll::Error) {
    _pathResult=detail!=PathPlayResult::None?detail:PathPlayResult::IoError;
    if(_pathResult==PathPlayResult::NoMedia || _pathResult==PathPlayResult::StaleMedia ||
       _pathResult==PathPlayResult::ReadFailed) {
      if(!hasError()) setError("SD folder queue failed");
    }
    if(_pathResult==PathPlayResult::ShuffleLimit && !hasError())
      setError("SD shuffle limit");
    if(_pathResult==PathPlayResult::NoMemory && !hasError())
      setError("SD folder: no memory");
    return;
  }
  if(poll!=FolderQueuePoll::Found) return;
  const uint32_t search_epoch=folderQueueSearchEpoch();
  MediaRef launch;
  if(!search_epoch || !folderQueueClaimFound(search_epoch,launch)) return;
  // Bound to the search's original epoch. Never re-admit under a post-STOP bump.
  // Привязка к исходному epoch поиска. Не пере-admit под bump после STOP.
  if(!_intentStillCurrent(search_epoch)) {
    folderQueueCancel();
    // Leave pathEpoch / queued Radio|path alone — only drop the stale search.
    // pathEpoch и очередь Radio|path не трогаем — снимаем только устаревший поиск.
    return;
  }
  uint32_t play_epoch=0;
  if(!_admitPathEpoch(search_epoch,play_epoch)) {
    // STOP/Radio/new path already invalidated this search epoch.
    // STOP/Radio/новый путь уже инвалидировали epoch этого поиска.
    folderQueueCancel();
    return;
  }
  _playAdmittedPath(launch,play_epoch);
}

void Player::stopInfo() {
//  config.setSmartStart(0);
  if (config.store.smartstart < 2) config.setSmartStart(0);			// ************************


  //telnet.info();
  netserver.requestOnChange(MODE, 0);
}

// E36AUD0A: store transport error only — LVGL reads lastError(); no title/NEWTITLE/AI / ошибка отдельно от metadata
void Player::setError(const char *e, bool emitErrorLog){
  char nextError[PLERR_LN];
  strlcpy(nextError, e ? e : "", sizeof(nextError));
  const bool changed = (strcmp(_plError, nextError) != 0);
  strlcpy(_plError, nextError, PLERR_LN);
  if (hasError() && emitErrorLog) {
    telnet.printf("##ERROR#:\t%s\n", _plError);
  }
#ifdef MEM_WATCHDOG_AUTOREBOOT
  // E36MEM0D1: every non-empty error resets recovery; WS/MQTT still deduped / health vs remote state
  if (hasError()) {
    const MWPlaybackFault fault =
        (strcmp(nextError, "responseHeaderline overflow") == 0)
            ? MWPlaybackFault::HEADER_OVERFLOW
            : MWPlaybackFault::GENERIC_ERROR;
    memWatchdog.onPlaybackHealthFault(fault);
  }
#endif
  // E36AUD0B: remote player_error state — queue only on change / WS+MQTT без повторов одного значения
  if (changed) {
    netserver.requestOnChange(PLAYER_ERROR, 0);
  }
}

void Player::_reportStopFailure() {
  using namespace yoradio::playback;
  setOutputPins(false);
  {
    PlayerRequestGuard guard(_controlMux);
    _context.state=PlaybackState::StopFailed;
  }
  const auto proof=_pathFs.fileStatus();
  const bool unknown=proof && !proof->allClosed() && proof->error.load()==yoradio::storage::FileError::CloseFailed;
  const char* error=unknown?"SD close unconfirmed; restart required":"Audio stop failed; press STOP to retry";
  if(!_stopFailureReported) { _invalidatePathEpoch();_stopFailureReported=true; }
  _pathResult=PathPlayResult::StopFailed;
  if(strcmp(lastError(),error)) setError(error);
}

bool Player::_stop(bool alreadyStopped){
  log_i("%s called", __func__);
  (void)alreadyStopped; // no legacy numeric SD resume state
  setOutputPins(false);
  // One checked stop/reset. No metadata, STOPPED or callback before the barrier.
  // Одна проверенная остановка/reset; STOPPED и callback только после barrier.
  if (!setDefaults()) {
    _reportStopFailure();
    S07_STORAGE_LOG("[S07-ENG] stop barrier=FAILED error=%s\n",lastError());
    return false;
  }
  S07_STORAGE_LOG("[S07-ENG] stop barrier=CONFIRMED storage_closed=%u\n",
      static_cast<unsigned>(!_pathFs.fileStatus() || _pathFs.fileStatus()->allClosed()));
  beginPlaybackSession();
  if (_stopFailureReported) {
    _stopFailureReported = false;
    if ((strcmp(lastError(), "Audio stop failed; press STOP to retry") == 0 ||
        strcmp(lastError(), "SD close unconfirmed; restart required") == 0)) setError("");
  }
  _confirmStopped();
  if(_singleFile.load()) _pathResult=yoradio::playback::PathPlayResult::Stopped;
  if(!hasError()) config.setTitle((display.mode()==LOST || display.mode()==UPDATING)
                                      ? ""
                                      : i18n::text(i18n::TextId::PlayerStopped));
  config.station.bitrate = 0;
  config.station.stream_sample_rate_hz   = 0;
  config.station.stream_bits_per_sample = 0;
  config.setBitrateFormat(BF_UNCNOWN);
  netserver.requestOnChange(GETPLAYERMODE, 0);
  netserver.requestOnChange(BITRATE, 0);
  display.putRequest(DBITRATE);

  if(!lockOutput) stopInfo();
  if (player_on_stop_play) player_on_stop_play();
  return true;
}

void Player::initHeaders(const char *file) {
  if(strlen(file)==0 || true) return; //TODO Read TAGs
  Serial.printf("🎵 [SD HEADERS] Reading headers from: %s\n", file);
  if(!connecttoFS(sdman,file)) { if(hasStopFailure()) _reportStopFailure(); return; }
  eofHeader = false;
  while(!eofHeader && !hasStopFailure()) Audio::loop();
  Serial.printf("🎵 [SD HEADERS] Headers read completed\n");
  //netserver.requestOnChange(SDPOS, 0);
  if(!setDefaults()) _reportStopFailure();
}

#ifndef PL_QUEUE_TICKS
  #define PL_QUEUE_TICKS 0
#endif
#ifndef PL_QUEUE_TICKS_ST
  #define PL_QUEUE_TICKS_ST 15
#endif

#ifdef MEM_WATCHDOG_AUTOREBOOT
// E36MEM0D: 15 s stable PLAYING+isRunning, reset on failure / стабильное воспроизведение 15 с
static void mwPollPlaybackRecovery(plStatus_e status, bool audioRunning) {
  static uint32_t healthySince = 0;
  static bool recoverNotified = false;

  if (memWatchdog.takeRecoveryTimerReset() || memWatchdog.rebootArmed()) {
    healthySince = 0;
    recoverNotified = false;
    if (memWatchdog.rebootArmed()) return;
  }

  const bool healthy = (status == PLAYING && audioRunning);
  if (healthy) {
    const uint32_t now = millis();
    if (healthySince == 0) healthySince = now;
    if (!recoverNotified &&
        (now - healthySince) >= MW_PLAYBACK_RECOVER_MS &&
        memWatchdog.canAcceptPlaybackRecovery()) {
      recoverNotified = true;
      memWatchdog.onPlaybackRecovered();
    }
  } else {
    healthySince = 0;
    recoverNotified = false;
  }
}
#endif

Player::ActivateAdmission Player::activate(yoradio::source::SourceId id) {
  using namespace yoradio::playback;
  using namespace yoradio::source;
  ActivateAdmission admission;
  if(!playerQueue) { admission.outcome=ActivateOutcome::Cancelled; return admission; }
  ActivateFacts facts;
  {
    PlayerRequestGuard guard(_controlMux);
    facts.stop_failed=hasStopFailure();
    facts.radio_playing=_context.state==PlaybackState::Playing &&
        _context.source==PlaybackSource::Radio;
    facts.sd_playing=_context.state==PlaybackState::Playing &&
        _context.source==PlaybackSource::SingleFile;
    facts.radio_has_stations=config.store.countStation>0;
    facts.bookmark_valid=_sdBookmarkValid;
    facts.bookmark_generation=_sdBookmark.generation;
  }
  const auto status=yoradio::storage::StorageOwner::instance().status();
  facts.storage_ready=status.ready;
  facts.storage_mounted=status.mounted;
  facts.storage_generation=status.generation;
  const auto decision=classify(id,facts);
  PlayerRequestGuard guard(_controlMux);
  // One activation owns the player until it finishes. A second call does not
  // replace it and does not move the playback epoch or the folder queue.
  // Одна активация владеет плеером до завершения. Второй вызов её не заменяет
  // и не двигает epoch воспроизведения или folder queue.
  if(_act.pending) { admission.outcome=ActivateOutcome::Busy; return admission; }
  if(_actSeq==UINT32_MAX) { admission.outcome=ActivateOutcome::Cancelled; return admission; }
  admission.request=++_actSeq;
  _act.id=admission.request;
  _act.source=id;
  _act.seen_epoch=_pathEpoch.load();
  _act.epoch=0;
  _act.pending=decision.step!=ActivateStep::Done;
  _act.complete=decision.step==ActivateStep::Done;
  _act.outcome=_act.pending?ActivateOutcome::Pending:decision.outcome;
  admission.outcome=_act.outcome;
  return admission;
}


yoradio::source::ActivateSnapshot Player::activation() const {
  PlayerRequestGuard guard(_controlMux);
  yoradio::source::ActivateSnapshot out;
  out.request=_act.id;
  out.source=_act.source;
  out.outcome=_act.outcome;
  out.epoch=_act.epoch;
  out.complete=_act.complete;
  return out;
}


bool Player::selectedSource(yoradio::source::SourceId& out) const {
  PlayerRequestGuard guard(_controlMux);
  if(!_selectedValid) return false;
  out=_selected;
  return true;
}


void Player::_serviceSourceActivation() {
  using namespace yoradio::playback;
  using namespace yoradio::source;
  uint32_t request=0, seen=0;
  SourceId id=SourceId::Radio;
  bool bookmark_valid=false;
  yoradio::storage::MediaRef bookmark;
  {
    PlayerRequestGuard guard(_controlMux);
    if(!_act.pending) return;
    request=_act.id; seen=_act.seen_epoch; id=_act.source;
    bookmark_valid=_sdBookmarkValid;
    if(bookmark_valid) bookmark=_sdBookmark;
    // A newer STOP or selection already owns the epoch. Do not probe or stop.
    // Более новый STOP или выбор уже владеет epoch. Не проверять файл и не останавливать.
    if(_stopPending || _pathEpoch.load()!=seen) {
      _act.pending=false; _act.complete=true; _act.outcome=ActivateOutcome::Superseded;
      return;
    }
  }
  auto finish=[&](ActivateOutcome outcome, uint32_t epoch) {
    PlayerRequestGuard guard(_controlMux);
    if(_act.id!=request || !_act.pending) return;
    _act.pending=false; _act.complete=true; _act.outcome=outcome; _act.epoch=epoch;
    if(outcome==ActivateOutcome::NeedsSelection) _pathResult=PathPlayResult::PathRequired;
    else if(outcome==ActivateOutcome::Unavailable) _pathResult=PathPlayResult::NoMedia;
    else if(outcome==ActivateOutcome::StaleMedia) _pathResult=PathPlayResult::StaleMedia;
    else if(outcome==ActivateOutcome::OpenFailed) _pathResult=PathPlayResult::OpenFailed;
    else if(outcome==ActivateOutcome::StopFailed) _pathResult=PathPlayResult::StopFailed;
    else if(outcome==ActivateOutcome::SelectedStopped) _pathResult=PathPlayResult::Stopped;
    else if(outcome==ActivateOutcome::Playing) _pathResult=PathPlayResult::Playing;
  };
  if(hasStopFailure()) { finish(ActivateOutcome::StopFailed,0); return; }
  const auto context=playbackContext();
  const bool same=context.state==PlaybackState::Playing &&
      ((id==SourceId::Radio && context.source==PlaybackSource::Radio) ||
       (id==SourceId::Sd && context.source==PlaybackSource::SingleFile));
  if(same) { finish(ActivateOutcome::AlreadyActive, context.epoch); return; }
  if(id==SourceId::Radio) {
    if(!config.store.countStation) { finish(ActivateOutcome::NeedsSelection,0); return; }
  } else {
    if(!bookmark_valid) { finish(ActivateOutcome::NeedsSelection,0); return; }
    const auto status=yoradio::storage::StorageOwner::instance().status();
    if(!status.mounted && !status.ready) { finish(ActivateOutcome::Unavailable,0); return; }
    if(!status.ready || status.generation!=bookmark.generation) {
      finish(ActivateOutcome::StaleMedia,0); return;
    }
    const auto probe=yoradio::storage::probeMediaFile(status.generation, bookmark.path);
    FileCheck check=FileCheck::Io;
    if(probe==yoradio::storage::MediaProbe::Present) check=FileCheck::Present;
    else if(probe==yoradio::storage::MediaProbe::Missing) check=FileCheck::Missing;
    else if(probe==yoradio::storage::MediaProbe::Unavailable) check=FileCheck::Unavailable;
    else if(probe==yoradio::storage::MediaProbe::Stale) check=FileCheck::Stale;
    const auto probed=afterProbe(check);
    if(probed.step!=ActivateStep::StopThenFinish) { finish(probed.outcome,0); return; }
  }
  uint32_t epoch=0;
  {
    PlayerRequestGuard guard(_controlMux);
    if(_act.id!=request || !_act.pending || _stopPending || _pathEpoch.load()!=seen) {
      if(_act.id==request && _act.pending) {
        _act.pending=false; _act.complete=true; _act.outcome=ActivateOutcome::Superseded;
      }
      return;
    }
    const auto current=_pathEpoch.load();
    if(!current || current==UINT32_MAX) {
      _act.pending=false; _act.complete=true; _act.outcome=ActivateOutcome::Cancelled;
      return;
    }
    epoch=current+1;
    _pathEpoch.store(epoch);
    _act.epoch=epoch;
  }
  cancelWebstreamReconnect("source activate");
  folderQueueCancel();
  if(!_stop()) { finish(ActivateOutcome::StopFailed, epoch); return; }
  {
    PlayerRequestGuard guard(_controlMux);
    if(_act.id!=request || !_act.pending || _stopPending || _pathEpoch.load()!=epoch) {
      if(_act.id==request && _act.pending) {
        _act.pending=false; _act.complete=true; _act.outcome=ActivateOutcome::Superseded;
      }
      return;
    }
    // The switch is committed. A later open failure does not restore the old source.
    // Переключение зафиксировано. Поздний отказ открытия не возвращает старый источник.
    _selectedValid=true;
    _selected=id;
    // Stopped Next/Prev follow the source just selected. Playing paths keep
    // the value published by the confirmed start.
    // Остановленные Next/Prev идут в только что выбранный источник.
    // Во время Playing значение задаёт подтверждённый старт.
    _singleFile.store(id==SourceId::Sd);
  }
  if(!smartStartStartsSource(static_cast<uint8_t>(config.store.smartstart))) {
    finish(ActivateOutcome::SelectedStopped, epoch);
    return;
  }
  if(id==SourceId::Radio) {
    folderQueueReset();
    _play(config.lastStation(), epoch);
    const auto started=playbackContext();
    if(hasStopFailure()) finish(ActivateOutcome::StopFailed, epoch);
    else if(started.state==PlaybackState::Playing &&
            started.source==PlaybackSource::Radio && started.epoch==epoch)
      finish(ActivateOutcome::Playing, epoch);
    else if(_pathEpoch.load()!=epoch) finish(ActivateOutcome::Superseded, epoch);
    else finish(ActivateOutcome::OpenFailed, epoch);
    return;
  }
  PathPlayRequest ref;
  ref.media=bookmark;
  ref.epoch=epoch;
  _playPath(ref, true);
  const auto result=_pathResult.load();
  if(result==PathPlayResult::Playing) finish(ActivateOutcome::Playing, epoch);
  else if(result==PathPlayResult::StopFailed || hasStopFailure()) finish(ActivateOutcome::StopFailed, epoch);
  else if(result==PathPlayResult::StaleEpoch || _pathEpoch.load()!=epoch) finish(ActivateOutcome::Superseded, epoch);
  else if(result==PathPlayResult::StaleMedia) finish(ActivateOutcome::StaleMedia, epoch);
  else finish(ActivateOutcome::OpenFailed, epoch);
}


void Player::loop() {
  if(playerQueue==NULL) return;
  _serviceSourceActivation();
  _pumpRequests();

  // Overflow merge AFTER dequeue — stale PR_VOL in queue must not overwrite latest finger/WebUI value / merge после очереди — «последний побеждает»
  {
    bool merge_now = false;
    int merge_payload = 0;
    portENTER_CRITICAL(&s_vol_merge_mux);
    if(s_vol_merge_pending) {
      merge_now = true;
      merge_payload = s_vol_merge_payload;
      s_vol_merge_pending = false;
    }
    portEXIT_CRITICAL(&s_vol_merge_mux);
    if(merge_now) {
      _applyVolPayload(merge_payload);
    }
  }

  Audio::loop();
  if(hasStopFailure()) _reportStopFailure();
  _pollPathTerminal();
  _pollFolderQueue();
#ifdef MEM_WATCHDOG_AUTOREBOOT
  mwPollPlaybackRecovery(_status, !hasStopFailure() && isRunning() && !isWebstreamReconnectPending());
#endif
  const auto context=playbackContext();
  const auto streamEof=_streamEof.exchange(0);
  if(context.source==yoradio::playback::PlaybackSource::Radio && !hasStopFailure() &&
     (!isRunning() || streamEof==context.epoch) && !isWebstreamReconnectPending() && _status==PLAYING) {
    const bool resume=streamEof==context.epoch && resumeAfterUrl && context.epoch==_pathEpoch.load();
#ifdef MEM_WATCHDOG_AUTOREBOOT
    const AudioTerminalReason terminalReason = consumeTerminalReason();
    if (terminalReason == AudioTerminalReason::HEADER_RETRY_EXHAUSTED ||
        terminalReason == AudioTerminalReason::UNSTABLE_STREAM_EXHAUSTED) {
      memWatchdog.onStationLocalStop(terminalReason);
    } else {
      consumeTerminalReason();
      // E36MEM0D1: automatic stop after stream loss — not manual PR_STOP / авто-стоп в loop
      memWatchdog.onPlaybackAutoStopped(hasError());
    }
#else
    consumeTerminalReason();
#endif
    if(_stop(true) && resume) sendCommand({PR_PLAY,config.lastStation()});
  }
  if(_volTimer){
    if((millis()-_volTicks)>1500){
      config.saveVolume();
      _volTimer=false;
    }
  }
#ifdef MQTT_ROOT_TOPIC
  if(strlen(burl)>0){
    browseUrl();
  }
#endif
#if YORADIO_S07_STORAGE_ENABLED
  // Emit only a changed transition/error snapshot, never periodic FS polling.
  // Вывод только при смене состояния/ошибки; FS дополнительно не опрашивается.
  const auto diag=playbackContext();
  const auto result=pathResult();const auto file_error=pathFileError();
  const auto proof=_pathFs.fileStatus();
  const uint32_t handles=proof?proof->handles.load():0;
  static bool first=true;
  static yoradio::playback::PlaybackContext previous;
  static auto previous_result=yoradio::playback::PathPlayResult::None;
  static auto previous_error=yoradio::storage::FileError::None;
  static uint32_t previous_handles=0;
  if(first || diag.source!=previous.source || diag.state!=previous.state ||
     diag.epoch!=previous.epoch || diag.generation!=previous.generation ||
     diag.radioStation!=previous.radioStation || result!=previous_result ||
     file_error!=previous_error || handles!=previous_handles) {
    Serial.printf("[S07-ENG] playback source=%u state=%u epoch=%u gen=%llu radio=%u result=%u fs_error=%u handles=%u stop_failed=%u\n",
        static_cast<unsigned>(diag.source),static_cast<unsigned>(diag.state),
        static_cast<unsigned>(diag.epoch),static_cast<unsigned long long>(diag.generation),
        static_cast<unsigned>(diag.radioStation),static_cast<unsigned>(result),
        static_cast<unsigned>(file_error),static_cast<unsigned>(handles),
        static_cast<unsigned>(hasStopFailure()));
    first=false;previous=diag;previous_result=result;previous_error=file_error;previous_handles=handles;
  }
#endif
}

/* LED keeps following playback only. The amplifier line follows AUDIBILITY: it is released
 * exclusively while the radio is playing AND not semantically muted, so BTN_MUTE / IR / WebUI
 * silence the amplifier itself instead of only zeroing the codec. Polarity is unchanged:
 * MUTE_VAL is still the asserted (muted) level and MUTE_LOCK still pins the line to !MUTE_VAL
 * for boards that must never assert it - there, semantic MUTE stays codec-only by design.
 * Светодиод по-прежнему следует за воспроизведением. Линия усилителя следует за СЛЫШИМОСТЬЮ:
 * отпускается только при playing И не в MUTE. Полярность не меняется: MUTE_VAL - активный
 * (заглушённый) уровень, MUTE_LOCK по-прежнему фиксирует линию в !MUTE_VAL. */
// Connecting placeholder may live only for the duration of the connect attempt.
// After connecttohost/connecttoFS returns, replace it with station.name IFF metadata did not.
// Плейсхолдер «соединение» живёт только на время попытки connect; после возврата
// connecttohost/connecttoFS меняем на station.name, если метаданные его ещё не сменили.
static void clearTransientTitleAfterConnectAttempt() {
  if (isTransientPlaybackTitle(config.station.title)) {
    config.setTitle(config.station.name);
  }
}

void Player::setOutputPins(bool isPlaying) {
  if(REAL_LEDBUILTIN!=255) digitalWrite(REAL_LEDBUILTIN, LED_INVERT?!isPlaying:isPlaying);
  const bool audible = isPlaying && !_muted && !hasStopFailure();
  bool _ml = MUTE_LOCK?!MUTE_VAL:(audible?!MUTE_VAL:MUTE_VAL);
  if(MUTE_PIN!=255) {
    digitalWrite(MUTE_PIN, _ml);
    // Log only on an actual level change - setOutputPins() is called on every play/stop/mute
    // event, often with the same resulting level, and must not spam the console each time.
    // Логируем только при реальной смене уровня - иначе спам на каждый play/stop/mute.
    const int8_t level = _ml ? 1 : 0;
    const char* word = (_ml == (bool)MUTE_VAL) ? "ASSERT" : "RELEASE";
    if (_ampLastLevel < 0) {
      Serial.printf("[MUTE] amp pin=%d init %s level=%d\n", MUTE_PIN, word, level);
      _ampLastLevel = level;
    } else if (level != _ampLastLevel) {
      Serial.printf("[MUTE] amp pin=%d %s level=%d\n", MUTE_PIN, word, level);
      _ampLastLevel = level;
    }
  }
}

void Player::_play(uint16_t stationId, uint32_t epoch) {
  if(!epoch || epoch!=_pathEpoch.load()) return;
  if(!config.store.countStation) { setError("No Radio stations");return; }
  if(!stationId || stationId>config.store.countStation) stationId=1;
  Serial.printf("🎵 [PLAY] _play() started, stationId=%d\n", stationId);
  log_i("%s called, stationId=%d", __func__, stationId);
  if(hasStopFailure()) { _reportStopFailure(); return; }
  cancelWebstreamReconnect("radio selection");
  if(!_stop(true)) return;
  if(epoch!=_pathEpoch.load()) return;
  resumeAfterUrl=false;
#ifdef MEM_WATCHDOG_AUTOREBOOT
  // E36MEM0D: episode-local heap monitor starts before TLS/connect alloc / до подключения
  memWatchdog.onPlaybackAttemptStarted();
#endif
  setError("");
  remoteStationName = false;
  config.setDspOn(1);
  config.vuThreshold = 0;
  Serial.printf("🎵 [PLAY] Basic setup completed\n");
  config.screensaverTicks=SCREENSAVERSTARTUPDELAY;
  config.screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
  setOutputPins(false);
  config.setTitle(i18n::text(i18n::TextId::PlayerConnecting));
//  config.setTitle(config.getMode()==PM_WEB?i18n::text(i18n::TextId::PlayerConnecting):"[next track]");
  config.station.bitrate = 0;
  config.station.stream_sample_rate_hz   = 0;
  config.station.stream_bits_per_sample = 0;
  config.setBitrateFormat(BF_UNCNOWN);
  Serial.printf("🎵 [PLAY] About to call config.loadStation(%d)\n", stationId);
  if (!config.loadStation(stationId)) {
    setError("Radio catalog unavailable");
    config.setTitle(i18n::text(i18n::TextId::PlayerStopped));
    return;
  }
  Serial.printf("🎵 [PLAY] config.loadStation() completed\n");
  _applyVolumeToAudio();
  Serial.printf("🎵 [PLAY] volume applied\n");
  display.putRequest(DBITRATE);
  display.putRequest(NEWSTATION);
  netserver.requestOnChange(STATION, 0);
  // 8.1HX-B: drop re-entrant netserver.loop() drains — let main loop() drain nsQueue.
  // Reduces WebSocket broadcast burst/re-entrancy during station switch. / Убраны вложенные
  // вызовы netserver.loop(); очередь дренирует основной loop() — меньше всплеск broadcast.
//  config.setSmartStart(0);
  if (config.store.smartstart < 2) config.setSmartStart(0);			//*******************************
  const bool isConnected=connecttohost(config.station.url);
  if(isConnected){
    if(!_commitPlayback(yoradio::playback::PlaybackSource::Radio,epoch,nullptr,stationId)) {
      _stop(true);return;
    }
    netserver.requestOnChange(GETPLAYERMODE,0);
    //config.setTitle("");
//    config.setSmartStart(1);
    if (config.store.smartstart < 2) config.setSmartStart(1);			//*********************************
    netserver.requestOnChange(MODE, 0);
    setOutputPins(true);
    display.putRequest(NEWMODE, PLAYER);
    if (player_on_start_play) player_on_start_play();
    if (player_on_station_change) player_on_station_change();
    clearTransientTitleAfterConnectAttempt();
  }else{
    if(hasStopFailure()) { _reportStopFailure(); return; }
    SET_PLAY_ERROR("Error connecting to %s", config.station.url);
#ifdef MEM_WATCHDOG_AUTOREBOOT
    // E-MW1: Audio already counted this very failure inside connecttohost().
    // Counting it again made one dead station cost two units, so the FAIL_STORM
    // threshold of 6 fired after three button presses. Only count here when the
    // attempt failed before Audio ever got to record it (empty/too long/invalid URL).
    // E-MW1: Audio уже посчитал этот же отказ внутри connecttohost(). Повторный
    // счёт делал одну мёртвую станцию равной двум единицам, и порог FAIL_STORM = 6
    // срабатывал после трёх нажатий. Считаем здесь только если попытка сорвалась
    // до того, как Audio успел её зафиксировать (пустой/длинный/некорректный URL).
    if (!memWatchdog.takeConnectFailureAck()) {
      memWatchdog.record(MWEvent::HTTP_FAIL);
      { auto d = memWatchdog.evaluate(); if (d.trigger) memWatchdog.armReboot(); }
    }
#endif
    _stop(true);
    // _stop skips PlayerStopped when hasError(); still drop a stuck [connecting] title.
    // _stop не ставит «остановлено» при hasError(); убираем зависшее «соединение».
    clearTransientTitleAfterConnectAttempt();
  };
}

#ifdef MQTT_ROOT_TOPIC
void Player::browseUrl(){
  _invalidatePathEpoch();
  const auto epoch=_pathEpoch.load();
  const bool resume=_status==PLAYING && !_singleFile.load();
  if(hasStopFailure()) { _reportStopFailure(); memset(burl, 0, MQTT_BURL_SIZE); return; }
  if(!_stop(true)) { _reportStopFailure(); memset(burl, 0, MQTT_BURL_SIZE); return; }
  setError("");
  beginPlaybackSession();
  remoteStationName = true;
  config.setDspOn(1);
  resumeAfterUrl = resume;
//  setDefaults();
  setOutputPins(false);
  config.setTitle(i18n::text(i18n::TextId::PlayerConnecting));
  if (connecttohost(burl)){
    if(!_commitPlayback(yoradio::playback::PlaybackSource::Radio,epoch,nullptr,config.lastStation())) {
      _stop(true);memset(burl,0,MQTT_BURL_SIZE);return;
    }
    config.setTitle("");
    netserver.requestOnChange(MODE, 0);
    setOutputPins(true);
    if (player_on_start_play) player_on_start_play();
    clearTransientTitleAfterConnectAttempt();
  }else{
    if(hasStopFailure()) { _reportStopFailure(); memset(burl, 0, MQTT_BURL_SIZE); return; }
    SET_PLAY_ERROR("Error connecting to %s", burl);
#ifdef MEM_WATCHDOG_AUTOREBOOT
    // E-MW1: Audio already counted this very failure inside connecttohost().
    // Counting it again made one dead station cost two units, so the FAIL_STORM
    // threshold of 6 fired after three button presses. Only count here when the
    // attempt failed before Audio ever got to record it (empty/too long/invalid URL).
    // E-MW1: Audio уже посчитал этот же отказ внутри connecttohost(). Повторный
    // счёт делал одну мёртвую станцию равной двум единицам, и порог FAIL_STORM = 6
    // срабатывал после трёх нажатий. Считаем здесь только если попытка сорвалась
    // до того, как Audio успел её зафиксировать (пустой/длинный/некорректный URL).
    if (!memWatchdog.takeConnectFailureAck()) {
      memWatchdog.record(MWEvent::HTTP_FAIL);
      { auto d = memWatchdog.evaluate(); if (d.trigger) memWatchdog.armReboot(); }
    }
#endif
    _stop(true);
    clearTransientTitleAfterConnectAttempt();
  }
  memset(burl, 0, MQTT_BURL_SIZE);
}
#endif

void Player::prev() { sendCommand({PR_PREV,0}); }

void Player::next() { sendCommand({PR_NEXT,0}); }

void Player::toggle() { sendCommand({PR_TOGGLE,0}); }

void Player::stepVol(bool up) {
  if (up) {
    if (config.store.volume <= 254 - config.store.volsteps) {
      setVol(config.store.volume + config.store.volsteps);
    }else{
      setVol(254);
    }
  } else {
    if (config.store.volume >= config.store.volsteps) {
      setVol(config.store.volume - config.store.volsteps);
    }else{
      setVol(0);
    }
  }
}

uint8_t Player::volToI2S(uint8_t volume) {
  int vol = map(volume, 0, 254 - config.station.ovol * 3 , 0, 254);
  if (vol > 254) vol = 254;
  if (vol < 0) vol = 0;
  return vol;
}

/* Single codec-level funnel. Every path that makes the radio audible or silent ends here,
 * so MUTE survives station change, stop/play and boot without extra bookkeeping.
 * Единственная точка выдачи уровня в кодек: MUTE переживает смену станции, stop/play и старт. */
void Player::_applyVolumeToAudio() {
  setVolume(_muted ? 0 : volToI2S(config.store.volume));
}

uint8_t Player::audibleVolume() const {
  return _muted ? 0 : config.store.volume;
}

/* Authoritative volume/MUTE convergence point. Every producer - BTN/encoder, touch bar,
 * WebUI, telnet, MQTT - reaches the codec through here, so the "0 == MUTE" contract holds
 * in exactly one place:
 *   requested 0  -> enter MUTE, config.store.volume keeps the last meaningful level;
 *   requested >0 -> leave MUTE and adopt the requested level as the remembered one.
 * config.store.volume is never set to 0: it is also the arithmetic base for relative
 * VOL steps (controls.cpp / stepVol), and zeroing it would strand unmute at silence.
 * Авторитетная точка схождения громкости и MUTE. Все источники приходят в кодек через неё,
 * поэтому правило "0 == MUTE" живёт ровно в одном месте. config.store.volume никогда не 0:
 * это ещё и база для относительных шагов громкости. */
/* Pop-free ordering for a MUTE transition. The caller has already stored the new _muted.
 *   mute   : assert the amplifier FIRST, then step the codec to 0 - the amp is already deaf
 *            when the DAC jumps, so the step is inaudible;
 *   unmute : set the codec level FIRST, then release the amplifier - the DAC step happens
 *            behind a still-asserted amp, and the amp opens onto a steady signal.
 * No transition: only the codec level moves.
 * Порядок без щелчков. Mute: сначала усилитель, затем кодек в 0. Unmute: сначала кодек,
 * затем отпускаем усилитель. Без перехода - только уровень кодека. */
void Player::_applyMuteTransition(bool wasMuted) {
  if (_muted == wasMuted) {
    _applyVolumeToAudio();
    return;
  }
  if (_muted) {
    Serial.printf("[MUTE] semantic ON\n");
    setOutputPins(_status == PLAYING);   /* amplifier asserted */
    _applyVolumeToAudio();               /* codec -> 0 */
  } else {
    Serial.printf("[MUTE] semantic OFF\n");
    _applyVolumeToAudio();               /* codec -> remembered level */
    setOutputPins(_status == PLAYING);   /* amplifier released (only if PLAYING) */
  }
}

void Player::_applyVolPayload(int payload) {
  int v = payload;
  if (v < 0) v = 0;
  if (v > 254) v = 254;

  const bool wasMuted = _muted;
  _muted = (v == 0);
  if (_muted) {
    /* Remembered level intentionally untouched; only the reported/audible value goes to 0. */
    display.putRequest(DRAWVOL);
    netserver.requestOnChange(VOLUME, 0);
  } else {
    config.setVolume(static_cast<uint8_t>(v));   /* stores level + DRAWVOL + WebUI push */
  }
  _applyMuteTransition(wasMuted);
}

/* The one semantic MUTE action. Unmute needs no saved copy of the volume: the remembered
 * level never left config.store.volume in the first place.
 * Единственное семантическое действие MUTE. Отдельная копия громкости не нужна:
 * запомненный уровень и не покидал config.store.volume. */
void Player::_applyMuteRequest(int payload) {
  const bool want = (payload == PMUTE_TOGGLE) ? !_muted : (payload != PMUTE_OFF);
  if (want == _muted) return;
  const bool wasMuted = _muted;
  _muted = want;
  _applyMuteTransition(wasMuted);
  display.putRequest(DRAWVOL);   /* UI reads player.isMuted() in DspTask - no LVGL call from here */
  netserver.requestOnChange(VOLUME, 0);
  telnet.printf("##CLI.MUTE#: %d\n", _muted ? 1 : 0);
}

void Player::setVol(uint8_t volume) {
  _volTicks = millis();
  _volTimer = true;
  player.sendCommand({PR_VOL, volume});
}


yoradio::playback::PlaybackContext Player::playbackContext() const {
  PlayerRequestGuard guard(_controlMux);
  return _context;
}


bool Player::currentMedia(yoradio::storage::MediaRef& out) const {
  PlayerRequestGuard guard(_controlMux);
  if(_context.source!=yoradio::playback::PlaybackSource::SingleFile ||
     _context.state!=yoradio::playback::PlaybackState::Playing) return false;
  out=_currentMedia;
  return true;
}


bool Player::_commitPlayback(yoradio::playback::PlaybackSource source,uint32_t epoch,
                             const yoradio::storage::MediaRef* media,uint16_t station) {
  using namespace yoradio::playback;
  PlayerRequestGuard guard(_controlMux);
  if(!epoch || epoch!=_pathEpoch.load() || hasStopFailure()) return false;
  _context.source=source;_context.state=PlaybackState::Playing;_context.epoch=epoch;
  _context.generation=media?media->generation:0;
  if(source==PlaybackSource::Radio) _context.radioStation=station;
  _currentMedia=media?*media:yoradio::storage::MediaRef{};
  _singleFile.store(source==PlaybackSource::SingleFile);
  // Selection follows a confirmed start. The bookmark records only a file that started.
  // Выбор следует за подтверждённым стартом. Закладка пишется только для запущенного файла.
  _selectedValid=true;
  _selected=source==PlaybackSource::Radio ? yoradio::source::SourceId::Radio
                                         : yoradio::source::SourceId::Sd;
  if(source==PlaybackSource::SingleFile && media && media->path[0]) {
    _sdBookmark=*media;
    _sdBookmarkValid=true;
  }
  _singleEof.store(0);_streamEof.store(0);
  _status=PLAYING;
  return true;
}


void Player::_confirmStopped() {
  PlayerRequestGuard guard(_controlMux);
  _context.state=yoradio::playback::PlaybackState::Stopped;
  _context.epoch=_pathEpoch.load();_context.generation=0;
  _currentMedia={};_singleEof.store(0);_streamEof.store(0);
  _status=STOPPED;
}


void Player::onStreamEof(uint32_t epoch) {
  const auto context=playbackContext();
  if(context.source==yoradio::playback::PlaybackSource::Radio &&
     context.state==yoradio::playback::PlaybackState::Playing &&
     (!epoch || epoch==context.epoch)) _streamEof.store(context.epoch);
}


void Player::_pumpRequests() {
  using namespace yoradio::playback;
  playerRequestParams_t request{};
  bool stop=false,control=false,path=false;
  PathPlayRequest ref;
  {
    PlayerRequestGuard guard(_controlMux);
    stop=_stopPending;_stopPending=false;
    if(!stop) {
      control=playerQueue && xQueueReceive(playerQueue,&request,0)==pdTRUE;
      if(!control) path=_pathQueue && xQueueReceive(_pathQueue,&ref,0)==pdTRUE;
    }
  }
  if(stop) {
    cancelWebstreamReconnect("manual stop");
    folderQueueCancel();
    _stop();
    yoradio::storage::g48("E-after-stop");
    return;
  }
  if(path) { _playPath(ref);return; }
  if(!control || (request.epoch && request.epoch!=_pathEpoch.load())) return;
  if(request.sourceEpoch && request.sourceEpoch!=playbackContext().epoch) return;
  switch(request.type) {
    case PR_PLAY:
      folderQueueReset();
      _play(static_cast<uint16_t>(abs(request.payload)),request.epoch);
      yoradio::storage::g48("E-after-radio");
      break;
    case PR_MODE: {
      const int mode=request.payload<0?(config.getMode()==PM_WEB?PM_SDCARD:PM_WEB):request.payload;
      if(mode==PM_WEB) {
        folderQueueReset();
        _play(config.lastStation(),request.epoch);
      } else { _pathResult=PathPlayResult::PathRequired;setError("SD playback requires a file path"); }
      break;
    }
    case PR_TOGGLE:
      if(hasStopFailure() || _status==PLAYING) {
        folderQueueCancel();
        _stop();
        break;
      }
      {
        // Armed selection starts that source. The old _singleFile branch remains only when nothing was chosen.
        // Выбранный источник запускается здесь. Прежняя ветка _singleFile остаётся, пока выбора нет.
        bool armed=false;
        yoradio::source::SourceId selected=yoradio::source::SourceId::Radio;
        bool bookmark=false;
        yoradio::storage::MediaRef mark{};
        {
          PlayerRequestGuard guard(_controlMux);
          armed=_selectedValid; selected=_selected; bookmark=_sdBookmarkValid;
          if(bookmark) mark=_sdBookmark;
        }
        if(armed) {
          const auto storage=yoradio::storage::StorageOwner::instance().status();
          const auto action=yoradio::source::toggleWhenStopped(
              true, selected, bookmark, storage.ready, storage.mounted,
              storage.generation, mark.generation);
          if(action==yoradio::source::ToggleWhenStopped::StartRadio) {
            folderQueueReset();
            _play(config.lastStation(),request.epoch);
          } else if(action==yoradio::source::ToggleWhenStopped::StartSd) {
            PathPlayRequest armed_ref;
            armed_ref.media=mark;
            armed_ref.epoch=request.epoch;
            _playPath(armed_ref,true);
          } else if(action==yoradio::source::ToggleWhenStopped::Unavailable) {
            _pathResult=PathPlayResult::NoMedia;
          } else if(action==yoradio::source::ToggleWhenStopped::StaleBookmark) {
            _pathResult=PathPlayResult::StaleMedia;
          } else _pathResult=PathPlayResult::PathRequired;
          break;
        }
      }
      if(_singleFile.load()) {
        folderQueueCancel();
        _stop();
      } else _play(config.lastStation(),request.epoch);
      break;
    case PR_NEXT:
    case PR_PREV: {
      if(_singleFile.load()) {
        // Relative commands carry pathEpoch at submit; keep it through STOP.
        // Относительные команды несут pathEpoch на submit; держим его через STOP.
        const uint32_t intent_epoch=request.epoch?request.epoch:_pathEpoch.load();
        if(request.type==PR_PREV) {
          yoradio::storage::MediaRef previous;
          // Peek only — pop after a confirmed start in OnHistoryPlay.
          // Только peek — pop после подтверждённого старта в OnHistoryPlay.
          if(!folderQueuePeekHistory(previous)) {
            _pathResult=PathPlayResult::NoAdjacentTrack;break;
          }
          folderQueueCancel();
          if(_status==PLAYING && !_stop(true)) {
            _pathResult=PathPlayResult::StopFailed;break;
          }
          if(!_intentStillCurrent(intent_epoch)) {
            folderQueueClearLaunchFlags();
            // Do not bump — a newer STOP/Radio/path already owns the epoch.
            // Не bump'ать — новый STOP/Radio/path уже владеет epoch.
            break;
          }
          folderQueueMarkHistoryLaunch();
          uint32_t play_epoch=0;
          if(!_admitPathEpoch(intent_epoch,play_epoch)) {
            folderQueueClearLaunchFlags();
            _pathResult=PathPlayResult::StaleEpoch;break;
          }
          _playAdmittedPath(previous,play_epoch);
          break;
        }
        yoradio::storage::MediaRef cursor;
        if(!currentMedia(cursor) && !folderQueueCurrent(cursor)) {
          _pathResult=PathPlayResult::NoAdjacentTrack;break;
        }
        if(_status==PLAYING && !_stop(true)) {
          _pathResult=PathPlayResult::StopFailed;break;
        }
        if(!_intentStillCurrent(intent_epoch)) break;
        _startFolderNext(cursor,false,intent_epoch);
        break;
      }
      uint16_t station=config.lastStation();
      if(config.store.countStation) {
        if(request.type==PR_NEXT) station=station>=config.store.countStation?1:station+1;
        else station=station<=1?config.store.countStation:station-1;
        _play(station,request.epoch);
      }
      break;
    }
    case PR_SEEK: {
      const auto context=playbackContext();
      if(context.source==PlaybackSource::SingleFile && context.state==PlaybackState::Playing &&
         !hasStopFailure() && request.payload>=0 && !setFilePos(static_cast<uint32_t>(request.payload))) {
        setError("SD file seek failed");
        _pathResult=_stop(true)?PathPlayResult::SeekFailed:PathPlayResult::StopFailed;
      }
      break;
    }
    case PR_VOL: _applyVolPayload(request.payload);break;
    case PR_MUTE: _applyMuteRequest(request.payload);break;
    case PR_VUTONUS: if(config.vuThreshold>10) config.vuThreshold-=10;break;
    case PR_SD_REPEAT: {
      const uint8_t next=static_cast<uint8_t>((_sdRepeat.load()+1u)%4u);
      _sdRepeat.store(next);
      {
        const char* name="Repeat Off";
        switch(static_cast<SdRepeatMode>(next)) {
          case SdRepeatMode::One: name="Repeat One"; break;
          case SdRepeatMode::Folder: name="Repeat Folder"; break;
          case SdRepeatMode::All: name="Repeat All"; break;
          default: break;
        }
        Serial.printf("[SD-PLAY] mode -> %s\n", name);
        telnet.printf("##SD.MODE#: %s\n", name);
      }
      netserver.requestOnChange(SDREPEAT,0);
      _restartFolderSearchIfNeeded();
      break;
    }
    case PR_SD_SHUFFLE: {
      const bool want=request.payload<0?!_sdShuffle.load():request.payload!=0;
      const bool changed=want!=_sdShuffle.load();
      _sdShuffle.store(want);
      if(changed) {
        folderQueueInvalidateShuffle();
        Serial.printf("[SD-PLAY] mode -> Folder Shuffle %s\n", want?"On":"Off");
        telnet.printf("##SD.MODE#: Folder Shuffle %s\n", want?"On":"Off");
      }
      netserver.requestOnChange(SDSHUFFLE,0);
      if(changed) _restartFolderSearchIfNeeded();
      break;
    }
    default: break; // PR_CHECKSD deliberately does not poll/mount a legacy card.
  }
}
