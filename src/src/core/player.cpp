#include "options.h"

#include "player.h"

#include "config.h"
#include "mem_watchdog.h"
#include "telnet.h"
#include "display.h"
#include "sdmanager.h"
#include "netserver.h"
#include "../i18n/i18n.h"

#include <freertos/portmacro.h>

Player player;
QueueHandle_t playerQueue;

// PR_VOL overflow merge — non-blocking send leaves latest volume here for next loop() — avoids deadlock / merge при полной очереди
static portMUX_TYPE s_vol_merge_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_vol_merge_pending = false;
static volatile int s_vol_merge_payload = 0;

#if VS1053_CS!=255 && !I2S_INTERNAL

#ifdef ARDUINO_ESP32S3_DEV
  #if VS_SSPI
//    Player::Player(): Audio(VS1053_CS, VS1053_DCS, VS1053_DREQ, &SPI2, VS1053_MOSI, VS1053_MISO, VS1053_SCK) {}
    Player::Player(): Audio(VS1053_CS, VS1053_DCS, VS1053_DREQ, &SPI3_HOST, 35, 37, 36) {}	// SubSPI- SPI3, MOSI-35, MISO-37, SCK-36
  #else
//    Player::Player(): Audio(VS1053_CS, VS1053_DCS, VS1053_DREQ, FSPI, 11, 13, 12) {}	// FSPI - SPI2, MOSI-11, MISO-12, SCK-12
    Player::Player(): Audio(VS1053_CS, VS1053_DCS, VS1053_DREQ, &SPI) {}	// FSPI - SPI2, MOSI-11, MISO-12, SCK-12
  #endif

#else
  #if VS_HSPI
    Player::Player(): Audio(VS1053_CS, VS1053_DCS, VS1053_DREQ, &SPI2) {}			// HSPI, MOSI-13, MISO-12, SCK-14
  #else
    Player::Player(): Audio(VS1053_CS, VS1053_DCS, VS1053_DREQ, &SPI) {}			// VSPI, MOSI-23, MISO-19, SCK-18
  #endif
#endif
  void ResetChip(){
    pinMode(VS1053_RST, OUTPUT);
    digitalWrite(VS1053_RST, LOW);
    delay(30);
    digitalWrite(VS1053_RST, HIGH);
    delay(100);
  }
#else
  #if !I2S_INTERNAL
    Player::Player() {}
  #else
    Player::Player(): Audio(true, I2S_DAC_CHANNEL_BOTH_EN)  {}
  #endif
#endif


