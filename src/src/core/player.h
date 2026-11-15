#ifndef player_h
#define player_h
#include "options.h"
#include "path_play_request.h"
#include "source_catalog.h"
#include "../platform/storage/storage_fs_bridge.h"

#if I2S_DOUT!=255 || I2S_INTERNAL
  #include "../audioI2S/AudioEx.h"
#else
  /* S00-2: бэкенд VS1053 снят. Нужен I2S. / S00-2: VS1053 backend removed. I2S is required. */
  #error "VS1053 backend was removed. Set I2S_DOUT or I2S_INTERNAL."
#endif

#ifndef MQTT_BURL_SIZE
  #define MQTT_BURL_SIZE  512
#endif

#ifndef PLQ_SEND_DELAY
	#define PLQ_SEND_DELAY portMAX_DELAY
#endif

/* Player command queue depth (was 5): burst PR_VOL + WebUI/API could fill queue and block sender forever. /
 * Глубина очереди команд плеера: лавина VOL раньше забивала очередь → зависание на portMAX_DELAY. */
#ifndef PLAYER_QUEUE_LENGTH
#define PLAYER_QUEUE_LENGTH 24
#endif

/* Level adopted as the remembered volume when NVS holds a legacy 0 (see Player::init).
 * Mirrors the factory default in Config::setDefaults(). */
#define PLAYER_VOLUME_FACTORY_DEFAULT  12

#define PLERR_LN        64
#define SET_PLAY_ERROR(...) {char buff[512 + 64]; sprintf(buff,__VA_ARGS__); setError(buff);}

enum playerRequestType_e : uint8_t {
  PR_PLAY = 1, PR_STOP = 2, PR_PREV = 3, PR_NEXT = 4, PR_VOL = 5, PR_CHECKSD = 6,
  PR_VUTONUS = 7, PR_MUTE = 8, PR_MODE = 9, PR_TOGGLE = 10, PR_SEEK = 11,
  PR_SD_REPEAT = 12, PR_SD_SHUFFLE = 13
};

/* PR_MUTE payload: the one semantic MUTE action, callable from any input source
 * (physical BTN_MUTE, touch/UI, future IR, WebUI/telnet) without knowing audio internals.
 * PR_MUTE payload: единственное семантическое действие MUTE для любого источника ввода. */
enum playerMuteRequest_e : int { PMUTE_OFF = 0, PMUTE_ON = 1, PMUTE_TOGGLE = -1 };
struct playerRequestParams_t
{
  playerRequestType_e type;
  int payload;
  uint32_t epoch = 0; // stamped by sendCommand; callers never supply ownership
  uint32_t sourceEpoch = 0; // relative commands belong to the confirmed session
};

enum plStatus_e : uint8_t{ PLAYING = 1, STOPPED = 2 };

