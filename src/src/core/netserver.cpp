#include "netserver.h"
#include <LittleFS.h>
#include <cstring>
#include <cstdarg>

#include "config.h"

// W-R1B: coordinate-save diagnostics gate — off by default.
// W-R1B: гейт диагностики сохранения координат — выключен по умолчанию.
#ifndef YORADIO_WEATHER_REQ_DIAG
#define YORADIO_WEATHER_REQ_DIAG 0
#endif
#include "save_manager.h"
#include "player.h"
#include "telnet.h"
#include "display.h"
#include "options.h"
#include "network.h"
#include "mqtt.h"
#include "controls.h"
#include <Update.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "../lvgl_ui/profiles/lv_profile_select.h"
#include "../lvgl_ui/theme/lv_theme_yoradio.h"
#include "../ai/ai_subsystem.h"
#include "../ai/ai_log.h"  // AI Layer logging macros

// Forward declarations for AI config functions from config.cpp / Forward объявления для функций AI config из config.cpp
// AIConfig structure is now defined in config.h (included above)
// Структура AIConfig теперь определена в config.h (включена выше)
bool aiLoadFromFS(AIConfig& out);
bool aiSaveToFS(const AIConfig& cfg);
void aiSetDefaults(AIConfig& cfg);
bool aiIsValidForEnable(const AIConfig& cfg);
void aiApplyToStore(const AIConfig& cfg);
#ifdef USE_SD
#include "sdmanager.h"
#endif
#ifndef MIN_MALLOC
#define MIN_MALLOC 24112
#endif
#ifndef NSQ_SEND_DELAY
  #define NSQ_SEND_DELAY       (TickType_t)100  //portMAX_DELAY?
#endif

//#define CORS_DEBUG

NetServer netserver;

AsyncWebServer webserver(80);
AsyncWebSocket websocket("/ws");
AsyncUDP udp;
static uint32_t s_ws_boot_guard_started_ms = 0;
constexpr uint32_t kWsHeavyClientBootGuardMs = 12000;

String processor(const String& var);
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleUploadWeb(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleWebUploadComplete(AsyncWebServerRequest *request);
void handleUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleHTTPArgs(AsyncWebServerRequest * request);
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void beginUploadBg(AsyncWebServerRequest *request);
void handleUploadBg(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void beginUploadTheme(AsyncWebServerRequest* request);
void handleUploadTheme(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final);
void handleRemoveThemeHttp(AsyncWebServerRequest* request);
void handleRemoveBgHttp(AsyncWebServerRequest *request);
void handleSetThemeHttp(AsyncWebServerRequest *request);
// Station Art MVP: /upload_art, /remove_art, /art_status
void beginUploadArt(AsyncWebServerRequest *request);
void handleUploadArt(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleRemoveArtHttp(AsyncWebServerRequest *request);
void handleArtStatusHttp(AsyncWebServerRequest *request);
void handleBgStatusHttp(AsyncWebServerRequest *request);

bool  shouldReboot  = false;
#ifdef MQTT_ROOT_TOPIC
Ticker mqttplaylistticker;
bool  mqttplaylistblock = false;
void mqttplaylistSend() {
  mqttplaylistblock = true;
  mqttplaylistticker.detach();
  mqttPublishPlaylist();
  mqttplaylistblock = false;
}
#endif

char* updateError() {
  static char ret[140] = {0};
  sprintf(ret, "Update failed with error (%d)<br /> %s", (int)Update.getError(), Update.errorString());
  return ret;
}

NetServer::TcpRole NetServer::_currentTcpRole() {
  if (network.status == SOFT_AP) {
    return TcpRole::Ap;
  }
  if (network.status == CONNECTED) {
    return TcpRole::Sta;
  }
  return TcpRole::None;
}

void NetServer::_logStarted(bool quiet) {
  if (network.status == CONNECTED) {
    Serial.printf("[NetServer] Started on STA %s\n", WiFi.localIP().toString().c_str());
  } else if (network.status == SOFT_AP) {
    Serial.printf("[NetServer] Started on AP %s\n", WiFi.softAPIP().toString().c_str());
  }
  if (!quiet) {
    Serial.println("done");
  }
}

bool NetServer::begin(bool quiet) {
  if(network.status==SDREADY) return true;
  if (!network.isTcpReady()) {
    if (_listening) {
      webserver.end();
      _listening = false;
    }
    if (!quiet) {
      Serial.println("[NetServer] Start deferred: no usable STA/AP IP");
    }
    return false;
  }
  const TcpRole role = _currentTcpRole();
  if (_handlersReady && _listening && _tcpRole == role) {
    return true;
  }
  if (_handlersReady) {
    if (_listening) {
      webserver.end();
      _listening = false;
    }
    webserver.begin();
    _listening = true;
    _tcpRole = role;
    _logStarted(quiet);
    return true;
  }
  if(!quiet) Serial.print("##[BOOT]#\tnetserver.begin\t");
  importRequest = IMDONE;
  irRecordEnable = false;
  s_ws_boot_guard_started_ms = millis();
  nsQueue = xQueueCreate( 20, sizeof( nsRequestParams_t ) );
  while(nsQueue==NULL){;}
  if(config.emptyFS){
    webserver.on("/bg_status", HTTP_GET, handleBgStatusHttp);
    webserver.on("/", HTTP_GET, [](AsyncWebServerRequest * request) { request->send_P(200, "text/html", emptyfs_html, processor); });
    // E36FS3a: unified multipart completion for emptyFS bootstrap / единый completion для POST /.
    webserver.on("/", HTTP_POST, handleWebUploadComplete, handleUploadWeb);
  }else{
    /* /bg_status BEFORE HTTP_ANY "/" — catch-all can swallow GET /bg_status and never send() → 500 */
    webserver.on("/bg_status", HTTP_GET, handleBgStatusHttp);
    webserver.on("/", HTTP_ANY, handleHTTPArgs);
    webserver.on("/webboard", HTTP_GET, [](AsyncWebServerRequest * request) { request->send_P(200, "text/html", emptyfs_html, processor); });
    // E36FS3a: same completion path as emptyFS POST / / тот же completion, что и POST /.
    webserver.on("/webboard", HTTP_POST, handleWebUploadComplete, handleUploadWeb);
  }
  
  webserver.on(PLAYLIST_PATH, HTTP_GET, handleHTTPArgs);
  webserver.on(INDEX_PATH, HTTP_GET, handleHTTPArgs);
  webserver.on(PLAYLIST_SD_PATH, HTTP_GET, handleHTTPArgs);
  webserver.on(INDEX_SD_PATH, HTTP_GET, handleHTTPArgs);
  webserver.on(SSIDS_PATH, HTTP_GET, handleHTTPArgs);
  
  webserver.on("/upload", HTTP_POST, beginUpload, handleUpload);
  webserver.on("/update", HTTP_GET, handleHTTPArgs);
  webserver.on("/update", HTTP_POST, beginUpdate, handleUpdate);
  webserver.on("/settings", HTTP_GET, handleHTTPArgs);
  webserver.on("/appearance", HTTP_GET, handleHTTPArgs);
  // Main background .bin → /bg/main_{dark,light,custom}.bin (Stage 6.1F-d) / Фон Main в слоты LittleFS
  webserver.on("/upload_bg", HTTP_POST, beginUploadBg, handleUploadBg);
  webserver.on("/remove_bg", HTTP_POST, handleRemoveBgHttp);
  // Stage 6.6R-B: runtime theme preset switch (enqueues SET_THEME_PRESET on DspTask; no LVGL here)
  // Этап 6.6R-B: переключение пресета темы (очередь DspTask; LVGL здесь не вызывается)
  webserver.on("/set_theme", HTTP_POST, handleSetThemeHttp);
  // Stage 6.6R-F1: custom palette file /data/theme_custom.txt (not theme.dat)
  // Этап 6.6R-F1: файл палитры Custom — отдельно от выбора пресета
  webserver.on("/upload_theme", HTTP_POST, beginUploadTheme, handleUploadTheme);
  webserver.on("/remove_theme", HTTP_POST, handleRemoveThemeHttp);
  // Station Art MVP: per-station art /logo/<key>.bin / Station Art MVP: арт станции
  webserver.on("/art_status", HTTP_GET, handleArtStatusHttp);
  webserver.on("/upload_art", HTTP_POST, beginUploadArt, handleUploadArt);
  webserver.on("/remove_art", HTTP_POST, handleRemoveArtHttp);
  if (IR_PIN != 255) webserver.on("/ir", HTTP_GET, handleHTTPArgs);
  webserver.serveStatic("/", LittleFS, "/www/").setCacheControl("max-age=31536000");
  /* No handler matched → library sends 500; route /bg_status here too + 404 for stray GET / Нет маршрута → 500; запасной /bg_status */
  webserver.onNotFound([](AsyncWebServerRequest* request) {
    if (request->method() == HTTP_GET && request->url() == "/bg_status") {
      handleBgStatusHttp(request);
      return;
    }
    request->send(404, "text/plain", "Not found");
  });
#ifdef CORS_DEBUG
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Origin"), F("*"));
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Headers"), F("content-type"));
#endif
  webserver.begin();
  if(strlen(config.store.mdnsname)>0)
    MDNS.begin(config.store.mdnsname);
  websocket.onEvent(onWsEvent);
  webserver.addHandler(&websocket);

  //echo -n "helle?" | socat - udp-datagram:255.255.255.255:44490,broadcast
  if (udp.listen(44490)) {
    udp.onPacket([](AsyncUDPPacket packet) {
      if (strcmp((char*)packet.data(), "helle?") == 0)
        packet.println(WiFi.localIP());
    });
  }
  _handlersReady = true;
  _listening = true;
  _tcpRole = role;
  _logStarted(quiet);
  return true;
}

void NetServer::beginUpdate(AsyncWebServerRequest *request) {
  shouldReboot = !Update.hasError();
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : updateError());
  response->addHeader("Connection", "close");
  request->send(response);
}

void handleUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    sm::suspendForOta();
    int target = (request->getParam("updatetarget", true)->value() == "spiffs") ? U_SPIFFS : U_FLASH;
    Serial.printf("Update Start: %s\n", filename.c_str());
    player.sendCommand({PR_STOP, 0});
    display.putRequest(NEWMODE, UPDATING);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, target)) {
      Update.printError(Serial);
      sm::resumeAfterOta();
      request->send(200, "text/html", updateError());
      return;
    }
  }
  if (!Update.hasError()) {
    if (Update.write(data, len) != len) {
      Update.printError(Serial);
      sm::resumeAfterOta();
      request->send(200, "text/html", updateError());
      return;
    }
  }
  if (final) {
    if (Update.end(true)) {
      Serial.printf("Update Success: %uB\n", index + len);
      sm::resumeAfterOta();
    } else {
      Update.printError(Serial);
      sm::resumeAfterOta();
      request->send(200, "text/html", updateError());
    }
  }
}

void NetServer::beginUpload(AsyncWebServerRequest *request) {
  if (request->hasParam("plfile", true, true)) {
    netserver.importRequest = IMPL;
    request->send(200);
  } else if (request->hasParam("wifile", true, true)) {
    netserver.importRequest = IMWIFI;
    request->send(200);
  } else {
    request->send(404);
  }
}

size_t NetServer::chunkedHtmlPageCallback(uint8_t* buffer, size_t maxLen, size_t index){
  File requiredfile;
  bool sdpl = strcmp(netserver.chunkedPathBuffer, PLAYLIST_SD_PATH) == 0;
  if(sdpl){
    requiredfile = config.SDPLFS()->open(netserver.chunkedPathBuffer, "r");
  }else{
    requiredfile = LittleFS.open(netserver.chunkedPathBuffer, "r");
  }
  if (!requiredfile) return 0;
  size_t filesize = requiredfile.size();
  size_t needread = filesize - index;
  if (!needread) {
    requiredfile.close();
    return 0;
  }
  size_t canread = (needread > maxLen) ? maxLen : needread;
  DBGVB("[%s] seek to %d in %s and read %d bytes with maxLen=%d", __func__, index, netserver.chunkedPathBuffer, canread, maxLen);
  requiredfile.seek(index, SeekSet);
  //vTaskDelay(1);
  requiredfile.read(buffer, canread);
  index += canread;
  if (requiredfile) requiredfile.close();
  return canread;
}

void NetServer::chunkedHtmlPage(const String& contentType, AsyncWebServerRequest *request, const char * path, bool doproc) {
  memset(chunkedPathBuffer, 0, sizeof(chunkedPathBuffer));
  strlcpy(chunkedPathBuffer, path, sizeof(chunkedPathBuffer)-1);
  AsyncWebServerResponse *response;
  if(doproc)
    response = request->beginChunkedResponse(contentType, chunkedHtmlPageCallback, processor);
  else
    response = request->beginChunkedResponse(contentType, chunkedHtmlPageCallback);
  request->send(response);
}

#ifndef DSP_NOT_FLIPPED
  #define DSP_CAN_FLIPPED true
#else
  #define DSP_CAN_FLIPPED false
#endif
#if !defined(HIDE_WEATHER) && !defined(DUMMYDISPLAY)
  #define SHOW_WEATHER  true
#else
  #define SHOW_WEATHER  false
#endif

#ifndef NS_QUEUE_TICKS
  #define NS_QUEUE_TICKS 0
#endif

const char *getFormat(BitrateFormat _format) {
  switch (_format) {
    case BF_MP3:  return "MP3";
    case BF_AAC:  return "AAC";
    case BF_FLAC: return "FLC";
    case BF_OGG:  return "OGG";
    case BF_WAV:  return "WAV";
    case BF_VOR:  return "VOR";
    case BF_OPU:  return "OPU";
    default:      return "bitrate";
  }
}

char wsbuf[BUFLEN * 2];

// S6V10A: Bounded formatting and safe WebSocket send helpers.
// S6V10A: Ограниченное форматирование и безопасные хелперы отправки WebSocket.

// Format wsbuf with vsnprintf; clear wsbuf and return false on truncation so callers
// never send partial/malformed JSON. / Форматирует wsbuf через vsnprintf; сбрасывает wsbuf
// и возвращает false при обрезке — частичный JSON не отправляется никогда.
static bool wsbufFormat(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int written = vsnprintf(wsbuf, sizeof(wsbuf), fmt, args);
  va_end(args);
  if (written < 0 || written >= (int)sizeof(wsbuf)) {
    wsbuf[0] = '\0'; // drop truncated message — do not send partial JSON / сбрасываем обрезанное сообщение
    return false;
  }
  return true;
}