void Player::init() {
  Serial.print("##[BOOT]#\tplayer.init\t");
  playerQueue=NULL;
  _resumeFilePos = 0;
  playerQueue = xQueueCreate(PLAYER_QUEUE_LENGTH, sizeof(playerRequestParams_t));
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
  #else
    SPI.begin();
    if(VS1053_RST>0) ResetChip();
    begin();
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

void Player::sendCommand(playerRequestParams_t request){
  if(playerQueue==NULL) return;
  // PR_VOL: never block — burst volume changes must not stall loop()/telnet/WebUI waiting on queue space.
  // Только для VOL — без ожидания; при полной очереди сохраняем последнее значение (merge).
  if(request.type == PR_VOL) {
    if(xQueueSend(playerQueue, &request, 0) != pdTRUE) {
      portENTER_CRITICAL(&s_vol_merge_mux);
      s_vol_merge_payload = request.payload;
      s_vol_merge_pending = true;
      portEXIT_CRITICAL(&s_vol_merge_mux);
    }
    return;
  }
  xQueueSend(playerQueue, &request, PLQ_SEND_DELAY);
}

void Player::resetQueue(){
	if(playerQueue!=NULL) xQueueReset(playerQueue);
  portENTER_CRITICAL(&s_vol_merge_mux);
  s_vol_merge_pending = false;
  portEXIT_CRITICAL(&s_vol_merge_mux);
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

void Player::_stop(bool alreadyStopped){
  log_i("%s called", __func__);
  beginPlaybackSession();
  if(config.getMode()==PM_SDCARD && !alreadyStopped) config.sdResumePos = player.getFilePos();
  _status = STOPPED;
  setOutputPins(false);
  if(!hasError()) config.setTitle((display.mode()==LOST || display.mode()==UPDATING)
                                      ? ""
                                      : i18n::text(i18n::TextId::PlayerStopped));
  config.station.bitrate = 0;
  config.station.stream_sample_rate_hz   = 0;
  config.station.stream_bits_per_sample = 0;
  config.setBitrateFormat(BF_UNCNOWN);
  netserver.requestOnChange(BITRATE, 0);
  display.putRequest(DBITRATE);

  setDefaults();
  if(!alreadyStopped) stopSong();
  if(!lockOutput) stopInfo();
  if (player_on_stop_play) player_on_stop_play();
}

void Player::initHeaders(const char *file) {
  if(strlen(file)==0 || true) return; //TODO Read TAGs
  Serial.printf("🎵 [SD HEADERS] Reading headers from: %s\n", file);
  connecttoFS(sdman,file);
  eofHeader = false;
  while(!eofHeader) Audio::loop();
  Serial.printf("🎵 [SD HEADERS] Headers read completed\n");
  //netserver.requestOnChange(SDPOS, 0);
  setDefaults();
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

void Player::loop() {
  if(playerQueue==NULL) return;
  playerRequestParams_t requestP;

  if(xQueueReceive(playerQueue, &requestP, isRunning()?PL_QUEUE_TICKS:PL_QUEUE_TICKS_ST)){
    switch (requestP.type){
      case PR_STOP:
        cancelWebstreamReconnect("manual stop");
        _stop();
        break;
      case PR_PLAY: {
        cancelWebstreamReconnect("station change");
        Serial.printf("🎵 [PLAYER] Received PR_PLAY command, payload: %d\n", requestP.payload);
        if (requestP.payload>0) {
          config.setLastStation((uint16_t)requestP.payload);
        }
        Serial.printf("🎵 [PLAYER] Calling _play() with stationId: %d\n", (uint16_t)abs(requestP.payload));
        _play((uint16_t)abs(requestP.payload)); 
        if (player_on_station_change) player_on_station_change(); 
        break;
      }
      case PR_VOL: {
        _applyVolPayload(requestP.payload);
        break;
      }
      case PR_MUTE: {
        _applyMuteRequest(requestP.payload);
        break;
      }
      #ifdef USE_SD
      case PR_CHECKSD: {
        if(config.getMode()==PM_SDCARD){
          if(!sdman.cardPresent()){
            sdman.stop();
            config.changeMode(PM_WEB);
          }
        }
        break;
      }
      #endif
      case PR_VUTONUS:
        if(config.vuThreshold>10) config.vuThreshold -=10;
      default: break;
    }
  }

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
#ifdef MEM_WATCHDOG_AUTOREBOOT
  mwPollPlaybackRecovery(_status, isRunning() && !isWebstreamReconnectPending());
#endif
  if(!isRunning() && !isWebstreamReconnectPending() && _status==PLAYING) {
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
    _stop(true);
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
}

/* LED keeps following playback only. The amplifier line follows AUDIBILITY: it is released
 * exclusively while the radio is playing AND not semantically muted, so BTN_MUTE / IR / WebUI
 * silence the amplifier itself instead of only zeroing the codec. Polarity is unchanged:
 * MUTE_VAL is still the asserted (muted) level and MUTE_LOCK still pins the line to !MUTE_VAL
 * for boards that must never assert it - there, semantic MUTE stays codec-only by design.
 * Светодиод по-прежнему следует за воспроизведением. Линия усилителя следует за СЛЫШИМОСТЬЮ:
 * отпускается только при playing И не в MUTE. Полярность не меняется: MUTE_VAL - активный
 * (заглушённый) уровень, MUTE_LOCK по-прежнему фиксирует линию в !MUTE_VAL. */
void Player::setOutputPins(bool isPlaying) {
  if(REAL_LEDBUILTIN!=255) digitalWrite(REAL_LEDBUILTIN, LED_INVERT?!isPlaying:isPlaying);
  const bool audible = isPlaying && !_muted;
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

void Player::_play(uint16_t stationId) {
  Serial.printf("🎵 [PLAY] _play() started, stationId=%d\n", stationId);
  log_i("%s called, stationId=%d", __func__, stationId);
  beginPlaybackSession();
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
  config.setTitle(config.getMode()==PM_WEB?i18n::text(i18n::TextId::PlayerConnecting):"");
//  config.setTitle(config.getMode()==PM_WEB?i18n::text(i18n::TextId::PlayerConnecting):"[next track]");
  config.station.bitrate = 0;
  config.station.stream_sample_rate_hz   = 0;
  config.station.stream_bits_per_sample = 0;
  config.setBitrateFormat(BF_UNCNOWN);
  Serial.printf("🎵 [PLAY] About to call config.loadStation(%d)\n", stationId);
  config.loadStation(stationId);
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
  bool isConnected = false;
  if(config.getMode()==PM_SDCARD && SDC_CS!=255){
    Serial.printf("🎵 [SD PLAY] Attempting to play: %s\n", config.station.url);
    Serial.printf("🎵 [SD PLAY] Resume position: %d\n", config.sdResumePos==0?_resumeFilePos:config.sdResumePos-player.sd_min);
    isConnected=connecttoFS(sdman,config.station.url,config.sdResumePos==0?_resumeFilePos:config.sdResumePos-player.sd_min);
    Serial.printf("🎵 [SD PLAY] connecttoFS result: %s\n", isConnected ? "SUCCESS" : "FAILED");
  }else {
    config.saveValue(&config.store.play_mode, static_cast<uint8_t>(PM_WEB));
  }
  if(config.getMode()==PM_WEB) isConnected=connecttohost(config.station.url);
  if(isConnected){
  //if (config.store.play_mode==PM_WEB?connecttohost(config.station.url):connecttoFS(SD,config.station.url,config.sdResumePos==0?_resumeFilePos:config.sdResumePos-player.sd_min)) {
    _status = PLAYING;
    if(config.getMode()==PM_SDCARD) {
      config.sdResumePos = 0;
      config.saveValue(&config.store.lastSdStation, stationId);
    }
    //config.setTitle("");
//    config.setSmartStart(1);
    if (config.store.smartstart < 2) config.setSmartStart(1);			//*********************************
    netserver.requestOnChange(MODE, 0);
    setOutputPins(true);
    display.putRequest(NEWMODE, PLAYER);
    if (player_on_start_play) player_on_start_play();
  }else{
    SET_PLAY_ERROR("Error connecting to %s", config.station.url);
#ifdef MEM_WATCHDOG_AUTOREBOOT
    memWatchdog.record(MWEvent::HTTP_FAIL);
    { auto d = memWatchdog.evaluate(); if (d.trigger) memWatchdog.armReboot(); }
#endif
    _stop(true);
  };
}

#ifdef MQTT_ROOT_TOPIC
void Player::browseUrl(){
  setError("");
  beginPlaybackSession();
  remoteStationName = true;
  config.setDspOn(1);
  resumeAfterUrl = _status==PLAYING;
//  setDefaults();
  setOutputPins(false);
  config.setTitle(i18n::text(i18n::TextId::PlayerConnecting));
  if (connecttohost(burl)){
    _status = PLAYING;
    config.setTitle("");
    netserver.requestOnChange(MODE, 0);
    setOutputPins(true);
    if (player_on_start_play) player_on_start_play();
  }else{
    SET_PLAY_ERROR("Error connecting to %s", burl);
#ifdef MEM_WATCHDOG_AUTOREBOOT
    memWatchdog.record(MWEvent::HTTP_FAIL);
    { auto d = memWatchdog.evaluate(); if (d.trigger) memWatchdog.armReboot(); }
#endif
    _stop(true);
  }
  memset(burl, 0, MQTT_BURL_SIZE);
}
#endif

void Player::prev() {
  
  uint16_t lastStation = config.lastStation();
  if(config.getMode()==PM_WEB || !config.store.sdsnuffle){
    if (lastStation == 1) config.lastStation(config.store.countStation); else config.lastStation(lastStation-1);
  }
  sendCommand({PR_PLAY, config.lastStation()});
}

void Player::next() {
  uint16_t lastStation = config.lastStation();
  if(config.getMode()==PM_WEB || !config.store.sdsnuffle){
    if (lastStation == config.store.countStation) config.lastStation(1); else config.lastStation(lastStation+1);
  }else{
    config.lastStation(random(1, config.store.countStation));
  }
  sendCommand({PR_PLAY, config.lastStation()});
}

void Player::toggle() {
  if (_status == PLAYING) {
    sendCommand({PR_STOP, 0});
  } else {
    sendCommand({PR_PLAY, config.lastStation()});
  }
}

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
