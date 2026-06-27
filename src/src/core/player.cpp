#include "options.h"

#include "player.h"

#include "config.h"
#include "mem_watchdog.h"
#include "telnet.h"
#include "display.h"
#include "sdmanager.h"
#include "netserver.h"

#include <freertos/portmacro.h>

Player player;
QueueHandle_t playerQueue;

// PR_VOL overflow merge — non-blocking send leaves latest volume here for next loop() — avoids deadlock / merge при полной очереди
static portMUX_TYPE s_vol_merge_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_vol_merge_pending = false;
static volatile int s_vol_merge_payload = 0;

static void playerApplyVolFromPayload(int payload) {
  uint8_t v = static_cast<uint8_t>(payload);
  if (v > 254) v = 254;
  config.setVolume(v);
  player.setVolume(player.volToI2S(v));
}

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
  setOutputPins(false);
  delay(50);
  memset(_plError, 0, PLERR_LN);
#ifdef MQTT_ROOT_TOPIC
  memset(burl, 0, MQTT_BURL_SIZE);
#endif
  if(MUTE_PIN!=255) pinMode(MUTE_PIN, OUTPUT);
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
  _loadVol(config.store.volume);
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
  strlcpy(_plError, e ? e : "", PLERR_LN);
  if (hasError() && emitErrorLog) {
    telnet.printf("##ERROR#:\t%s\n", _plError);
  }
}

void Player::_stop(bool alreadyStopped){
  log_i("%s called", __func__);
  if(config.getMode()==PM_SDCARD && !alreadyStopped) config.sdResumePos = player.getFilePos();
  _status = STOPPED;
  setOutputPins(false);
  if(!hasError()) config.setTitle((display.mode()==LOST || display.mode()==UPDATING)?"":const_PlStopped);
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
void Player::loop() {
  if(playerQueue==NULL) return;
  playerRequestParams_t requestP;

  if(xQueueReceive(playerQueue, &requestP, isRunning()?PL_QUEUE_TICKS:PL_QUEUE_TICKS_ST)){
    switch (requestP.type){
      case PR_STOP: _stop(); break;
      case PR_PLAY: {
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
        playerApplyVolFromPayload(requestP.payload);
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
      playerApplyVolFromPayload(merge_payload);
    }
  }

  Audio::loop();
  if(!isRunning() && _status==PLAYING) _stop(true);
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

void Player::setOutputPins(bool isPlaying) {
  if(REAL_LEDBUILTIN!=255) digitalWrite(REAL_LEDBUILTIN, LED_INVERT?!isPlaying:isPlaying);
  bool _ml = MUTE_LOCK?!MUTE_VAL:(isPlaying?!MUTE_VAL:MUTE_VAL);
  if(MUTE_PIN!=255) digitalWrite(MUTE_PIN, _ml);
}

void Player::_play(uint16_t stationId) {
  Serial.printf("🎵 [PLAY] _play() started, stationId=%d\n", stationId);
  log_i("%s called, stationId=%d", __func__, stationId);
  setError("");
  remoteStationName = false;
  config.setDspOn(1);
  config.vuThreshold = 0;
  Serial.printf("🎵 [PLAY] Basic setup completed\n");
  config.screensaverTicks=SCREENSAVERSTARTUPDELAY;
  config.screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
  setOutputPins(false);
  config.setTitle(config.getMode()==PM_WEB?const_PlConnect:"");
//  config.setTitle(config.getMode()==PM_WEB?const_PlConnect:"[next track]");
  config.station.bitrate = 0;
  config.station.stream_sample_rate_hz   = 0;
  config.station.stream_bits_per_sample = 0;
  config.setBitrateFormat(BF_UNCNOWN);
  Serial.printf("🎵 [PLAY] About to call config.loadStation(%d)\n", stationId);
  config.loadStation(stationId);
  Serial.printf("🎵 [PLAY] config.loadStation() completed\n");
  _loadVol(config.store.volume);
  Serial.printf("🎵 [PLAY] _loadVol() completed\n");
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
  remoteStationName = true;
  config.setDspOn(1);
  resumeAfterUrl = _status==PLAYING;
//  setDefaults();
  setOutputPins(false);
  config.setTitle(const_PlConnect);
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

void Player::_loadVol(uint8_t volume) {
  setVolume(volToI2S(volume));
}

void Player::setVol(uint8_t volume) {
  _volTicks = millis();
  _volTimer = true;
  player.sendCommand({PR_VOL, volume});
}