// Skip textAll when there are no WebSocket clients — avoids AsyncWebSocket::makeBuffer
// allocation under network/Wi-Fi-loss pressure. / Пропускаем textAll при отсутствии клиентов
// — избегаем malloc makeBuffer при нестабильной сети.
static void safeWsTextAll(const char* payload) {
  if (!payload || payload[0] == '\0') return;
  if (websocket.count() == 0) return;
  if (!websocket.availableForWriteAll()) return;
  websocket.textAll(payload);
}

// Skip per-client text() if the client has already disconnected.
// Пропускаем text() если клиент уже отключился.
static void safeWsTextClient(uint32_t clientId, const char* payload) {
  if (!payload || payload[0] == '\0') return;
  AsyncWebSocketClient* client = websocket.client(clientId);
  if (client == nullptr) return;
  if (client->status() != WS_CONNECTED) return;
  if (client->client() == nullptr) return;
  if (!client->canSend()) return;
  if (client->queueIsFull()) return;
  if (!websocket.availableForWrite(clientId)) return;
  client->text(payload, strlen(payload));
}

static bool isHeavyClientStateRequest(requestType_e type) {
  switch (type) {
    case GETMODE:
      return false;
    case GETINDEX:
    case GETACTIVE:
    case GETSYSTEM:
    case GETSCREEN:
    case GETTIMEZONE:
    case GETWEATHER:
    case GETAI:
    case GETCONTROLS:
    case PLAYLIST:
    case PLAYLISTSAVED:
    case STATION:
    case STATIONNAME:
    case ITEM:
    case TITLE:
    case PLAYER_ERROR:
    case VOLUME:
    case NRSSI:
    case BITRATE:
    case MODE:
    case EQUALIZER:
    case BALANCE:
    case SDPOS:
    case SDLEN:
    case SDSNUFFLE:
    case SDINIT:
    case GETPLAYERMODE:
    case DSPON:
      return true;
    default:
      return false;
  }
}

static bool shouldDropEarlyClientState(requestType_e type, uint8_t clientId) {
  if (clientId == 0) return false;
  if (!isHeavyClientStateRequest(type)) return false;
  if (s_ws_boot_guard_started_ms == 0) return false;
  if ((uint32_t)(millis() - s_ws_boot_guard_started_ms) >= kWsHeavyClientBootGuardMs) return false;
  // S6V10F: keep WebUI reachable, but drop heavy per-client initial state while boot/Main preload is still allocating.
  // S6V10F: WebUI остаётся доступен, но тяжёлый per-client initial state дропаем во время ранних boot/Main malloc.
  return true;
}

void NetServer::processQueue(){
  if(nsQueue==NULL) return;
  nsRequestParams_t request;
  if(xQueueReceive(nsQueue, &request, NS_QUEUE_TICKS)){
    // 8.1HX-B: clear coalescing flag on dequeue (before processing) so a state change
    // occurring during processing can re-queue and is not lost.
    // 8.1HX-B: снимаем флаг при извлечении (до обработки), чтобы изменение во время
    // обработки могло снова встать в очередь и не потерялось.
    if (request.clientId == 0 && (uint8_t)request.type < NS_BROADCAST_PENDING_COUNT) {
      portENTER_CRITICAL(&_pendingMux);
      _broadcastPending[(uint8_t)request.type] = false;
      portEXIT_CRITICAL(&_pendingMux);
    }
    memset(wsbuf, 0, BUFLEN * 2);
    uint8_t clientId = request.clientId;
    if (shouldDropEarlyClientState(request.type, clientId)) return;
    switch (request.type) {
      case PLAYLIST:        getPlaylist(clientId); break;
      case PLAYLISTSAVED:   {
        #ifdef USE_SD
        if(config.getMode()==PM_SDCARD) {
        //  config.indexSDPlaylist();
          config.initSDPlaylist();
        }
        #endif
        if(config.getMode()==PM_WEB){
          config.indexPlaylist(); 
          config.initPlaylist(); 
        }
        getPlaylist(clientId); break;
      }
      case GETACTIVE: {
          bool dbgact = false;
          String act = F("\"group_wifi\",");
          if (network.status == CONNECTED) {
                                                                act += F("\"group_system\",");
            if (BRIGHTNESS_PIN != 255 || DSP_CAN_FLIPPED || dbgact)    act += F("\"group_display\",");
                                                              #if defined(LCD_I2C) || defined(DSP_OLED)
                                                                act += F("\"group_oled\",");
                                                              #endif
                                                              #ifndef HIDE_VU
                                                                // Future LVGL VU: WebUI group for config.store.vumeter (8.1F-B).
                                                                act += F("\"group_vu\",");
                                                              #endif
            #ifdef ENABLE_BRIGHTNESS_CONTROL
                                                                act += F("\"group_brightness\",");
            #endif
            if (DSP_CAN_FLIPPED || dbgact)                      act += F("\"group_tft\",");
            if (TS_MODEL != TS_MODEL_UNDEFINED || dbgact)       act += F("\"group_touch\",");
                                                                act += F("\"group_timezone\",");
            if (SHOW_WEATHER || dbgact)                         act += F("\"group_weather\",");
                                                                act += F("\"group_ai\",");
                                                                act += F("\"group_controls\",");
            if (ENC_BTNL != 255 || ENC2_BTNL != 255 || dbgact)  act += F("\"group_encoder\",");
            if (IR_PIN != 255 || dbgact)                        act += F("\"group_ir\",");
          }
                                                                act = act.substring(0, act.length() - 1);
          wsbufFormat( "{\"act\":[%s]}", act.c_str());
          break;
        }
      case GETMODE:       wsbufFormat( "{\"pmode\":\"%s\"}", network.status == CONNECTED ? "player" : "ap"); break;
      case GETINDEX:      {
          requestOnChange(STATION, clientId); 
          requestOnChange(TITLE, clientId); 
          requestOnChange(PLAYER_ERROR, clientId);
          requestOnChange(VOLUME, clientId); 
          requestOnChange(EQUALIZER, clientId); 
          requestOnChange(BALANCE, clientId); 
          requestOnChange(BITRATE, clientId); 
          requestOnChange(MODE, clientId); 
          requestOnChange(SDINIT, clientId);
          requestOnChange(GETPLAYERMODE, clientId); 
          if (config.getMode()==PM_SDCARD) { requestOnChange(SDPOS, clientId); requestOnChange(SDLEN, clientId); requestOnChange(SDSNUFFLE, clientId); } 
          return; 
          break;
        }
      case GETSYSTEM:     wsbufFormat( "{\"sst\":%d,\"aif\":%d,\"vu\":%d,\"softr\":%d,\"vut\":%d,\"mdns\":\"%s\"}", 
                                  config.store.smartstart != 2, 
                                  config.store.audioinfo, 
                                  config.store.vumeter, 
                                  config.store.softapdelay,
                                  config.vuThreshold,
                                  config.store.mdnsname); 
                                  break;
      case GETSCREEN:     wsbufFormat( "{\"flip\":%d,\"inv\":%d,\"nump\":%d,\"tsf\":%d,\"tsd\":%d,\"dspon\":%d,\"br\":%d,\"con\":%d,\"scre\":%d,\"scrt\":%d,\"scrb\":%d,\"scrpe\":%d,\"scrpt\":%d,\"scrpb\":%d,\"sa\":%d}", 
                                  config.store.flipscreen, 
                                  config.store.invertdisplay, 
                                  config.store.numplaylist, 
                                  config.store.fliptouch, 
                                  config.store.dbgtouch, 
                                  config.store.dspon, 
                                  config.store.brightness, 
                                  config.store.contrast,
                                  config.store.screensaverEnabled,
                                  config.store.screensaverTimeout,
                                  config.store.screensaverBlank,
                                  config.store.screensaverPlayingEnabled,
                                  config.store.screensaverPlayingTimeout,
                                  config.store.screensaverPlayingBlank,
                                  config.store.usespectrum);
                                  break;
      case GETTIMEZONE:   wsbufFormat( "{\"tzh\":%d,\"tzm\":%d,\"sntp1\":\"%s\",\"sntp2\":\"%s\"}", 
                                  config.store.tzHour, 
                                  config.store.tzMin, 
                                  config.store.sntp1, 
                                  config.store.sntp2); 
                                  break;
      case GETWEATHER:    wsbufFormat( "{\"wen\":%d,\"wlat\":\"%s\",\"wlon\":\"%s\",\"wkey\":\"%s\"}", 
                                  config.store.showweather, 
                                  config.store.weatherlat, 
                                  config.store.weatherlon, 
                                  config.store.weatherkey); 
                                  break;
      case GETAI: {
                                  AIConfig aicfg;
                                  aiLoadFromFS(aicfg);
                                  // Use store.ai_enabled instead of aicfg.enabled to reflect validated state / Используем store.ai_enabled вместо aicfg.enabled для отражения валидированного состояния
                                  // (store.ai_enabled may be false if prompt is missing, even if file says enabled=true) / (store.ai_enabled может быть false если промпт отсутствует, даже если в файле enabled=true)
                                  // Simple JSON escaping: replace " with \", \ with \\ / Простое экранирование JSON
                                  String host_esc = String(aicfg.host);
                                  host_esc.replace("\\", "\\\\");
                                  host_esc.replace("\"", "\\\"");
                                  String path_esc = String(aicfg.path);
                                  path_esc.replace("\\", "\\\\");
                                  path_esc.replace("\"", "\\\"");
                                  String key_esc = String(aicfg.api_key);
                                  key_esc.replace("\\", "\\\\");
                                  key_esc.replace("\"", "\\\"");
                                  String model_esc = String(aicfg.model);
                                  model_esc.replace("\\", "\\\\");
                                  model_esc.replace("\"", "\\\"");
                                  // Get prompt status / Получить статус промпта
                                  extern bool aiPromptIsAvailable();
                                  extern size_t aiPromptGetSize();
                                  bool prompt_loaded = aiPromptIsAvailable();
                                  size_t prompt_size = prompt_loaded ? aiPromptGetSize() : 0;
                                  wsbufFormat( "{\"aien\":%d,\"aihost\":\"%s\",\"aiport\":%d,\"aipath\":\"%s\",\"aitimeout\":%lu,\"aimodel\":\"%s\",\"aikey\":\"%s\",\"aiprompt_loaded\":%d,\"aiprompt_size\":%u}", 
                                  config.store.ai_enabled ? 1 : 0, 
                                  host_esc.c_str(),
                                  aicfg.port,
                                  path_esc.c_str(),
                                  aicfg.timeout_ms,
                                  model_esc.c_str(),
                                  key_esc.c_str(),
                                  prompt_loaded ? 1 : 0,
                                  prompt_size); 
                                  break;
                                }
      case GETCONTROLS:   wsbufFormat( "{\"vols\":%d,\"enca\":%d,\"irtl\":%d,\"skipup\":%d}", 
                                  config.store.volsteps, 
                                  config.store.encacc, 
                                  config.store.irtlp,
                                  config.store.skipPlaylistUpDown); 
                                  break;
      case DSPON:         wsbufFormat( "{\"dspontrue\":%d}", 1); break;
      case STATION:       requestOnChange(STATIONNAME, clientId); requestOnChange(ITEM, clientId); break;
      case STATIONNAME:   wsbufFormat( "{\"nameset\": \"%s\"}", config.station.name); break;
      case ITEM:          wsbufFormat( "{\"current\": %d}", config.lastStation()); break;
      case TITLE:         wsbufFormat( "{\"meta\": \"%s\"}", config.station.title); telnet.printf("##CLI.META#: %s\n> ", config.station.title); break;
      case PLAYER_ERROR:  wsbufFormat( "{\"player_error\": \"%s\"}", player.lastError()); break;
      case VOLUME:        wsbufFormat( "{\"vol\": %d}", config.store.volume); telnet.printf("##CLI.VOL#: %d\n", config.store.volume); break;
      case NRSSI:         wsbufFormat( "{\"rssi\": %d}", rssi); /*rssi = 255;*/ break;
      case SDPOS:         wsbufFormat( "{\"sdpos\": %d,\"sdend\": %d,\"sdtpos\": %d,\"sdtend\": %d}", 
                                  player.getFilePos(), 
                                  player.getFileSize(), 
                                  player.getAudioCurrentTime(), 
                                  player.getAudioFileDuration()); 
                                  break;
      case SDLEN:         wsbufFormat( "{\"sdmin\": %d,\"sdmax\": %d}", player.sd_min, player.sd_max); break;
      case SDSNUFFLE:     wsbufFormat( "{\"snuffle\": %d}", config.store.sdsnuffle); break;
      case BITRATE:       wsbufFormat( "{\"bitrate\": %d, \"format\": \"%s\"}", config.station.bitrate, getFormat(config.configFmt)); break;
      case MODE:          wsbufFormat( "{\"mode\": \"%s\"}", player.status() == PLAYING ? "playing" : "stopped"); telnet.info(); break;
      case EQUALIZER:     wsbufFormat( "{\"bass\": %d, \"middle\": %d, \"trebble\": %d}", config.store.bass, config.store.middle, config.store.trebble); break;
      case BALANCE:       wsbufFormat( "{\"balance\": %d}", config.store.balance); break;
      case SDINIT:        wsbufFormat( "{\"sdinit\": %d}", SDC_CS!=255); break;
      case GETPLAYERMODE: wsbufFormat( "{\"playermode\": \"%s\"}", config.getMode()==PM_SDCARD?"modesd":"modeweb"); break;
      #ifdef USE_SD
        case CHANGEMODE:    config.changeMode(newConfigMode); return; break;
      #endif
      default:          break;
    }
    if (strlen(wsbuf) > 0) {
      // S6V10A: safe helpers skip send when no clients or client gone / safe helpers пропускают отправку если нет клиентов
      if (clientId == 0) { safeWsTextAll(wsbuf); }else{ safeWsTextClient(clientId, wsbuf); }
  #ifdef MQTT_ROOT_TOPIC
      if (clientId == 0 && (request.type == STATION || request.type == ITEM || request.type == TITLE || request.type == PLAYER_ERROR || request.type == MODE)) mqttPublishStatus();
      if (clientId == 0 && request.type == VOLUME) mqttPublishVolume();
  #endif
    }
  }
}