class Player: public Audio {
  private:
    uint32_t    _volTicks;   /* delayed volume save  */
    bool        _volTimer;   /* delayed volume save  */
    int32_t    _resumeFilePos;
    plStatus_e  _status = STOPPED;
    char        _plError[PLERR_LN];
    bool        _muted = false;   /* runtime-only, not persisted (see save_manager.md) */
    bool        _stopFailureReported = false;
    QueueHandle_t _pathQueue = nullptr;
    yoradio::storage::StorageFsBridge _pathFs;
    std::atomic<uint32_t> _pathEpoch{1};
    std::atomic<bool> _singleFile{false};
    std::atomic<uint32_t> _singleEof{0};
    std::atomic<uint32_t> _streamEof{0};
    mutable portMUX_TYPE _controlMux = portMUX_INITIALIZER_UNLOCKED;
    yoradio::playback::PlaybackContext _context;
    yoradio::storage::MediaRef _currentMedia;
    bool _stopPending = false; // priority STOP cannot be lost to a full queue
    std::atomic<yoradio::playback::PathPlayResult> _pathResult{yoradio::playback::PathPlayResult::None};
    std::atomic<uint8_t> _sdRepeat{0}; // SdRepeatMode, RAM-only / только RAM
    std::atomic<bool> _sdShuffle{false};
    bool _folderSearchFromEof = false; // EOF search vs manual Next / поиск EOF vs ручной Next
    void _invalidatePathEpoch(uint32_t expected = 0);
    void _playPath(const yoradio::playback::PathPlayRequest& ref, bool adopt_queue = true);
    void _pollPathTerminal();
    void _pollFolderQueue();
    void _pumpRequests();
    // Admit under _controlMux using required_epoch, then play outside the lock.
    // Допуск под _controlMux по required_epoch, затем play вне блокировки.
    bool _admitPathEpoch(uint32_t required_epoch, uint32_t& play_epoch);
    void _playAdmittedPath(const yoradio::storage::MediaRef& media, uint32_t play_epoch);
    // Replay One / start next using the intent epoch captured before STOP — never re-load after stop.
    // Повтор One / старт next по epoch намерения, снятому до STOP — не перечитывать после stop.
    void _replayCurrentPath(const yoradio::storage::MediaRef& media, uint32_t intent_epoch);
    bool _startFolderNext(const yoradio::storage::MediaRef& cursor, bool from_eof,
                          uint32_t intent_epoch);
    bool _intentStillCurrent(uint32_t intent_epoch) const;
    void _restartFolderSearchIfNeeded();
    bool _commitPlayback(yoradio::playback::PlaybackSource source, uint32_t epoch,
                         const yoradio::storage::MediaRef* media = nullptr, uint16_t station = 0);
    void _confirmStopped();
    // One activation at a time. The loop task probes, then stops, then starts.
    // Одна активация за раз. Задача loop проверяет файл, затем останавливает и запускает.
    struct SourceActivation {
      uint32_t id = 0;
      uint32_t seen_epoch = 0;
      uint32_t epoch = 0;
      yoradio::source::SourceId source = yoradio::source::SourceId::Radio;
      yoradio::source::ActivateOutcome outcome = yoradio::source::ActivateOutcome::Cancelled;
      bool pending = false;
      bool complete = false;
    };
    SourceActivation _act;
    uint32_t _actSeq = 0;
    // Chosen source, separate from the confirmed Playing context.
    // Выбранный источник, отдельно от подтверждённого Playing.
    bool _selectedValid = false;
    yoradio::source::SourceId _selected = yoradio::source::SourceId::Radio;
    // Last file that actually started. RAM only: STOP and Radio keep it, reboot does not.
    // Последний реально запущенный файл. Только RAM: STOP и Radio его сохраняют, reboot — нет.
    bool _sdBookmarkValid = false;
    yoradio::storage::MediaRef _sdBookmark;
    void _serviceSourceActivation();
    /* -1 = never written yet (boot). Dedup for the [MUTE] amp diagnostic - setOutputPins()
     * runs on every play/stop/mute event, often with an unchanged resulting level. */
    int8_t      _ampLastLevel = -1;
  private:
    bool _stop(bool alreadyStopped = false);
    void _reportStopFailure();
    void _play(uint16_t stationId, uint32_t epoch);
    /* The only place that hands a level to the codec - gates config.store.volume through _muted.
     * Единственное место, отдающее уровень в кодек, - фильтрует громкость через _muted. */
    void _applyVolumeToAudio();
    void _applyMuteTransition(bool wasMuted);
    void _applyVolPayload(int payload);
    void _applyMuteRequest(int payload);
  public:
    bool lockOutput = true;
    bool resumeAfterUrl = false;
    uint32_t sd_min, sd_max;
    #ifdef MQTT_ROOT_TOPIC
    char      burl[MQTT_BURL_SIZE];  /* buffer for browseUrl  */
    #endif
  public:
    Player();
    void init();
    void loop();
    void initHeaders(const char *file);
    // E36AUD0A1: emitErrorLog=false for system warnings (##SYS# only) / без ##ERROR# для системных
    void setError(const char *e, bool emitErrorLog = true);
    // E36AUD0A: read-only transport error buffer — not track metadata / только ошибка, не title
    bool hasError() const { return strlen(_plError) > 0; }
    const char* lastError() const { return _plError; }
    // Zero-wait submission; false means not admitted. STOP has a priority latch.
    // Отправка без ожидания; false означает отказ. STOP имеет отдельный приоритет.
    bool sendCommand(playerRequestParams_t request);
    void resetQueue();
    // Submit copies the complete owned locator, never blocks, and promises only
    // queue admission. The owner confirms stop before changing sources/files.
    // Копирует путь/поколение/epoch без ожидания; успех означает постановку в очередь.
    // Владелец подтверждает остановку до смены источника или файла.
    yoradio::playback::PathPlayResult requestPlay(const yoradio::playback::PathPlayRequest& ref);
    // Admit a source switch. Pending means accepted and not yet Playing.
    // Принять переключение источника. Pending значит принято, но это ещё не Playing.
    struct ActivateAdmission {
      uint32_t request = 0;
      yoradio::source::ActivateOutcome outcome = yoradio::source::ActivateOutcome::Cancelled;
    };
    ActivateAdmission activate(yoradio::source::SourceId id);
    yoradio::source::ActivateSnapshot activation() const;
    bool selectedSource(yoradio::source::SourceId& out) const;
    // True only while the confirmed context is Playing.
    // Истина только пока подтверждённый контекст — Playing.
    bool soundingSource(yoradio::source::SourceId& out) const {
      const auto context = playbackContext();
      if (context.state != yoradio::playback::PlaybackState::Playing) return false;
      out = context.source == yoradio::playback::PlaybackSource::Radio
          ? yoradio::source::SourceId::Radio : yoradio::source::SourceId::Sd;
      return true;
    }
    uint32_t pathEpoch() const { return _pathEpoch.load(); }
    yoradio::playback::PathPlayResult pathResult() const { return _pathResult.load(); }
    yoradio::storage::FileError pathFileError() const {
      const auto status=_pathFs.fileStatus();
      return status?status->error.load():yoradio::storage::FileError::NoMemory;
    }
    bool hasPathWork() const;
    yoradio::playback::PlaybackContext playbackContext() const;
    bool currentMedia(yoradio::storage::MediaRef& out) const;
    bool onSingleFileEof(uint32_t epoch = 0);
    void onStreamEof(uint32_t epoch = 0);
    #ifdef MQTT_ROOT_TOPIC
    void browseUrl();
    #endif
    bool remoteStationName = false;
    plStatus_e status() { return _status; }
    void prev();
    void next();
    void toggle();
    // Confirmed RAM SD modes for WebUI/LVGL. Cycle/toggle go through the owner queue.
    // Подтверждённые RAM-режимы SD для WebUI/LVGL. Смена — через очередь владельца.
    yoradio::playback::SdRepeatMode sdRepeatMode() const {
      return static_cast<yoradio::playback::SdRepeatMode>(_sdRepeat.load());
    }
    bool sdShuffle() const { return _sdShuffle.load(); }
    void cycleSdRepeat() { sendCommand({PR_SD_REPEAT, 0}); }
    void toggleSdShuffle() { sendCommand({PR_SD_SHUFFLE, -1}); }
    void setSdShuffle(bool on) { sendCommand({PR_SD_SHUFFLE, on ? 1 : 0}); }
    void stepVol(bool up);
    void setVol(uint8_t volume);
    /* Central semantic MUTE entry point - queue-based, safe from any task/callback.
     * Центральная точка входа MUTE - через очередь, безопасна из любой задачи. */
    void requestMute(int request = PMUTE_TOGGLE) { sendCommand({PR_MUTE, request}); }
    bool isMuted() const { return _muted; }
    /* Volume as every observer must see it: 0 while muted, otherwise the remembered level.
     * Keeps "0 == MUTE" true on UI, WebUI, telnet and MQTT without a second state flag.
     * Громкость для всех наблюдателей: 0 в MUTE, иначе запомненный уровень.
     * Держит правило "0 == MUTE" единым для UI, WebUI, telnet и MQTT. */
    uint8_t audibleVolume() const;
    uint8_t volToI2S(uint8_t volume);
    void stopInfo();
    void setOutputPins(bool isPlaying);
    void setResumeFilePos(int32_t pos) { _resumeFilePos = pos; }
};

extern Player player;

extern __attribute__((weak)) void player_on_start_play();
extern __attribute__((weak)) void player_on_stop_play();
extern __attribute__((weak)) void player_on_track_change();
extern __attribute__((weak)) void player_on_station_change();

#endif