void NetServer::loop() {
  if(network.status==SDREADY) return;
  if (shouldReboot) {
    Serial.println("Rebooting...");
    delay(100);
    sm::systemRestart();
  }
  websocket.cleanupClients();
  switch (importRequest) {
    case IMPL:    importPlaylist();  importRequest = IMDONE; break;
    case IMWIFI:  config.saveWifi(); importRequest = IMDONE; break;
    default:      break;
  }
  //if (rssi < 255) requestOnChange(NRSSI, 0);
  processQueue();
}

#if IR_PIN!=255
void NetServer::irToWs(const char* protocol, uint64_t irvalue) {
  char buf[BUFLEN] = { 0 };
  sprintf (buf, "{\"ircode\": %llu, \"protocol\": \"%s\"}", irvalue, protocol);
  websocket.textAll(buf);
}
void NetServer::irValsToWs() {
  if (!irRecordEnable) return;
  char buf[BUFLEN] = { 0 };
  sprintf (buf, "{\"irvals\": [%llu, %llu, %llu]}", config.ircodes.irVals[config.irindex][0], config.ircodes.irVals[config.irindex][1], config.ircodes.irVals[config.irindex][2]);
  websocket.textAll(buf);
}
#endif

void NetServer::onWsMessage(void *arg, uint8_t *data, size_t len, uint8_t clientId) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    char cmd[65], val[65];
    if (config.parseWsCommand((const char*)data, cmd, val, 65)) {
      if (strcmp(cmd, "getmode") == 0     ) { requestOnChange(GETMODE, clientId);     return; }
      if (strcmp(cmd, "getindex") == 0    ) { requestOnChange(GETINDEX, clientId);    return; }
      if (strcmp(cmd, "getsystem") == 0   ) { requestOnChange(GETSYSTEM, clientId);   return; }
      if (strcmp(cmd, "getscreen") == 0   ) { requestOnChange(GETSCREEN, clientId);   return; }
      if (strcmp(cmd, "gettimezone") == 0 ) { requestOnChange(GETTIMEZONE, clientId); return; }
      if (strcmp(cmd, "getcontrols") == 0 ) { requestOnChange(GETCONTROLS, clientId); return; }
      if (strcmp(cmd, "getweather") == 0  ) { requestOnChange(GETWEATHER, clientId);  return; }
      if (strcmp(cmd, "getai") == 0       ) { requestOnChange(GETAI, clientId);       return; }
      if (strcmp(cmd, "getactive") == 0   ) { requestOnChange(GETACTIVE, clientId);   return; }
      if (strcmp(cmd, "newmode") == 0     ) { newConfigMode = atoi(val); requestOnChange(CHANGEMODE, 0); return; }
      if (strcmp(cmd, "smartstart") == 0) {
        uint8_t valb = atoi(val);
        uint8_t ss = valb == 1 ? 1 : 2;
        if (!player.isRunning() && ss == 1) ss = 0;
        config.setSmartStart(ss);
        return;
      }
      if (strcmp(cmd, "audioinfo") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.audioinfo, valb);
        return;
      }
      if (strcmp(cmd, "vumeter") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.vumeter, valb);
        // Persist only — LVGL VU widget not wired yet (Block 8.1F-B).
        return;
      }
      if (strcmp(cmd, "usespectrum") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.usespectrum, valb);
        // Persist only — LVGL spectrum widget not wired yet (Block 8.1F-B).
        return;
      }
      if (strcmp(cmd, "softap") == 0) {
        uint8_t valb = atoi(val);
        config.saveValue(&config.store.softapdelay, valb);
        return;
      }
      if (strcmp(cmd, "mdnsname") == 0) {
        config.saveValue(config.store.mdnsname, val, MDNS_LENGTH);
        return;
      }
      if (strcmp(cmd, "rebootmdns") == 0) {
        char buf[MDNS_LENGTH*2];
        if(strlen(config.store.mdnsname)>0)
          snprintf(buf, MDNS_LENGTH*2, "{\"redirect\": \"http://%s.local\"}", config.store.mdnsname);
        else
          snprintf(buf, MDNS_LENGTH*2, "{\"redirect\": \"http://%s/\"}", WiFi.localIP().toString().c_str());
        websocket.text(clientId, buf);
        delay(500);
        ESP.restart();
        return;
      }
      if (strcmp(cmd, "invertdisplay") == 0) {
        // Wi-Fi 3B: dev/test entry — same WebUI/API cmd "invertdisplay", opens LVGL Wi-Fi shell; no invert, no NVS write (DspTask via queue).
        // Wi‑Fi 3B: временный вход в Wi‑Fi shell через тот же invertdisplay; без invert и без сохранения в store.
        (void)val;
        display.putRequest(NEWMODE, WIFI);
        return;
      }
      if (strcmp(cmd, "numplaylist") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.numplaylist, valb);
        display.putRequest(REFRESH_MAIN);
        return;
      }
      if (strcmp(cmd, "fliptouch") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.fliptouch, valb);
        flipTS();
        return;
      }
      if (strcmp(cmd, "dbgtouch") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.dbgtouch, valb);
        return;
      }
      if (strcmp(cmd, "flipscreen") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.flipscreen, valb);
        display.flip();
        // 8.1H-I-B corrective: payload 1 = force full redraw after panel orientation flip
        // (LVGL keeps old pixels otherwise). / payload 1 = принудительная полная перерисовка после flip.
        display.putRequest(REFRESH_MAIN, 1);
        return;
      }
      if (strcmp(cmd, "brightness") == 0) {
        uint8_t valb = atoi(val);
        if (!config.store.dspon) requestOnChange(DSPON, 0);
        config.store.brightness = valb;
        config.setBrightness(true);
        return;
      }
      if (strcmp(cmd, "screenon") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.setDspOn(valb);
        return;
      }
      if (strcmp(cmd, "contrast") == 0) {
        uint8_t valb = atoi(val);
        config.saveValue(&config.store.contrast, valb);
        display.setContrast();
        return;
      }
      if (strcmp(cmd, "screensaverenabled") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.screensaverEnabled, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensavertimeout") == 0) {
        uint16_t valb = atoi(val);
        valb = constrain(valb,5,65520);
        config.saveValue(&config.store.screensaverTimeout, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensaverblank") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.screensaverBlank, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensaverplayingenabled") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.screensaverPlayingEnabled, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensaverplayingtimeout") == 0) {
        uint16_t valb = atoi(val);
        valb = constrain(valb,5,65520);
        config.saveValue(&config.store.screensaverPlayingTimeout, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensaverplayingblank") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.screensaverPlayingBlank, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "tzh") == 0) {
        int8_t vali = atoi(val);
        config.saveValue(&config.store.tzHour, vali);
        return;
      }
      if (strcmp(cmd, "tzm") == 0) {
        int8_t vali = atoi(val);
        config.saveValue(&config.store.tzMin, vali);
        return;
      }
      if (strcmp(cmd, "sntp2") == 0) {
        config.saveValue(config.store.sntp2, val, 35, false);
        return;
      }
      if (strcmp(cmd, "sntp1") == 0) {
        config.saveValue(config.store.sntp1, val, 35);
        bool tzdone = false;
        if (strlen(config.store.sntp1) > 0 && strlen(config.store.sntp2) > 0) {
          configTime(config.store.tzHour * 3600 + config.store.tzMin * 60, config.getTimezoneOffset(), config.store.sntp1, config.store.sntp2);
          tzdone = true;
        } else if (strlen(config.store.sntp1) > 0) {
          configTime(config.store.tzHour * 3600 + config.store.tzMin * 60, config.getTimezoneOffset(), config.store.sntp1);
          tzdone = true;
        }
        if (tzdone) {
          network.forceTimeSync = true;
        }
        return;
      }
      if (strcmp(cmd, "volsteps") == 0) {
        uint8_t valb = atoi(val);
        config.saveValue(&config.store.volsteps, valb);
        return;
      }
      if (strcmp(cmd, "encacceleration") == 0) {
        uint16_t valb = atoi(val);
        setEncAcceleration(valb);
        config.saveValue(&config.store.encacc, valb);
        return;
      }
      if (strcmp(cmd, "irtlp") == 0) {
        uint8_t valb = atoi(val);
        setIRTolerance(valb);
        return;
      }
      if (strcmp(cmd, "oneclickswitching") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.skipPlaylistUpDown, valb);
        return;
      }
      if (strcmp(cmd, "showweather") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.showweather, valb);
        network.forceWeather = true;
        return;
      }
      if (strcmp(cmd, "lat") == 0) {
        config.saveValue(config.store.weatherlat, val, 10, false);
#if YORADIO_WEATHER_REQ_DIAG
        Serial.printf("[WEATHER_CFG] save field=lat value=\"%s\"\n", val);
        Serial.printf("[WEATHER_CFG] stored lat=\"%s\" lon=\"%s\"\n",
                      config.store.weatherlat, config.store.weatherlon);
#endif
        return;
      }
      if (strcmp(cmd, "lon") == 0) {
        config.saveValue(config.store.weatherlon, val, 10, false);
#if YORADIO_WEATHER_REQ_DIAG
        Serial.printf("[WEATHER_CFG] save field=lon value=\"%s\"\n", val);
        Serial.printf("[WEATHER_CFG] stored lat=\"%s\" lon=\"%s\"\n",
                      config.store.weatherlat, config.store.weatherlon);
#endif
        return;
      }
      if (strcmp(cmd, "key") == 0) {
        config.saveValue(config.store.weatherkey, val, WEATHERKEY_LENGTH);
        display.putRequest(REFRESH_MAIN);
        return;
      }
      // W-R1C: final Apply signal — one refresh after lat/lon/key saved; no HTTP here.
      // W-R1C: финальный сигнал Apply — один refresh после сохранения lat/lon/key; HTTP не здесь.
      if (strcmp(cmd, "weatherapply") == 0) {
        // Guard: no fetch arm without a persisted API key (coords-only Apply is rejected in WebUI).
        // Защита: не arm-ить fetch без сохранённого API-ключа.
        if (config.store.showweather && strlen(config.store.weatherkey) > 0) {
          network.forceWeatherRefreshFromUi();
        }
#if YORADIO_WEATHER_REQ_DIAG
        Serial.printf("[WEATHER_CFG] apply refresh_requested=%d lat=\"%s\" lon=\"%s\"\n",
                      (config.store.showweather && strlen(config.store.weatherkey) > 0) ? 1 : 0,
                      config.store.weatherlat, config.store.weatherlon);
#endif
        return;
      }
      // AI settings commands / Команды настроек AI
      if (strcmp(cmd, "ai_enabled") == 0) {
        AIConfig aicfg;
        aiLoadFromFS(aicfg);
        bool old_enabled = aicfg.enabled;  // Сохраняем старое состояние из FS / Save old state from FS
        bool old_store_enabled = config.store.ai_enabled;  // Сохраняем старое состояние из store / Save old state from store
        aicfg.enabled = (atoi(val) == 1);
        
        // СТРОГАЯ ПРОВЕРКА: при попытке включить проверяем промпт / STRICT CHECK: when trying to enable, check prompt
        extern bool aiPromptIsAvailable();
        if (aicfg.enabled) {
          if (!aiPromptIsAvailable()) {
            // Промпт недоступен - принудительно выключаем / Prompt unavailable - force disable
            aicfg.enabled = false;
            AI_LOG("[AI] Enable rejected: prompt missing (/ai/ai_prompt.txt)");
          } else if (!aiIsValidForEnable(aicfg)) {
            // Другие параметры невалидны / Other parameters invalid
            aicfg.enabled = false;
          }
        }
        
        aiSaveToFS(aicfg);
        // Notify AISubsystem BEFORE applying to store / Уведомляем AISubsystem ДО применения к store
        // (чтобы функция могла сравнить с текущим состоянием / so function can compare with current state)
        if (old_store_enabled != aicfg.enabled) {
          aiSubsystem.onEnabledChanged(aicfg.enabled);
        }
        aiApplyToStore(aicfg);
        return;
      }
      if (strcmp(cmd, "ai_host") == 0) {
        AIConfig aicfg;
        aiLoadFromFS(aicfg);
        strlcpy(aicfg.host, val, sizeof(aicfg.host));
        // Revalidate enabled state / Перепроверка состояния enabled
        bool was_enabled = aicfg.enabled;
        bool old_store_enabled = config.store.ai_enabled;  // Сохраняем старое состояние из store / Save old state from store
        if (aicfg.enabled && !aiIsValidForEnable(aicfg)) {
          aicfg.enabled = false;
        }
        aiSaveToFS(aicfg);
        // Notify AISubsystem BEFORE applying to store if enabled state changed / Уведомляем AISubsystem ДО применения к store если состояние enabled изменилось
        if (was_enabled != aicfg.enabled || old_store_enabled != aicfg.enabled) {
          aiSubsystem.onEnabledChanged(aicfg.enabled);
        }
        aiApplyToStore(aicfg);
        return;
      }
      if (strcmp(cmd, "ai_port") == 0) {
        AIConfig aicfg;
        aiLoadFromFS(aicfg);
        uint16_t port_val = atoi(val);
        if (port_val > 0 && port_val <= 65535) {
          aicfg.port = port_val;
        }
        // Revalidate enabled state / Перепроверка состояния enabled
        bool was_enabled = aicfg.enabled;
        bool old_store_enabled = config.store.ai_enabled;  // Сохраняем старое состояние из store / Save old state from store
        if (aicfg.enabled && !aiIsValidForEnable(aicfg)) {
          aicfg.enabled = false;
        }
        aiSaveToFS(aicfg);
        // Notify AISubsystem BEFORE applying to store if enabled state changed / Уведомляем AISubsystem ДО применения к store если состояние enabled изменилось
        if (was_enabled != aicfg.enabled || old_store_enabled != aicfg.enabled) {
          aiSubsystem.onEnabledChanged(aicfg.enabled);
        }
        aiApplyToStore(aicfg);
        return;
      }
      if (strcmp(cmd, "ai_path") == 0) {
        AIConfig aicfg;
        aiLoadFromFS(aicfg);
        strlcpy(aicfg.path, val, sizeof(aicfg.path));
        // Revalidate enabled state / Перепроверка состояния enabled
        bool was_enabled = aicfg.enabled;
        bool old_store_enabled = config.store.ai_enabled;  // Сохраняем старое состояние из store / Save old state from store
        if (aicfg.enabled && !aiIsValidForEnable(aicfg)) {
          aicfg.enabled = false;
        }
        aiSaveToFS(aicfg);
        // Notify AISubsystem BEFORE applying to store if enabled state changed / Уведомляем AISubsystem ДО применения к store если состояние enabled изменилось
        if (was_enabled != aicfg.enabled || old_store_enabled != aicfg.enabled) {
          aiSubsystem.onEnabledChanged(aicfg.enabled);
        }
        aiApplyToStore(aicfg);
        return;
      }
      if (strcmp(cmd, "ai_timeout") == 0) {
        AIConfig aicfg;
        aiLoadFromFS(aicfg);
        uint32_t timeout_val = atoi(val);
        if (timeout_val >= 1000 && timeout_val <= 60000) {
          aicfg.timeout_ms = timeout_val;
        }
        aiSaveToFS(aicfg);
        aiApplyToStore(aicfg);
        return;
      }
      if (strcmp(cmd, "ai_model") == 0) {
        AIConfig aicfg;
        aiLoadFromFS(aicfg);
        strlcpy(aicfg.model, val, sizeof(aicfg.model));
        // Revalidate enabled state / Перепроверка состояния enabled
        bool was_enabled = aicfg.enabled;
        bool old_store_enabled = config.store.ai_enabled;  // Сохраняем старое состояние из store / Save old state from store
        if (aicfg.enabled && !aiIsValidForEnable(aicfg)) {
          aicfg.enabled = false;
        }
        aiSaveToFS(aicfg);
        // Notify AISubsystem BEFORE applying to store if enabled state changed / Уведомляем AISubsystem ДО применения к store если состояние enabled изменилось
        if (was_enabled != aicfg.enabled || old_store_enabled != aicfg.enabled) {
          aiSubsystem.onEnabledChanged(aicfg.enabled);
        }
        aiApplyToStore(aicfg);
        return;
      }
      if (strcmp(cmd, "ai_key") == 0) {
        AIConfig aicfg;
        aiLoadFromFS(aicfg);
        strlcpy(aicfg.api_key, val, sizeof(aicfg.api_key));
        // Revalidate enabled state / Перепроверка состояния enabled
        bool was_enabled = aicfg.enabled;
        bool old_store_enabled = config.store.ai_enabled;  // Сохраняем старое состояние из store / Save old state from store
        if (aicfg.enabled && !aiIsValidForEnable(aicfg)) {
          aicfg.enabled = false;
        }
        aiSaveToFS(aicfg);
        // Notify AISubsystem BEFORE applying to store if enabled state changed / Уведомляем AISubsystem ДО применения к store если состояние enabled изменилось
        if (was_enabled != aicfg.enabled || old_store_enabled != aicfg.enabled) {
          aiSubsystem.onEnabledChanged(aicfg.enabled);
        }
        aiApplyToStore(aicfg);
        return;
      }
      /*  RESETS  */
      if (strcmp(cmd, "reset") == 0) {
        if (strcmp(val, "system") == 0) {
          config.saveValue(&config.store.smartstart, (uint8_t)2, false);
          config.saveValue(&config.store.audioinfo, false, false);
          config.saveValue(&config.store.vumeter, false, false);
          config.saveValue(&config.store.softapdelay, (uint8_t)0, false);
          snprintf(config.store.mdnsname, MDNS_LENGTH, "yoradio-%x", config.getChipId());
          config.saveValue(config.store.mdnsname, config.store.mdnsname, MDNS_LENGTH, true, true);
          display.putRequest(REFRESH_MAIN);
          requestOnChange(GETSYSTEM, clientId);
          return;
        }
        if (strcmp(val, "screen") == 0) {
          config.saveValue(&config.store.flipscreen, false, false);
          display.flip();
          config.saveValue(&config.store.invertdisplay, false, false);
          display.invert();
          config.saveValue(&config.store.dspon, true, false);
          config.store.brightness = 100;
          config.setBrightness(false);
          config.saveValue(&config.store.contrast, (uint8_t)55, false);
          display.setContrast();
          config.saveValue(&config.store.numplaylist, false);
          config.saveValue(&config.store.screensaverEnabled, false);
          config.saveValue(&config.store.screensaverTimeout, (uint16_t)20);
          config.saveValue(&config.store.screensaverBlank, false);
          config.saveValue(&config.store.screensaverPlayingEnabled, false);
          config.saveValue(&config.store.screensaverPlayingTimeout, (uint16_t)20);
          config.saveValue(&config.store.screensaverPlayingBlank, false);
          display.putRequest(REFRESH_MAIN);
          requestOnChange(GETSCREEN, clientId);
          return;
        }
        if (strcmp(val, "timezone") == 0) {
          config.saveValue(&config.store.tzHour, (int8_t)3, false);
          config.saveValue(&config.store.tzMin, (int8_t)0, false);
          config.saveValue(config.store.sntp1, "2.ru.pool.ntp.org", 35, false);
          config.saveValue(config.store.sntp2, "1.ru.pool.ntp.org", 35);
          configTime(config.store.tzHour * 3600 + config.store.tzMin * 60, config.getTimezoneOffset(), config.store.sntp1, config.store.sntp2);
          network.forceTimeSync = true;
          requestOnChange(GETTIMEZONE, clientId);
          return;
        }
        if (strcmp(val, "weather") == 0) {
          config.saveValue(&config.store.showweather, false, false);
          config.saveValue(config.store.weatherlat, "55.7512", 10, false);
          config.saveValue(config.store.weatherlon, "37.6184", 10, false);
          config.saveValue(config.store.weatherkey, "", WEATHERKEY_LENGTH);
          display.putRequest(REFRESH_MAIN);
          requestOnChange(GETWEATHER, clientId);
          return;
        }
        if (strcmp(val, "ai") == 0) {
          // Reset AI config: delete file or write defaults / Сброс AI конфигурации: удалить файл или записать дефолты
          bool old_store_enabled = config.store.ai_enabled;  // Сохраняем старое состояние из store / Save old state from store
          AIConfig aicfg;
          aiSetDefaults(aicfg);
          aicfg.enabled = false;  // Принудительно выключаем AI / Force disable AI
          aiSaveToFS(aicfg);
          
          // Reset prompt cache after reset / Сбросить кеш промптов после сброса
          extern void aiPromptResetCache();
          aiPromptResetCache();
          
          // Notify AISubsystem BEFORE applying to store if state changed / Уведомляем AISubsystem ДО применения к store если состояние изменилось
          if (old_store_enabled != false) {
            aiSubsystem.onEnabledChanged(false);
          }
          aiApplyToStore(aicfg);
          requestOnChange(GETAI, clientId);
          return;
        }
        if (strcmp(val, "controls") == 0) {
          config.saveValue(&config.store.volsteps, (uint8_t)1, false);
          config.saveValue(&config.store.fliptouch, false, false);
          config.saveValue(&config.store.dbgtouch, false, false);
          config.saveValue(&config.store.skipPlaylistUpDown, false);
          
          setEncAcceleration(200);
          setIRTolerance(40);
          requestOnChange(GETCONTROLS, clientId);
          return;
        }
      } /*  EOF RESETS  */
      if (strcmp(cmd, "volume") == 0) {
        uint8_t v = atoi(val);
        player.setVol(v);
      }
      if (strcmp(cmd, "sdpos") == 0) {
        //return;
        if (config.getMode()==PM_SDCARD){
          config.sdResumePos = 0;
          if(!player.isRunning()){
            player.setResumeFilePos(atoi(val)-player.sd_min);
            player.sendCommand({PR_PLAY, config.store.lastSdStation});
          }else{
            player.setFilePos(atoi(val)-player.sd_min);
          }
        }
        return;
      }
      if (strcmp(cmd, "snuffle") == 0) {
        config.setSnuffle(strcmp(val, "true") == 0);
        return;
      }
      if (strcmp(cmd, "balance") == 0) {
        int8_t valb = atoi(val);
        player.setBalance(valb);
        config.setBalance(valb);
        netserver.requestOnChange(BALANCE, 0);
        return;
      }
      if (strcmp(cmd, "treble") == 0) {
        int8_t valb = atoi(val);
        player.setTone(config.store.bass, config.store.middle, valb);
        config.setTone(config.store.bass, config.store.middle, valb);
        netserver.requestOnChange(EQUALIZER, 0);
        return;
      }
      if (strcmp(cmd, "middle") == 0) {
        int8_t valb = atoi(val);
        player.setTone(config.store.bass, valb, config.store.trebble);
        config.setTone(config.store.bass, valb, config.store.trebble);
        netserver.requestOnChange(EQUALIZER, 0);
        return;
      }
      if (strcmp(cmd, "bass") == 0) {
        int8_t valb = atoi(val);
        player.setTone(valb, config.store.middle, config.store.trebble);
        config.setTone(valb, config.store.middle, config.store.trebble);
        netserver.requestOnChange(EQUALIZER, 0);
        return;
      }
      if (strcmp(cmd, "submitplaylist") == 0) {
        return;
      }
      if (strcmp(cmd, "submitplaylistdone") == 0) {
#ifdef MQTT_ROOT_TOPIC
        //mqttPublishPlaylist();
        mqttplaylistticker.attach(5, mqttplaylistSend);
#endif
        if (player.isRunning()) {
          player.sendCommand({PR_PLAY, -config.lastStation()});
        }
        return;
      }
#if IR_PIN!=255
      if (strcmp(cmd, "irbtn") == 0) {
        config.irindex = atoi(val);
        irRecordEnable = (config.irindex >= 0);
        config.irchck = 0;
        irValsToWs();
        if (config.irindex < 0) config.saveIR();
      }
      if (strcmp(cmd, "chkid") == 0) {
        config.irchck = atoi(val);
      }
      if (strcmp(cmd, "irclr") == 0) {
        uint8_t cl = atoi(val);
        config.ircodes.irVals[config.irindex][cl] = 0;
      }
#endif
    }
  }
}

void NetServer::getPlaylist(uint8_t clientId) {
  char buf[160] = {0};
  sprintf(buf, "{\"file\": \"http://%s%s\"}", WiFi.localIP().toString().c_str(), PLAYLIST_PATH);
  if (clientId == 0) { websocket.textAll(buf); } else { websocket.text(clientId, buf); }
}

uint8_t NetServer::_readPlaylistLine(File &file, char * line, size_t size){
  int bytesRead = file.readBytesUntil('\n', line, size);
  if(bytesRead>0){
    line[bytesRead] = 0;
    if(line[bytesRead-1]=='\r') line[bytesRead-1]=0;
  }
  return bytesRead;
}

bool NetServer::importPlaylist() {
  if(config.getMode()==PM_SDCARD) return false;
  File tempfile = LittleFS.open(TMP_PATH, "r");
  if (!tempfile) {
    return false;
  }
  char sName[BUFLEN], sUrl[BUFLEN], linePl[BUFLEN*3];;
  int sOvol;
  _readPlaylistLine(tempfile, linePl, sizeof(linePl)-1);
  if (config.parseCSV(linePl, sName, sUrl, sOvol)) {
    tempfile.close();
    LittleFS.rename(TMP_PATH, PLAYLIST_PATH);
    requestOnChange(PLAYLISTSAVED, 0);
    return true;
  }
  if (config.parseJSON(linePl, sName, sUrl, sOvol)) {
    File playlistfile = LittleFS.open(PLAYLIST_PATH, "w");
    snprintf(linePl, sizeof(linePl)-1, "%s\t%s\t%d", sName, sUrl, 0);
    playlistfile.println(linePl);
    while (tempfile.available()) {
      _readPlaylistLine(tempfile, linePl, sizeof(linePl)-1);
      if (config.parseJSON(linePl, sName, sUrl, sOvol)) {
        snprintf(linePl, sizeof(linePl)-1, "%s\t%s\t%d", sName, sUrl, 0);
        playlistfile.println(linePl);
      }
    }
    playlistfile.flush();
    playlistfile.close();
    tempfile.close();
    LittleFS.remove(TMP_PATH);
    requestOnChange(PLAYLISTSAVED, 0);
    return true;
  }
  tempfile.close();
  LittleFS.remove(TMP_PATH);
  return false;
}

void NetServer::requestOnChange(requestType_e request, uint8_t clientId) {
  if(nsQueue==NULL) return;
  // 8.1HX-B: coalesce only broadcast (clientId==0) requests. Per-client replies
  // (clientId!=0) are never deduped — every targeted reply must reach its client.
  // 8.1HX-B: коалесим только broadcast (clientId==0). Per-client ответы (clientId!=0)
  // не дедуплицируем — каждый адресный ответ должен дойти до клиента.
  const bool coalescable = (clientId == 0) && ((uint8_t)request < NS_BROADCAST_PENDING_COUNT);
  if (coalescable) {
    bool alreadyPending;
    portENTER_CRITICAL(&_pendingMux);
    alreadyPending = _broadcastPending[(uint8_t)request];
    if (!alreadyPending) _broadcastPending[(uint8_t)request] = true;
    portEXIT_CRITICAL(&_pendingMux);
    if (alreadyPending) return; // identical broadcast already queued — skip duplicate
  }
  nsRequestParams_t nsrequest;
  nsrequest.type = request;
  nsrequest.clientId = clientId;
  if (xQueueSend(nsQueue, &nsrequest, NSQ_SEND_DELAY) != pdTRUE) {
    // Enqueue failed (queue full): clear pending so the next change can re-queue.
    // Постановка не удалась (очередь полна): снимаем флаг, чтобы следующее изменение встало в очередь.
    if (coalescable) {
      portENTER_CRITICAL(&_pendingMux);
      _broadcastPending[(uint8_t)request] = false;
      portEXIT_CRITICAL(&_pendingMux);
    }
  }
}

void NetServer::resetQueue(){
  if(nsQueue!=NULL) xQueueReset(nsQueue);
  // 8.1HX-B: clear coalescing flags alongside the queue reset. / Сбрасываем флаги вместе с очередью.
  portENTER_CRITICAL(&_pendingMux);
  for (uint8_t i = 0; i < NS_BROADCAST_PENDING_COUNT; ++i) _broadcastPending[i] = false;
  portEXIT_CRITICAL(&_pendingMux);
}

// ---------------------------------------------------------------------------
// Stage 6.1F-d: Main background slots — POST /upload_bg, GET /bg_status
// Слоты фона Main: загрузка .bin в /bg/main_*.bin, JSON статус для WebUI
// ---------------------------------------------------------------------------
namespace {

static const char kBgTmpPath[] = "/bg/.upload_bg.tmp";

// LVGL 8.x lv_img_header_t (4 bytes LE) / Заголовок изображения LVGL 8.x
static bool bgParseImgHeader(const uint8_t* b, uint8_t* outCf, uint16_t* outW, uint16_t* outH) {
  uint32_t v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  uint8_t az = (uint8_t)((v >> 5) & 7u);
  if (az != 0) {
    return false;
  }
  *outCf = (uint8_t)(v & 0x1Fu);
  *outW = (uint16_t)((v >> 10) & 0x7FFu);
  *outH = (uint16_t)((v >> 21) & 0x7FFu);
  return true;
}

// LV_IMG_CF_TRUE_COLOR — LVGL 8.x color format for RGB565 image data
static constexpr uint8_t kLvImgCfTrueColor = 4;

static bool gBgUploadArmed = false;
static char gBgDestPath[40] = {0};
static char gBgSlotName[12] = {0};  // dark | light | custom / имя слота для JSON ответа
static size_t gBgExpectedSize = 0;
static size_t gBgWrittenTotal = 0;  // bytes written to temp / записано в .tmp (диагностика)
static bool gBgIoFatal = false;
static File gBgUploadFile;

// Validate ?slot= + FS space; fill gBgDestPath / gBgExpectedSize / gBgSlotName. / Подготовка слота до записи чанков
static bool bgPrepareSlotForUpload(AsyncWebServerRequest* request) {
  memset(gBgDestPath, 0, sizeof(gBgDestPath));
  memset(gBgSlotName, 0, sizeof(gBgSlotName));
  if (!request->hasParam("slot")) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_slot\"}");
    return false;
  }
  String slot = request->getParam("slot")->value();
  if (slot == "dark") {
    strlcpy(gBgDestPath, "/bg/main_dark.bin", sizeof(gBgDestPath));
  } else if (slot == "light") {
    strlcpy(gBgDestPath, "/bg/main_light.bin", sizeof(gBgDestPath));
  } else if (slot == "custom") {
    strlcpy(gBgDestPath, "/bg/main_custom.bin", sizeof(gBgDestPath));
  } else {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_slot\"}");
    return false;
  }
  strlcpy(gBgSlotName, slot.c_str(), sizeof(gBgSlotName));
  uint32_t w = LV_ACTIVE_PROFILE.width;
  uint32_t h = LV_ACTIVE_PROFILE.height;
  gBgExpectedSize = 4u + (size_t)w * (size_t)h * 2u;
  size_t freeB = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (freeB < gBgExpectedSize + 4096u) {
    request->send(507, "application/json", "{\"ok\":false,\"error\":\"insufficient_space\"}");
    return false;
  }
  return true;
}

}  // namespace

void beginUploadBg(AsyncWebServerRequest* request) {
  (void)request;
  /* AsyncWebServer: onRequest runs after the multipart body is fully parsed — *after* handleUploadBg.
     Never send() here: a second 200 overwrote the JSON success body; WebUI saw empty 200 as "success".
     Не отвечать здесь — иначе второй ответ затирал JSON из handleUploadBg. */
}

void handleUploadBg(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
  (void)filename;

  if (index == 0) {
    gBgUploadArmed = false;
    gBgIoFatal = false;
    gBgWrittenTotal = 0;
    if (gBgUploadFile) {
      gBgUploadFile.close();
    }
    if (!bgPrepareSlotForUpload(request)) {
      return;
    }
    gBgUploadArmed = true;
    if (LittleFS.exists(kBgTmpPath)) {
      LittleFS.remove(kBgTmpPath);
    }
    gBgUploadFile = LittleFS.open(kBgTmpPath, "w");
    if (!gBgUploadFile) {
      gBgIoFatal = true;
    }
  } else {
    if (!gBgUploadArmed) {
      return;
    }
  }

  if (gBgIoFatal) {
    if (final) {
      gBgUploadArmed = false;
      if (gBgUploadFile) {
        gBgUploadFile.close();
      }
      LittleFS.remove(kBgTmpPath);
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"open_failed\"}");
    }
    return;
  }

  if (len && gBgUploadFile) {
    size_t n = gBgUploadFile.write(data, len);
    gBgWrittenTotal += n;
    if (n != len) {
      gBgIoFatal = true;
    }
  }

  if (!final) {
    return;
  }

  gBgUploadArmed = false;
  if (gBgUploadFile) {
    gBgUploadFile.close();
  }

  if (gBgIoFatal) {
    LittleFS.remove(kBgTmpPath);
    request->send(500, "application/json", "{\"ok\":false,\"error\":\"write_failed\"}");
    gBgIoFatal = false;
    return;
  }

  File vf = LittleFS.open(kBgTmpPath, "r");
  if (!vf) {
    request->send(500, "application/json", "{\"ok\":false,\"error\":\"read_failed\"}");
    return;
  }
  size_t sz = vf.size();
  if (sz != gBgExpectedSize) {
    vf.close();
    LittleFS.remove(kBgTmpPath);
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"size_mismatch\"}");
    return;
  }
  uint8_t hdr[4];
  if (vf.read(hdr, 4) != 4) {
    vf.close();
    LittleFS.remove(kBgTmpPath);
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"header_short\"}");
    return;
  }
  uint8_t cf = 0;
  uint16_t iw = 0;
  uint16_t ih = 0;
  if (!bgParseImgHeader(hdr, &cf, &iw, &ih)) {
    vf.close();
    LittleFS.remove(kBgTmpPath);
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_header\"}");
    return;
  }
  if (cf != kLvImgCfTrueColor) {
    vf.close();
    LittleFS.remove(kBgTmpPath);
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"cf_not_true_color\"}");
    return;
  }
  if (iw != LV_ACTIVE_PROFILE.width || ih != LV_ACTIVE_PROFILE.height) {
    vf.close();
    LittleFS.remove(kBgTmpPath);
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"dimensions_mismatch\"}");
    return;
  }
  vf.close();

  if (LittleFS.exists(gBgDestPath)) {
    LittleFS.remove(gBgDestPath);
  }
  if (!LittleFS.rename(kBgTmpPath, gBgDestPath)) {
    File src = LittleFS.open(kBgTmpPath, "r");
    File dst = LittleFS.open(gBgDestPath, "w");
    if (!src || !dst) {
      if (src) {
        src.close();
      }
      if (dst) {
        dst.close();
      }
      LittleFS.remove(kBgTmpPath);
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"commit_failed\"}");
      return;
    }
    uint8_t buf[512];
    while (src.available()) {
      size_t rd = src.read(buf, sizeof(buf));
      if (rd && dst.write(buf, rd) != rd) {
        src.close();
        dst.close();
        LittleFS.remove(kBgTmpPath);
        LittleFS.remove(gBgDestPath);
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"commit_copy\"}");
        return;
      }
    }
    src.close();
    dst.close();
    LittleFS.remove(kBgTmpPath);
  }
  size_t final_sz = 0;
  bool target_exists = LittleFS.exists(gBgDestPath);
  if (target_exists) {
    File committed = LittleFS.open(gBgDestPath, "r");
    if (committed) {
      final_sz = committed.size();
      committed.close();
    }
  }
  // Invalidate Main PSRAM bg when WebUI overwrote the active theme slot (DspTask reload).
  // Сброс кэша фона: только если залитый слот совпадает с активным пресетом — иначе очередь не трогаем.
  {
    uint8_t uploaded = 255;
    if (std::strcmp(gBgSlotName, "dark") == 0) {
      uploaded = 0;
    } else if (std::strcmp(gBgSlotName, "light") == 0) {
      uploaded = 1;
    } else if (std::strcmp(gBgSlotName, "custom") == 0) {
      uploaded = 2;
    }
    if (uploaded <= 2u) {
      const uint8_t active = static_cast<uint8_t>(lvgl_ui::yoradio_theme_active_preset());
      if (uploaded == active) {
        display.putRequest(MAIN_BG_FS_UPDATED, static_cast<int>(uploaded));
      }
    }
  }
  char okjson[280];
  snprintf(okjson, sizeof(okjson),
           "{\"ok\":true,\"slot\":\"%s\",\"path\":\"%s\",\"written_bytes\":%lu,\"final_size\":%lu,\"target_exists\":%s}",
           gBgSlotName[0] ? gBgSlotName : "unknown",
           gBgDestPath,
           (unsigned long)gBgWrittenTotal,
           (unsigned long)final_sz,
           target_exists ? "true" : "false");
  request->send(200, "application/json", okjson);
}

void handleRemoveBgHttp(AsyncWebServerRequest* request) {
  // Delete slot file on LittleFS; missing file → ok / Удалить bin слота; нет файла → успех
  if (!request->hasParam("slot")) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_slot\"}");
    return;
  }
  String slot = request->getParam("slot")->value();
  const char* path = nullptr;
  if (slot == "dark") {
    path = "/bg/main_dark.bin";
  } else if (slot == "light") {
    path = "/bg/main_light.bin";
  } else if (slot == "custom") {
    path = "/bg/main_custom.bin";
  } else {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_slot\"}");
    return;
  }
  if (LittleFS.exists(path)) {
    if (!LittleFS.remove(path)) {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"remove_failed\"}");
      return;
    }
  }
  request->send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// Stage 6.6R-F1: Custom theme palette file — POST /upload_theme, POST /remove_theme
// /data/theme_custom.txt (key=#RRGGBB); does not change /data/theme.dat
// ---------------------------------------------------------------------------
static const char kThemeCustomTmpPath[] = "/data/.theme_custom.tmp";
static const char kThemeCustomFinalPath[] = "/data/theme_custom.txt";
static constexpr size_t kThemeCustomUploadMax = 4096u;

static bool gThemeUploadArmed = false;
static bool gThemeIoFatal = false;
static size_t gThemeWrittenTotal = 0;
static File gThemeUploadFile;

void beginUploadTheme(AsyncWebServerRequest* request) {
  (void)request;
}

void handleUploadTheme(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
  if (index == 0) {
    gThemeUploadArmed = false;
    gThemeIoFatal = false;
    gThemeWrittenTotal = 0;
    if (gThemeUploadFile) {
      gThemeUploadFile.close();
    }
    if (filename.length() > 0 && !filename.endsWith(".txt")) {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_extension\"}");
      return;
    }
    if (LittleFS.exists(kThemeCustomTmpPath)) {
      LittleFS.remove(kThemeCustomTmpPath);
    }
    gThemeUploadFile = LittleFS.open(kThemeCustomTmpPath, "w");
    if (!gThemeUploadFile) {
      gThemeIoFatal = true;
    } else {
      gThemeUploadArmed = true;
    }
  } else if (!gThemeUploadArmed) {
    return;
  }

  if (gThemeIoFatal) {
    if (final) {
      gThemeUploadArmed = false;
      if (gThemeUploadFile) {
        gThemeUploadFile.close();
      }
      LittleFS.remove(kThemeCustomTmpPath);
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"open_failed\"}");
    }
    return;
  }

  if (len && gThemeUploadFile) {
    if (gThemeWrittenTotal + len > kThemeCustomUploadMax) {
      gThemeIoFatal = true;
    } else {
      const size_t n = gThemeUploadFile.write(data, len);
      gThemeWrittenTotal += n;
      if (n != len) {
        gThemeIoFatal = true;
      }
    }
  }

  if (!final) {
    return;
  }

  gThemeUploadArmed = false;
  if (gThemeUploadFile) {
    gThemeUploadFile.close();
  }

  if (gThemeIoFatal) {
    LittleFS.remove(kThemeCustomTmpPath);
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"write_failed_or_too_large\"}");
    gThemeIoFatal = false;
    return;
  }

  File vf = LittleFS.open(kThemeCustomTmpPath, "r");
  if (!vf) {
    LittleFS.remove(kThemeCustomTmpPath);
    request->send(500, "application/json", "{\"ok\":false,\"error\":\"read_failed\"}");
    return;
  }
  const size_t sz = vf.size();
  vf.close();
  if (sz == 0 || sz > kThemeCustomUploadMax) {
    LittleFS.remove(kThemeCustomTmpPath);
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"size_invalid\"}");
    return;
  }

  if (LittleFS.exists(kThemeCustomFinalPath)) {
    LittleFS.remove(kThemeCustomFinalPath);
  }
  if (!LittleFS.rename(kThemeCustomTmpPath, kThemeCustomFinalPath)) {
    File src = LittleFS.open(kThemeCustomTmpPath, "r");
    File dst = LittleFS.open(kThemeCustomFinalPath, "w");
    if (!src || !dst) {
      if (src) {
        src.close();
      }
      if (dst) {
        dst.close();
      }
      LittleFS.remove(kThemeCustomTmpPath);
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"commit_failed\"}");
      return;
    }
    uint8_t buf[256];
    while (src.available()) {
      const size_t rd = src.read(buf, sizeof(buf));
      if (rd && dst.write(buf, rd) != rd) {
        src.close();
        dst.close();
        LittleFS.remove(kThemeCustomTmpPath);
        LittleFS.remove(kThemeCustomFinalPath);
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"commit_copy\"}");
        return;
      }
    }
    src.close();
    dst.close();
    LittleFS.remove(kThemeCustomTmpPath);
  }

  // Stage 6.6R-F2: DspTask-only reload — NetServer does FS commit + queue (no palette parse here).
  // Этап 6.6R-F2: парсинг/палитра только в DspTask; stats — через GET /bg_status после reload.
  display.putRequest(CUSTOM_THEME_FILE_UPDATED, 0);

  char okjson[256];
  snprintf(okjson, sizeof(okjson),
           "{\"ok\":true,\"path\":\"%s\",\"written_bytes\":%lu,\"final_size\":%lu,\"reload\":\"queued\"}",
           kThemeCustomFinalPath,
           (unsigned long)gThemeWrittenTotal,
           (unsigned long)sz);
  request->send(200, "application/json", okjson);
}

void handleRemoveThemeHttp(AsyncWebServerRequest* request) {
  if (LittleFS.exists(kThemeCustomFinalPath)) {
    if (!LittleFS.remove(kThemeCustomFinalPath)) {
      request->send(500, "application/json", "{\"ok\":false,\"error\":\"remove_failed\"}");
      return;
    }
  }
  display.putRequest(CUSTOM_THEME_FILE_UPDATED, 0);
  request->send(200, "application/json",
                 "{\"ok\":true,\"custom_theme_exists\":false,\"reload\":\"queued\"}");
}

// Stage 6.6R-B: POST /set_theme?preset=dark|light|custom — enqueue runtime preset switch on DspTask.
// No persistence yet; reboot resets to Dark. LVGL APIs are never called here.
// Этап 6.6R-B: поставить смену пресета темы в очередь DspTask. Без сохранения; reboot → Dark.
void handleSetThemeHttp(AsyncWebServerRequest* request) {
  if (!request->hasParam("preset")) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing_preset\"}");
    return;
  }
  const String preset = request->getParam("preset")->value();
  uint8_t pid = 255u;
  if (preset == "dark")        pid = 0u;
  else if (preset == "light")  pid = 1u;
  else if (preset == "custom") pid = 2u;
  if (pid > 2u) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_preset\"}");
    return;
  }
  display.putRequest(SET_THEME_PRESET, static_cast<int>(pid));
  char buf[96];
  snprintf(buf, sizeof(buf),
           "{\"ok\":true,\"preset\":\"%s\",\"active_theme\":\"%s\"}",
           preset.c_str(), preset.c_str());
  request->send(200, "application/json", buf);
}

void handleBgStatusHttp(AsyncWebServerRequest* request) {
  // Main background slots on LittleFS — read-only, defensive / Слоты фона Main, только чтение
  static const char* const kBgPaths[3] = {"/bg/main_dark.bin", "/bg/main_light.bin", "/bg/main_custom.bin"};
  bool bgOk[3] = {false, false, false};
  size_t bgSz[3] = {0, 0, 0};
  for (int i = 0; i < 3; i++) {
    if (!LittleFS.exists(kBgPaths[i])) {
      continue;
    }
    File f = LittleFS.open(kBgPaths[i], "r");
    if (!f) {
      continue;
    }
    bgOk[i] = true;
    bgSz[i] = f.size();
    f.close();
  }
  const uint32_t dw = LV_ACTIVE_PROFILE.width;
  const uint32_t dh = LV_ACTIVE_PROFILE.height;
  const char* active_theme = "dark";
  switch (lvgl_ui::yoradio_theme_active_preset()) {
    case lvgl_ui::ThemePreset::Light:
      active_theme = "light";
      break;
    case lvgl_ui::ThemePreset::Custom:
      active_theme = "custom";
      break;
    default:
      active_theme = "dark";
      break;
  }
  const lvgl_ui::ThemeCustomParseStats& cst = lvgl_ui::yoradio_theme_custom_parse_stats();
  const bool custom_exists = lvgl_ui::yoradio_theme_custom_file_exists();
  // FS size for WebUI — stats may lag until DspTask parses after upload (F2 dedupe).
  // Размер с диска: applied_keys обновляется в DspTask, не в NetServer.
  uint32_t custom_fs_size = 0u;
  if (custom_exists && LittleFS.exists("/data/theme_custom.txt")) {
    File cf = LittleFS.open("/data/theme_custom.txt", "r");
    if (cf) {
      custom_fs_size = static_cast<uint32_t>(cf.size());
      cf.close();
    }
  }
  const uint32_t custom_report_size =
      (custom_fs_size > 0u) ? custom_fs_size : cst.file_size;
  char buf[768];
  snprintf(buf, sizeof(buf),
           "{\"dsp_w\":%u,\"dsp_h\":%u,"
           "\"active_theme\":\"%s\","
           "\"bg_dark\":%s,\"bg_light\":%s,\"bg_custom\":%s,"
           "\"bg_dark_size\":%lu,\"bg_light_size\":%lu,\"bg_custom_size\":%lu,"
           "\"custom_theme_exists\":%s,"
           "\"custom_theme_size\":%lu,"
           "\"custom_theme_applied_keys\":%u,"
           "\"custom_theme_invalid_lines\":%u,"
           "\"custom_theme_unknown_keys\":%u}",
           (unsigned)dw, (unsigned)dh,
           active_theme,
           bgOk[0] ? "true" : "false",
           bgOk[1] ? "true" : "false",
           bgOk[2] ? "true" : "false",
           (unsigned long)bgSz[0], (unsigned long)bgSz[1], (unsigned long)bgSz[2],
           custom_exists ? "true" : "false",
           (unsigned long)custom_report_size,
           (unsigned)cst.applied_keys,
           (unsigned)cst.invalid_lines,
           (unsigned)cst.unknown_keys);
  request->send(200, "application/json", buf);
}

// ---------------------------------------------------------------------------
// Station Art MVP: POST /upload_art, POST /remove_art, GET /art_status
// Арт станции: загрузка/удаление/статус — /logo/<normalized_playlist_name>.bin
// Key contract: stationByNum(config.lastStation()) → artNormalizeKey() — server-side only.
// ---------------------------------------------------------------------------
#include "art_key.h"
namespace {

static const char kArtTmpPath[] = "/logo/.upload_art.tmp";
static constexpr uint16_t kArtSlotW = 120;
static constexpr uint16_t kArtSlotH = 120;
// LV_IMG_CF_TRUE_COLOR_ALPHA = 5; 3 bytes per pixel (RGB565 LE + alpha)
static constexpr uint8_t kLvImgCfTrueColorAlpha = 5;
static constexpr size_t kArtExpectedSize = 4u + (size_t)kArtSlotW * kArtSlotH * 3u; // 43204

static bool   gArtUploadArmed   = false;
static char   gArtDestPath[84]  = {0};  // /logo/<key>.bin
static char   gArtNormalizedKey[68] = {0};
static size_t gArtWrittenTotal  = 0;
static bool   gArtIoFatal       = false;
static File   gArtUploadFile;

// Build current station's art path from stable playlist source.
// Строим путь к арту из стабильного плейлистного имени (не runtime ICY name).
static bool artBuildCurrentPath(char* dest_path, size_t dest_sz, char* key_out, size_t key_sz) {
    const char* playlist_name = config.stationByNum(config.lastStation());
    memset(key_out, 0, key_sz);
    artNormalizeKey(playlist_name, key_out, key_sz);
    if (key_out[0] == '\0') return false;
    snprintf(dest_path, dest_sz, "/logo/%s.bin", key_out);
    return true;
}

}  // namespace

void beginUploadArt(AsyncWebServerRequest* request) {
    (void)request;
    // Same as beginUploadBg: do not send() here — response comes from handleUploadArt on final chunk.
    // Не отвечать здесь — ответ придёт из handleUploadArt при final-чанке (иначе два ответа → клиент видит пустой 200).
}

void handleUploadArt(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
    (void)filename;

    if (index == 0) {
        gArtUploadArmed = false;
        gArtIoFatal     = false;
        gArtWrittenTotal = 0;
        if (gArtUploadFile) gArtUploadFile.close();

        // Determine dest from current station (server-side only — never from client param).
        // Путь определяется на сервере из stationByNum — клиент не может влиять на имя файла.
        if (!artBuildCurrentPath(gArtDestPath, sizeof(gArtDestPath), gArtNormalizedKey, sizeof(gArtNormalizedKey))) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"empty_key\"}");
            return;
        }

        size_t freeB = LittleFS.totalBytes() - LittleFS.usedBytes();
        if (freeB < kArtExpectedSize + 4096u) {
            request->send(507, "application/json", "{\"ok\":false,\"error\":\"insufficient_space\"}");
            return;
        }

        gArtUploadArmed = true;
        if (LittleFS.exists(kArtTmpPath)) LittleFS.remove(kArtTmpPath);
        gArtUploadFile = LittleFS.open(kArtTmpPath, "w");
        if (!gArtUploadFile) gArtIoFatal = true;
    } else {
        if (!gArtUploadArmed) return;
    }

    if (gArtIoFatal) {
        if (final) {
            gArtUploadArmed = false;
            if (gArtUploadFile) gArtUploadFile.close();
            LittleFS.remove(kArtTmpPath);
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"open_failed\"}");
        }
        return;
    }

    if (len && gArtUploadFile) {
        size_t n = gArtUploadFile.write(data, len);
        gArtWrittenTotal += n;
        if (n != len) gArtIoFatal = true;
    }

    if (!final) return;

    gArtUploadArmed = false;
    if (gArtUploadFile) gArtUploadFile.close();

    if (gArtIoFatal) {
        LittleFS.remove(kArtTmpPath);
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"write_failed\"}");
        gArtIoFatal = false;
        return;
    }

    // Validate: size, LVGL header, CF=5, 120×120
    File vf = LittleFS.open(kArtTmpPath, "r");
    if (!vf) {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"read_failed\"}");
        return;
    }
    const size_t sz = vf.size();
    if (sz != kArtExpectedSize) {
        vf.close(); LittleFS.remove(kArtTmpPath);
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"size_mismatch\"}");
        return;
    }
    uint8_t hdr[4];
    if (vf.read(hdr, 4) != 4) {
        vf.close(); LittleFS.remove(kArtTmpPath);
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"header_short\"}");
        return;
    }
    uint8_t cf = 0; uint16_t iw = 0, ih = 0;
    if (!bgParseImgHeader(hdr, &cf, &iw, &ih)) {  // reuse existing LVGL header parser
        vf.close(); LittleFS.remove(kArtTmpPath);
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_header\"}");
        return;
    }
    if (cf != kLvImgCfTrueColorAlpha) {
        vf.close(); LittleFS.remove(kArtTmpPath);
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"cf_not_true_color_alpha\"}");
        return;
    }
    if (iw != kArtSlotW || ih != kArtSlotH) {
        vf.close(); LittleFS.remove(kArtTmpPath);
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"dimensions_mismatch\"}");
        return;
    }
    vf.close();

    // Commit: rename tmp → dest (fallback to copy if rename fails across dirs on some FS versions)
    if (LittleFS.exists(gArtDestPath)) LittleFS.remove(gArtDestPath);
    if (!LittleFS.rename(kArtTmpPath, gArtDestPath)) {
        File src = LittleFS.open(kArtTmpPath, "r");
        File dst = LittleFS.open(gArtDestPath, "w");
        if (!src || !dst) {
            if (src) src.close();
            if (dst) dst.close();
            LittleFS.remove(kArtTmpPath);
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"commit_failed\"}");
            return;
        }
        uint8_t cpbuf[512];
        bool copy_ok = true;
        while (src.available()) {
            size_t rd = src.read(cpbuf, sizeof(cpbuf));
            if (rd && dst.write(cpbuf, rd) != rd) { copy_ok = false; break; }
        }
        src.close(); dst.close();
        LittleFS.remove(kArtTmpPath);
        if (!copy_ok) {
            LittleFS.remove(gArtDestPath);
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"commit_copy\"}");
            return;
        }
    }

    size_t final_sz = 0;
    const bool target_exists = LittleFS.exists(gArtDestPath);
    if (target_exists) {
        File committed = LittleFS.open(gArtDestPath, "r");
        if (committed) { final_sz = committed.size(); committed.close(); }
    }

    // Signal DspTask to reload art on Main screen / Сигнал DspTask — перезагрузить арт на Main.
    display.putRequest(ART_FS_UPDATED, 0);

    char okjson[320];
    snprintf(okjson, sizeof(okjson),
             "{\"ok\":true,\"normalized_key\":\"%s\",\"path\":\"%s\","
             "\"written_bytes\":%lu,\"final_size\":%lu,\"target_exists\":%s}",
             gArtNormalizedKey, gArtDestPath,
             (unsigned long)gArtWrittenTotal, (unsigned long)final_sz,
             target_exists ? "true" : "false");
    request->send(200, "application/json", okjson);
}

void handleRemoveArtHttp(AsyncWebServerRequest* request) {
    char key[68]  = {};
    char path[84] = {};
    if (!artBuildCurrentPath(path, sizeof(path), key, sizeof(key))) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"empty_key\"}");
        return;
    }
    if (LittleFS.exists(path)) {
        if (!LittleFS.remove(path)) {
            request->send(500, "application/json", "{\"ok\":false,\"error\":\"remove_failed\"}");
            return;
        }
    }
    display.putRequest(ART_FS_UPDATED, 0);
    request->send(200, "application/json", "{\"ok\":true}");
}

void handleArtStatusHttp(AsyncWebServerRequest* request) {
    char key[68]  = {};
    char path[84] = {};
    const char* playlist_name = config.stationByNum(config.lastStation());
    artNormalizeKey(playlist_name, key, sizeof(key));
    if (key[0] != '\0') {
        snprintf(path, sizeof(path), "/logo/%s.bin", key);
    }

    bool art_ok  = (path[0] != '\0') && LittleFS.exists(path);
    size_t art_sz = 0;
    if (art_ok) {
        File f = LittleFS.open(path, "r");
        if (f) { art_sz = f.size(); f.close(); }
    }

    // Minimal JSON-safe escape for station name (replace backslash and double-quote only).
    // Минимальное экранирование для JSON: только \\ и \".
    char name_esc[512] = {};
    size_t ne = 0;
    for (size_t i = 0; playlist_name[i] && ne < sizeof(name_esc) - 2u; i++) {
        char c = playlist_name[i];
        if (c == '"' || c == '\\') name_esc[ne++] = '\\';
        name_esc[ne++] = c;
    }

    char buf[640];
    snprintf(buf, sizeof(buf),
             "{\"station_name\":\"%s\",\"normalized_key\":\"%s\","
             "\"art_present\":%s,\"art_size\":%lu,\"slot_w\":%u,\"slot_h\":%u}",
             name_esc, key,
             art_ok ? "true" : "false",
             (unsigned long)art_sz,
             (unsigned)kArtSlotW, (unsigned)kArtSlotH);
    request->send(200, "application/json", buf);
}

String processor(const String& var) { // %Templates%
  if (var == "ACTION") return (network.status == CONNECTED && !config.emptyFS)?"webboard":"";
  if (var == "UPLOADWIFI") return (network.status == CONNECTED)?" hidden":"";
  if (var == "VERSION") return YOVERSION;
  return String();
}

int freeSpace;
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    if(filename!="tempwifi.csv"){
      if(LittleFS.exists(PLAYLIST_PATH)) LittleFS.remove(PLAYLIST_PATH);
      if(LittleFS.exists(INDEX_PATH)) LittleFS.remove(INDEX_PATH);
      if(LittleFS.exists(PLAYLIST_SD_PATH)) LittleFS.remove(PLAYLIST_SD_PATH);
      if(LittleFS.exists(INDEX_SD_PATH)) LittleFS.remove(INDEX_SD_PATH);
    }
    freeSpace = (float)(LittleFS.totalBytes() - LittleFS.usedBytes()) - 4096;
    request->_tempFile = LittleFS.open(TMP_PATH , "w");
  }
  if (len) {
    if(freeSpace>index+len){
      request->_tempFile.write(data, len);
    }
  }
  if (final) {
    request->_tempFile.close();
  }
}

// E36FS3a: request-scoped state via AsyncWebServerRequest::_tempObject (freed on disconnect).
// E36FS3a: состояние на запрос через _tempObject (освобождается при disconnect).
struct WebUploadReqState {
  uint16_t files_seen   = 0;
  uint16_t files_ok     = 0;
  uint16_t files_failed = 0;
  bool web_seen         = false;
  bool web_ok           = false;
  bool playlist_seen    = false;
  bool playlist_ok      = false;
  bool wifi_seen        = false;
  bool wifi_ok          = false;
  size_t total_bytes    = 0;
  char current_name[48] = {};
  char current_dest[64] = {};
  size_t current_bytes  = 0;
  bool current_is_web      = false;
  bool current_is_playlist = false;
  bool current_is_wifi     = false;
};

static Ticker s_webUploadRebootTicker;
static bool s_webUploadRebootPending = false;

static void webUploadDoReboot() {
  s_webUploadRebootTicker.detach();
  s_webUploadRebootPending = false;
  ESP.restart();
}

static void webUploadScheduleReboot() {
  if (s_webUploadRebootPending) return;
  s_webUploadRebootPending = true;
  Serial.println("[WebUpload] Upload complete; reboot scheduled");
  s_webUploadRebootTicker.once(1.0f, webUploadDoReboot);  // response first, then ~1s / сначала ответ, затем ~1 с
}

// POST / (emptyFS) and POST /webboard share this upload path / общий путь для встроенного uploader.
static bool webUploadIsEmbeddedRoute(const AsyncWebServerRequest* request) {
  const String& url = request->url();
  return url == "/" || url == "/webboard";
}

static WebUploadReqState* webUploadReqState(AsyncWebServerRequest* request) {
  if (!request->_tempObject) {
    request->_tempObject = calloc(1, sizeof(WebUploadReqState));
  }
  return static_cast<WebUploadReqState*>(request->_tempObject);
}

// Basename only — strip browser path prefixes / только basename, без каталогов браузера.
static String webUploadNormalizeBasename(String filename) {
  filename.replace('\\', '/');
  const int slash = filename.lastIndexOf('/');
  if (slash >= 0) {
    filename = filename.substring(slash + 1);
  }
  return filename;
}

static bool webUploadBasenameEquals(const String& base, const char* literal) {
  return base.length() == strlen(literal) && base.equalsIgnoreCase(literal);
}

// AI prompt .txt — handled in handleUploadWeb with its own HTTP response / свой ответ в upload handler.
static bool webUploadIsAiPromptBasename(const String& base) {
  return base.endsWith(".txt") && !webUploadBasenameEquals(base, "playlist.csv")
      && !webUploadBasenameEquals(base, "wifi.csv");
}

static bool webUploadRequestHasAiPromptFile(AsyncWebServerRequest* request) {
  for (size_t i = 0; i < request->params(); i++) {
    AsyncWebParameter* p = request->getParam(i);
    if (p && p->isFile() && webUploadIsAiPromptBasename(webUploadNormalizeBasename(p->value()))) {
      return true;
    }
  }
  return false;
}

static bool webUploadMapDestination(const String& base, String& outDest) {
  if (base.length() == 0 || base.indexOf("..") >= 0) {
    return false;
  }
  if (webUploadBasenameEquals(base, "playlist.csv")) {
    outDest = "/data/playlist.csv";
    return true;
  }
  if (webUploadBasenameEquals(base, "wifi.csv")) {
    outDest = "/data/wifi.csv";
    return true;
  }
  outDest = "/www/" + base;
  return true;
}

static void webUploadMarkFileFailed(WebUploadReqState* upSt) {
  if (!upSt) return;
  upSt->files_failed++;
}

static void webUploadBeginFile(WebUploadReqState* upSt, const String& base, const String& dest) {
  if (!upSt) return;
  upSt->files_seen++;
  upSt->current_bytes = 0;
  upSt->current_is_web = dest.startsWith("/www/");
  upSt->current_is_playlist = webUploadBasenameEquals(base, "playlist.csv");
  upSt->current_is_wifi = webUploadBasenameEquals(base, "wifi.csv");
  if (upSt->current_is_web) upSt->web_seen = true;
  if (upSt->current_is_playlist) upSt->playlist_seen = true;
  if (upSt->current_is_wifi) upSt->wifi_seen = true;
  strncpy(upSt->current_name, base.c_str(), sizeof(upSt->current_name) - 1);
  strncpy(upSt->current_dest, dest.c_str(), sizeof(upSt->current_dest) - 1);
  Serial.printf("[WebUpload] begin name='%s' dest='%s'\n", upSt->current_name, upSt->current_dest);
}

static void webUploadCompleteFile(WebUploadReqState* upSt, bool callIndexPlaylist) {
  if (!upSt) return;
  upSt->files_ok++;
  upSt->total_bytes += upSt->current_bytes;
  if (upSt->current_is_web) upSt->web_ok = true;
  if (upSt->current_is_playlist) upSt->playlist_ok = true;
  if (upSt->current_is_wifi) upSt->wifi_ok = true;
  Serial.printf("[WebUpload] complete name='%s' bytes=%u\n",
                upSt->current_name, static_cast<unsigned>(upSt->current_bytes));
  if (callIndexPlaylist) {
    config.indexPlaylist();
  }
}

void handleWebUploadComplete(AsyncWebServerRequest* request) {
  // emptyFS Wi-Fi credential form — not a file upload / форма SSID, не multipart файлов.
  if (request->arg("ssid") != "" && request->arg("pass") != "") {
    char buf[BUFLEN];
    memset(buf, 0, BUFLEN);
    snprintf(buf, BUFLEN, "%s\t%s", request->arg("ssid").c_str(), request->arg("pass").c_str());
    request->redirect("/");
    config.saveWifiFromPost(buf);
    return;
  }

  WebUploadReqState* const upSt = static_cast<WebUploadReqState*>(request->_tempObject);
  if (!upSt) {
    bool anyFile = false;
    for (size_t i = 0; i < request->params(); i++) {
      AsyncWebParameter* p = request->getParam(i);
      if (p && p->isFile()) {
        anyFile = true;
        break;
      }
    }
    if (!anyFile) {
      Serial.println("[WebUpload] Upload failed; reboot cancelled");
      request->send(400, "text/plain", "No file selected");
      return;
    }
    // AI prompt: handleUploadWeb already sent 200/4xx/5xx on final chunk — one response only.
    // AI prompt: ответ уже отправлен в handleUploadWeb — не дублировать redirect/send.
    if (webUploadRequestHasAiPromptFile(request)) {
      return;
    }
    // Other untracked file types — redirect only, no reboot / прочие типы — только redirect.
    request->redirect("/");
    return;
  }

  Serial.printf("[WebUpload] request complete files=%u ok=%u failed=%u web=%d playlist=%d wifi=%d\n",
                upSt->files_seen, upSt->files_ok, upSt->files_failed,
                upSt->web_ok ? 1 : 0, upSt->playlist_ok ? 1 : 0, upSt->wifi_ok ? 1 : 0);

  const bool ok = upSt->files_seen > 0
               && upSt->files_ok == upSt->files_seen
               && upSt->files_failed == 0;
  if (!ok) {
    Serial.println("[WebUpload] Upload failed; reboot cancelled");
    request->send(upSt->files_seen > 0 ? 500 : 400, "text/plain",
                  upSt->files_seen > 0 ? "Upload failed" : "No file selected");
    return;
  }
  request->redirect("/");
  webUploadScheduleReboot();
}

void handleUploadWeb(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  DBGVB("File: %s, size:%u bytes, index: %u, final: %s\n", filename.c_str(), len, index, final?"true":"false");

  const String base = webUploadNormalizeBasename(filename);
  
  // Handle AI prompt upload / Обработка загрузки AI промпта
  // Accept any .txt file for AI prompt (filename ignored, always saved as /ai/ai_prompt.txt)
  // Принимаем любой .txt файл для AI prompt (имя файла игнорируется, всегда сохраняется как /ai/ai_prompt.txt)
  // Atomic upload: write to /ai/ai_prompt.tmp, replace only on success
  // Атомарная загрузка: пишем в /ai/ai_prompt.tmp, заменяем только при успехе
  const bool is_prompt_file = webUploadIsAiPromptBasename(base);
  
  if (is_prompt_file) {
    // Get max prompt size from ai_prompt module / Получить максимальный размер промпта из модуля ai_prompt
    extern size_t aiPromptGetMaxLen();
    size_t max_prompt_size = aiPromptGetMaxLen();
    const char* tmp_path = "/ai/ai_prompt.tmp";
    const char* final_path = "/ai/ai_prompt.txt";
    
    if (!index) {
      // First chunk - validate and prepare / Первый chunk - валидация и подготовка
      // ATOMIC UPLOAD: Do NOT touch existing prompt file until upload succeeds
      // АТОМАРНАЯ ЗАГРУЗКА: НЕ трогаем существующий prompt файл до успешной загрузки
      
      // Debug logging (debug-only) / Отладочное логирование (только debug)
      AI_DLOG("[AI] Upload start: filename=%s index=%u len=%u final=%d", filename.c_str(), index, len, final ? 1 : 0);
      
      // Save old file size for logging overwrite / Сохранить старый размер файла для логирования перезаписи
      size_t old_size = 0;
      if (LittleFS.exists(final_path)) {
        File old_file = LittleFS.open(final_path, "r");
        if (old_file) {
          old_size = old_file.size();
          old_file.close();
        }
      }
      
      // Store old_size for logging (static variable, safe for sequential uploads)
      // Сохранить old_size для логирования (static переменная, безопасно для последовательных загрузок)
      static size_t g_upload_old_size = 0;
      g_upload_old_size = old_size;
      
      AI_DLOG("[AI] Upload: old_size=%u", old_size);
      
      // Remove any leftover temp file / Удалить любой оставшийся временный файл
      if (LittleFS.exists(tmp_path)) {
        LittleFS.remove(tmp_path);
      }
      
      float freeSpace = (float)(LittleFS.totalBytes() - LittleFS.usedBytes()) - 4096;
      AI_DLOG("[AI] Upload: freeSpace=%.0f max_prompt_size=%u", freeSpace, max_prompt_size);
      
      if (freeSpace < max_prompt_size) {
        AI_LOG("[AI] Upload rejected: insufficient FS space (free=%.0f, required=%u)", freeSpace, max_prompt_size);
        request->send(413, "text/plain", "Insufficient filesystem space");
        return;
      }
      
      // Content-Length is the size of the full multipart body, not the file itself.
      // Actual file size is validated after writing (see final chunk handling below).
      if (request->hasHeader("Content-Length")) {
        size_t content_length = atoi(request->getHeader("Content-Length")->value().c_str());
        AI_DLOG("[AI] Upload: Content-Length=%u (multipart body, not file size)", content_length);
      }
      
      // Open TEMP file for writing (atomic upload) / Открыть ВРЕМЕННЫЙ файл для записи (атомарная загрузка)
      request->_tempFile = LittleFS.open(tmp_path, "w");
      if (!request->_tempFile) {
        AI_LOG("[AI] Upload failed: FS error (cannot open temp file)");
        AI_DLOG("[AI] Upload: open() failed for %s", tmp_path);
        request->send(500, "text/plain", "Filesystem error");
        return;
      }
      
      AI_DLOG("[AI] Upload: temp file opened successfully");
    }
    
    if (len) {
      // Check current file size during upload / Проверить текущий размер файла во время загрузки
      if (request->_tempFile) {
        // Для первого chunk (index=0) size() может возвращать мусорное значение
        // For first chunk (index=0) size() may return garbage value
        // Используем index как приблизительную оценку размера для проверки
        // Use index as approximate size estimate for validation
        size_t estimated_size = (index == 0) ? len : request->_tempFile.size();
        
        if (estimated_size + len > max_prompt_size) {
          AI_LOG("[AI] Upload rejected: prompt too large (estimated=%u + chunk=%u > max=%u)", estimated_size, len, max_prompt_size);
          request->_tempFile.close();
          LittleFS.remove(tmp_path);  // Remove temp file only / Удалить только временный файл
          request->send(413, "text/plain", "File too large");
          return;
        }
        
        size_t bytes_written = request->_tempFile.write(data, len);
        request->_tempFile.flush();
        
        AI_DLOG("[AI] Upload chunk: index=%u len=%u written=%u final=%d estimated_size=%u", index, len, bytes_written, final ? 1 : 0, estimated_size);
        
        if (bytes_written != len) {
          AI_LOG("[AI] Upload failed: write error (requested=%u written=%u)", len, bytes_written);
          request->_tempFile.close();
          LittleFS.remove(tmp_path);
          request->send(500, "text/plain", "Filesystem write error");
          return;
        }
      } else {
        AI_DLOG("[AI] Upload chunk: request->_tempFile is NULL (index=%u len=%u)", index, len);
      }
    }
    
    if (final) {
      if (request->_tempFile) {
        request->_tempFile.flush();  // Final flush before checking size / Финальный flush перед проверкой размера
        size_t new_size = request->_tempFile.size();
        request->_tempFile.close();
        
        // Debug logging (debug-only) / Отладочное логирование (только debug)
        AI_DLOG("[AI] Upload final: tmp_size=%u max_prompt_size=%u index=%u len=%u", new_size, max_prompt_size, index, len);
        
        // Validate final file size / Валидация итогового размера файла
        if (new_size == 0 || new_size > max_prompt_size) {
          AI_LOG("[AI] Upload failed: invalid file size (%u, max=%u)", new_size, max_prompt_size);
          AI_DLOG("[AI] Upload: validation failed, removing tmp only");
          LittleFS.remove(tmp_path);  // Remove temp file only, keep old prompt / Удалить только временный файл, оставить старый prompt
          request->send(400, "text/plain", "Invalid file size");
          return;
        }
        
        // ROLLBACK-SAFE REPLACE: Create backup of old file, then replace only on success
        // БЕЗОПАСНАЯ ЗАМЕНА С ВОССТАНОВЛЕНИЕМ: Создаём backup старого файла, затем заменяем только при успехе
        // If replace fails, restore backup and keep old prompt
        // Если замена не удалась, восстанавливаем backup и оставляем старый prompt
        const char* bak_path = "/ai/ai_prompt.bak";
        
        // Step 1: Create backup of old file (if exists) / Шаг 1: Создать backup старого файла (если существует)
        bool has_backup = false;
        if (LittleFS.exists(final_path)) {
          // Remove old backup if exists / Удалить старый backup если существует
          if (LittleFS.exists(bak_path)) {
            LittleFS.remove(bak_path);
          }
          
          // Copy old file to backup / Копировать старый файл в backup
          File old_file = LittleFS.open(final_path, "r");
          if (old_file) {
            File bak_file = LittleFS.open(bak_path, "w");
            if (bak_file) {
              uint8_t buf[128];
              bool bak_success = true;
              while (old_file.available()) {
                size_t bytes_read = old_file.read(buf, sizeof(buf));
                if (bytes_read > 0) {
                  size_t bytes_written = bak_file.write(buf, bytes_read);
                  if (bytes_written != bytes_read) {
                    bak_success = false;
                    break;
                  }
                }
              }
              bak_file.close();
              old_file.close();
              
              if (bak_success) {
                has_backup = true;
              } else {
                // Backup failed - remove corrupted backup / Backup не удался - удалить повреждённый backup
                LittleFS.remove(bak_path);
              }
            } else {
              old_file.close();
            }
          }
        }
        
        // Step 2: Copy temp to final (overwrite) / Шаг 2: Копировать временный в финальный (перезаписать)
        File tmp_file = LittleFS.open(tmp_path, "r");
        if (!tmp_file) {
          AI_LOG("[AI] Upload failed: cannot read temp file for finalization");
          LittleFS.remove(tmp_path);
          if (has_backup && LittleFS.exists(bak_path)) {
            LittleFS.remove(bak_path);  // Cleanup backup if we didn't use it / Очистить backup если не использовали
          }
          request->send(500, "text/plain", "Filesystem error");
          return;
        }
        
        File final_file = LittleFS.open(final_path, "w");
        if (!final_file) {
          tmp_file.close();
          AI_LOG("[AI] Upload failed: cannot create final file");
          LittleFS.remove(tmp_path);
          if (has_backup && LittleFS.exists(bak_path)) {
            LittleFS.remove(bak_path);  // Cleanup backup / Очистить backup
          }
          request->send(500, "text/plain", "Filesystem error");
          return;
        }
        
        // Copy temp to final / Копировать временный в финальный
        uint8_t buf[128];
        bool copy_success = true;
        while (tmp_file.available()) {
          size_t bytes_read = tmp_file.read(buf, sizeof(buf));
          if (bytes_read > 0) {
            size_t bytes_written = final_file.write(buf, bytes_read);
            if (bytes_written != bytes_read) {
              copy_success = false;
              break;
            }
          }
        }
        
        tmp_file.close();
        final_file.close();
        
        // Step 3: Validate copied file / Шаг 3: Валидировать скопированный файл
        // Check final file size (more reliable than counting bytes during copy)
        // Проверяем размер финального файла (надёжнее, чем подсчёт байтов при копировании)
        size_t final_size = 0;
        File validate_file = LittleFS.open(final_path, "r");
        if (validate_file) {
          final_size = validate_file.size();
          validate_file.close();
        }
        
        AI_DLOG("[AI] Upload: copy result - copy_success=%d tmp_size=%u final_size=%u", copy_success ? 1 : 0, new_size, final_size);
        
        if (!copy_success || final_size != new_size || final_size == 0) {
          // Copy failed or validation failed - rollback / Копирование не удалось или валидация не прошла - откат
          if (LittleFS.exists(final_path)) {
            LittleFS.remove(final_path);  // Remove corrupted final / Удалить повреждённый финальный
          }
          
          // Restore backup if exists / Восстановить backup если существует
          if (has_backup && LittleFS.exists(bak_path)) {
            File bak_file = LittleFS.open(bak_path, "r");
            if (bak_file) {
              File restore_file = LittleFS.open(final_path, "w");
              if (restore_file) {
                uint8_t buf[128];
                bool restore_success = true;
                while (bak_file.available()) {
                  size_t bytes_read = bak_file.read(buf, sizeof(buf));
                  if (bytes_read > 0) {
                    size_t bytes_written = restore_file.write(buf, bytes_read);
                    if (bytes_written != bytes_read) {
                      restore_success = false;
                      break;
                    }
                  }
                }
                restore_file.close();
                bak_file.close();
                
                if (restore_success) {
                  AI_LOG("[AI] Upload failed: restored prompt from backup");
                } else {
                  LittleFS.remove(final_path);  // Remove corrupted restore / Удалить повреждённое восстановление
                }
              } else {
                bak_file.close();
              }
            }
            LittleFS.remove(bak_path);  // Cleanup backup after restore attempt / Очистить backup после попытки восстановления
          }
          
          LittleFS.remove(tmp_path);
          AI_LOG("[AI] Upload failed: file copy/validation failed (tmp_size=%u final_size=%u)", new_size, final_size);
          request->send(500, "text/plain", "Filesystem copy/validation error");
          return;
        }
        
        // Step 4: Replace succeeded - cleanup temp and backup / Шаг 4: Замена успешна - очистить временный файл и backup
        LittleFS.remove(tmp_path);
        if (has_backup && LittleFS.exists(bak_path)) {
          LittleFS.remove(bak_path);  // Remove backup after successful replace / Удалить backup после успешной замены
        }
        
        // Log upload event FIRST (before cache reset) / Залогировать событие загрузки ПЕРВЫМ (до сброса кеша)
        static size_t g_upload_old_size = 0;
        if (g_upload_old_size > 0) {
          AI_LOG("[AI] Prompt overwritten: /ai/ai_prompt.txt (old_len=%u new_len=%u)", g_upload_old_size, new_size);
        } else {
          AI_LOG("[AI] Prompt uploaded: /ai/ai_prompt.txt (len=%u)", new_size);
        }
        g_upload_old_size = 0;
        
        // Reset prompt cache AFTER successful upload / Сбросить кеш промпта ПОСЛЕ успешной загрузки
        // IMPORTANT: Reset only after file is written and validated / ВАЖНО: Сброс только после записи и валидации файла
        extern void aiPromptResetCache();
        aiPromptResetCache();
        
        request->send(200, "text/plain", "OK");
      } else {
        AI_LOG("[AI] Upload failed: FS error (file not open)");
        if (LittleFS.exists(tmp_path)) {
          LittleFS.remove(tmp_path);  // Cleanup temp file / Очистить временный файл
        }
        request->send(500, "text/plain", "Filesystem error");
      }
      return;
    }
    return;  // Early return for prompt upload / Ранний возврат для загрузки промпта
  }
  
  // Original logic for other files / Оригинальная логика для других файлов
  WebUploadReqState* const upSt = webUploadIsEmbeddedRoute(request) ? webUploadReqState(request) : nullptr;

  if (!index) {
    String dest;
    if (!webUploadMapDestination(base, dest)) {
      webUploadMarkFileFailed(upSt);
      return;
    }
    webUploadBeginFile(upSt, base, dest);
    request->_tempFile = LittleFS.open(dest, "w");
    if (!request->_tempFile) {
      webUploadMarkFileFailed(upSt);
      return;
    }
  }
  if (len) {
    if (!request->_tempFile) {
      webUploadMarkFileFailed(upSt);
      return;
    }
    const size_t written = request->_tempFile.write(data, len);
    if (written != len) {
      request->_tempFile.close();
      webUploadMarkFileFailed(upSt);
      return;
    }
    if (upSt) upSt->current_bytes += written;
  }
  if (final) {
    if (!request->_tempFile) {
      webUploadMarkFileFailed(upSt);
      return;
    }
    request->_tempFile.close();
    const bool isPlaylist = webUploadBasenameEquals(base, "playlist.csv");
    webUploadCompleteFile(upSt, isPlaylist);
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT: if (config.store.audioinfo) Serial.printf("[WEBSOCKET] client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str()); break;
    case WS_EVT_DISCONNECT: if (config.store.audioinfo) Serial.printf("[WEBSOCKET] client #%u disconnected\n", client->id()); break;
    case WS_EVT_DATA: netserver.onWsMessage(arg, data, len, client->id()); break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void handleHTTPArgs(AsyncWebServerRequest * request) {
  /* Fallback if HTTP_ANY "/" handled this URL first / Запасной путь если сначала сработал catch-all */
  if (request->method() == HTTP_GET && strcmp(request->url().c_str(), "/bg_status") == 0) {
    handleBgStatusHttp(request);
    return;
  }
  if (request->method() == HTTP_GET) {
    DBGVB("[%s] client ip=%s request of %s", __func__, request->client()->remoteIP().toString().c_str(), request->url().c_str());
    if (strcmp(request->url().c_str(), PLAYLIST_PATH) == 0 || 
        strcmp(request->url().c_str(), SSIDS_PATH) == 0 || 
        strcmp(request->url().c_str(), INDEX_PATH) == 0 || 
        strcmp(request->url().c_str(), TMP_PATH) == 0 || 
        strcmp(request->url().c_str(), PLAYLIST_SD_PATH) == 0 || 
        strcmp(request->url().c_str(), INDEX_SD_PATH) == 0) {
#ifdef MQTT_ROOT_TOPIC
      if (strcmp(request->url().c_str(), PLAYLIST_PATH) == 0) while (mqttplaylistblock) vTaskDelay(5);
#endif
      if(strcmp(request->url().c_str(), PLAYLIST_PATH) == 0 && config.getMode()==PM_SDCARD){
         netserver.chunkedHtmlPage("application/octet-stream", request, PLAYLIST_SD_PATH, false);
      }else{
        netserver.chunkedHtmlPage("application/octet-stream", request, request->url().c_str(), false);
      }
      return;
    }
    if (strcmp(request->url().c_str(), "/") == 0 && request->params() == 0) {
      netserver.chunkedHtmlPage(String(), request, network.status == CONNECTED ? "/www/index.html" : "/www/settings.html");
      return;
    }
    if (strcmp(request->url().c_str(), "/update") == 0 || strcmp(request->url().c_str(), "/settings") == 0 || strcmp(request->url().c_str(), "/appearance") == 0 || strcmp(request->url().c_str(), "/ir") == 0) {
      char buf[40] = { 0 };
      sprintf(buf, "/www%s.html", request->url().c_str());
      netserver.chunkedHtmlPage(String(), request, buf);
      return;
    }
  }
  if (network.status == CONNECTED) {
    bool commandFound=false;
    if (request->hasArg("start")) { player.sendCommand({PR_PLAY, config.lastStation()}); commandFound=true; }
    if (request->hasArg("stop")) { player.sendCommand({PR_STOP, 0}); commandFound=true; }
    if (request->hasArg("toggle")) { player.toggle(); commandFound=true; }
    if (request->hasArg("prev")) { player.prev(); commandFound=true; }
    if (request->hasArg("next")) { player.next(); commandFound=true; }
    if (request->hasArg("volm")) { player.stepVol(false); commandFound=true; }
    if (request->hasArg("volp")) { player.stepVol(true); commandFound=true; }
    #ifdef USE_SD
    if (request->hasArg("mode")) {
      AsyncWebParameter* p = request->getParam("mode");
      int mm = atoi(p->value().c_str());
      if(mm>2) mm=0;
      if(mm==2)
        config.changeMode();
      else
        config.changeMode(mm);
      commandFound=true;
    }
    #endif
    if (request->hasArg("reset")) { request->redirect("/"); request->send(200); config.reset(); return; }
    if (request->hasArg("trebble") && request->hasArg("middle") && request->hasArg("bass")) {
      AsyncWebParameter* pt = request->getParam("trebble", request->method() == HTTP_POST);
      AsyncWebParameter* pm = request->getParam("middle", request->method() == HTTP_POST);
      AsyncWebParameter* pb = request->getParam("bass", request->method() == HTTP_POST);
      int t = atoi(pt->value().c_str());
      int m = atoi(pm->value().c_str());
      int b = atoi(pb->value().c_str());
      player.setTone(b, m, t);
      config.setTone(b, m, t);
      netserver.requestOnChange(EQUALIZER, 0);
      commandFound=true;
    }
    if (request->hasArg("ballance")) {
      AsyncWebParameter* p = request->getParam("ballance", request->method() == HTTP_POST);
      int b = atoi(p->value().c_str());
      player.setBalance(b);
      config.setBalance(b);
      netserver.requestOnChange(BALANCE, 0);
      commandFound=true;
    }
    if (request->hasArg("playstation") || request->hasArg("play")) {
      AsyncWebParameter* p = request->getParam(request->hasArg("playstation") ? "playstation" : "play", request->method() == HTTP_POST);
      int id = atoi(p->value().c_str());
      if (id < 1) id = 1;
      if (id > config.store.countStation) id = config.store.countStation;
      //config.sdResumePos = 0;
      player.sendCommand({PR_PLAY, id});
      commandFound=true;
      DBGVB("[%s] play=%d", __func__, id);
    }
    if (request->hasArg("vol")) {
      AsyncWebParameter* p = request->getParam("vol", request->method() == HTTP_POST);
      int v = atoi(p->value().c_str());
      if (v < 0) v = 0;
      if (v > 254) v = 254;
      config.setVolume((uint8_t)v);
      player.setVol((uint8_t)v);
      commandFound=true;
      DBGVB("[%s] vol=%d", __func__, v);
    }
    if (request->hasArg("dspon")) {
      AsyncWebParameter* p = request->getParam("dspon", request->method() == HTTP_POST);
      int d = atoi(p->value().c_str());
      config.setDspOn(d!=0);
      commandFound=true;
    }
    if (request->hasArg("dim")) {
      AsyncWebParameter* p = request->getParam("dim", request->method() == HTTP_POST);
      int d = atoi(p->value().c_str());
      if (d < 0) d = 0;
      if (d > 100) d = 100;
      config.store.brightness = (uint8_t)d;
      config.setBrightness(true);
      commandFound=true;
    }
    if (request->hasArg("sleep")) {
      AsyncWebParameter* sfor = request->getParam("sleep", request->method() == HTTP_POST);
      int sford = atoi(sfor->value().c_str());
      int safterd = 0;
      if(request->hasArg("after")){
        AsyncWebParameter* safter = request->getParam("after", request->method() == HTTP_POST);
        safterd = atoi(safter->value().c_str());
      }
      if(sford > 0 && safterd >= 0){
        request->send(200);
        config.sleepForAfter(sford, safterd);
        commandFound=true;
      }
    }
    if (request->hasArg("clearspiffs") || request->hasArg("clearfs")) {
      if(config.fsCleanup()){
        config.saveValue(&config.store.play_mode, static_cast<uint8_t>(PM_WEB));
        request->redirect("/");
        ESP.restart();
      }else{
        request->send(200);
      }
      return;
    }
    if (request->params() > 0) {
      request->send(commandFound?200:404);
      return;
    }
  } else {
    if (request->params() > 0) {
      request->send(404);
      return;
    }
  }
}
