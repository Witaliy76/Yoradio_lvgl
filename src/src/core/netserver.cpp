#include "netserver.h"
#include <LittleFS.h>

#include "config.h"
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
#include "../plugins/AIPlugin.h"
#include "../plugins/ai/ai_log.h"  // AI Layer logging macros

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

String processor(const String& var);
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleUploadWeb(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleHTTPArgs(AsyncWebServerRequest * request);
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

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

bool NetServer::begin(bool quiet) {
  if(network.status==SDREADY) return true;
  if(!quiet) Serial.print("##[BOOT]#\tnetserver.begin\t");
  importRequest = IMDONE;
  irRecordEnable = false;
  nsQueue = xQueueCreate( 20, sizeof( nsRequestParams_t ) );
  while(nsQueue==NULL){;}
  if(config.emptyFS){
    webserver.on("/", HTTP_GET, [](AsyncWebServerRequest * request) { request->send_P(200, "text/html", emptyfs_html, processor); });
    webserver.on("/", HTTP_POST, [](AsyncWebServerRequest *request) { 
      if(request->arg("ssid")!="" && request->arg("pass")!=""){
        char buf[BUFLEN];
        memset(buf, 0, BUFLEN);
        snprintf(buf, BUFLEN, "%s\t%s", request->arg("ssid").c_str(), request->arg("pass").c_str());
        request->redirect("/");
        config.saveWifiFromNextion(buf);
        return;
      }
      request->redirect("/"); 
      ESP.restart(); 
    }, handleUploadWeb);
  }else{
    webserver.on("/", HTTP_ANY, handleHTTPArgs);
    webserver.on("/webboard", HTTP_GET, [](AsyncWebServerRequest * request) { request->send_P(200, "text/html", emptyfs_html, processor); });
    webserver.on("/webboard", HTTP_POST, [](AsyncWebServerRequest *request) { request->redirect("/"); }, handleUploadWeb);
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
  if (IR_PIN != 255) webserver.on("/ir", HTTP_GET, handleHTTPArgs);
  webserver.serveStatic("/", LittleFS, "/www/").setCacheControl("max-age=31536000");
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
  if(!quiet) Serial.println("done");
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
    int target = (request->getParam("updatetarget", true)->value() == "spiffs") ? U_SPIFFS : U_FLASH;
    Serial.printf("Update Start: %s\n", filename.c_str());
    player.sendCommand({PR_STOP, 0});
    display.putRequest(NEWMODE, UPDATING);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, target)) {
      Update.printError(Serial);
      request->send(200, "text/html", updateError());
    }
  }
  if (!Update.hasError()) {
    if (Update.write(data, len) != len) {
      Update.printError(Serial);
      request->send(200, "text/html", updateError());
    }
  }
  if (final) {
    if (Update.end(true)) {
      Serial.printf("Update Success: %uB\n", index + len);
    } else {
      Update.printError(Serial);
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
#if !defined(HIDE_WEATHER) && (!defined(DUMMYDISPLAY) && !defined(USE_NEXTION))
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
void NetServer::processQueue(){
  if(nsQueue==NULL) return;
  nsRequestParams_t request;
  if(xQueueReceive(nsQueue, &request, NS_QUEUE_TICKS)){
    memset(wsbuf, 0, BUFLEN * 2);
    uint8_t clientId = request.clientId;
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
          bool dbgact = false, nxtn=false;
          String act = F("\"group_wifi\",");
          if (network.status == CONNECTED) {
                                                                act += F("\"group_system\",");
            if (BRIGHTNESS_PIN != 255 || DSP_CAN_FLIPPED || DSP_MODEL == DSP_NOKIA5110 || dbgact)    act += F("\"group_display\",");
          #ifdef USE_NEXTION
                                                                act += F("\"group_nextion\",");
            if (!SHOW_WEATHER || dbgact)                        act += F("\"group_weather\",");
            nxtn=true;
          #endif
                                                              #if defined(LCD_I2C) || defined(DSP_OLED)
                                                                act += F("\"group_oled\",");
                                                              #endif
                                                              #ifndef HIDE_VU
                                                                act += F("\"group_vu\",");
                                                              #endif
            #ifdef ENABLE_BRIGHTNESS_CONTROL
                                                                act += F("\"group_brightness\",");
            #endif
            if (DSP_CAN_FLIPPED || dbgact)                      act += F("\"group_tft\",");
            if (TS_MODEL != TS_MODEL_UNDEFINED || dbgact)       act += F("\"group_touch\",");
            if (DSP_MODEL == DSP_NOKIA5110)                     act += F("\"group_nokia\",");
                                                                act += F("\"group_timezone\",");
            if (SHOW_WEATHER || dbgact)                         act += F("\"group_weather\",");
                                                                act += F("\"group_ai\",");
                                                                act += F("\"group_controls\",");
            if (ENC_BTNL != 255 || ENC2_BTNL != 255 || dbgact)  act += F("\"group_encoder\",");
            if (IR_PIN != 255 || dbgact)                        act += F("\"group_ir\",");
          }
                                                                act = act.substring(0, act.length() - 1);
          sprintf (wsbuf, "{\"act\":[%s]}", act.c_str());
          break;
        }
      case GETMODE:       sprintf (wsbuf, "{\"pmode\":\"%s\"}", network.status == CONNECTED ? "player" : "ap"); break;
      case GETINDEX:      {
          requestOnChange(STATION, clientId); 
          requestOnChange(TITLE, clientId); 
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
      case GETSYSTEM:     sprintf (wsbuf, "{\"sst\":%d,\"aif\":%d,\"vu\":%d,\"softr\":%d,\"vut\":%d,\"mdns\":\"%s\"}", 
                                  config.store.smartstart != 2, 
                                  config.store.audioinfo, 
                                  config.store.vumeter, 
                                  config.store.softapdelay,
                                  config.vuThreshold,
                                  config.store.mdnsname); 
                                  break;
      case GETSCREEN:     sprintf (wsbuf, "{\"flip\":%d,\"inv\":%d,\"nump\":%d,\"tsf\":%d,\"tsd\":%d,\"dspon\":%d,\"br\":%d,\"con\":%d,\"scre\":%d,\"scrt\":%d,\"scrb\":%d,\"scrpe\":%d,\"scrpt\":%d,\"scrpb\":%d,\"sa\":%d}", 
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
      case GETTIMEZONE:   sprintf (wsbuf, "{\"tzh\":%d,\"tzm\":%d,\"sntp1\":\"%s\",\"sntp2\":\"%s\"}", 
                                  config.store.tzHour, 
                                  config.store.tzMin, 
                                  config.store.sntp1, 
                                  config.store.sntp2); 
                                  break;
      case GETWEATHER:    sprintf (wsbuf, "{\"wen\":%d,\"wlat\":\"%s\",\"wlon\":\"%s\",\"wkey\":\"%s\"}", 
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
                                  sprintf (wsbuf, "{\"aien\":%d,\"aihost\":\"%s\",\"aiport\":%d,\"aipath\":\"%s\",\"aitimeout\":%lu,\"aimodel\":\"%s\",\"aikey\":\"%s\",\"aiprompt_loaded\":%d,\"aiprompt_size\":%u}", 
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
      case GETCONTROLS:   sprintf (wsbuf, "{\"vols\":%d,\"enca\":%d,\"irtl\":%d,\"skipup\":%d}", 
                                  config.store.volsteps, 
                                  config.store.encacc, 
                                  config.store.irtlp,
                                  config.store.skipPlaylistUpDown); 
                                  break;
      case DSPON:         sprintf (wsbuf, "{\"dspontrue\":%d}", 1); break;
      case STATION:       requestOnChange(STATIONNAME, clientId); requestOnChange(ITEM, clientId); break;
      case STATIONNAME:   sprintf (wsbuf, "{\"nameset\": \"%s\"}", config.station.name); break;
      case ITEM:          sprintf (wsbuf, "{\"current\": %d}", config.lastStation()); break;
      case TITLE:         sprintf (wsbuf, "{\"meta\": \"%s\"}", config.station.title); telnet.printf("##CLI.META#: %s\n> ", config.station.title); break;
      case VOLUME:        sprintf (wsbuf, "{\"vol\": %d}", config.store.volume); telnet.printf("##CLI.VOL#: %d\n", config.store.volume); break;
      case NRSSI:         sprintf (wsbuf, "{\"rssi\": %d}", rssi); /*rssi = 255;*/ break;
      case SDPOS:         sprintf (wsbuf, "{\"sdpos\": %d,\"sdend\": %d,\"sdtpos\": %d,\"sdtend\": %d}", 
                                  player.getFilePos(), 
                                  player.getFileSize(), 
                                  player.getAudioCurrentTime(), 
                                  player.getAudioFileDuration()); 
                                  break;
      case SDLEN:         sprintf (wsbuf, "{\"sdmin\": %d,\"sdmax\": %d}", player.sd_min, player.sd_max); break;
      case SDSNUFFLE:     sprintf (wsbuf, "{\"snuffle\": %d}", config.store.sdsnuffle); break;
      case BITRATE:       sprintf (wsbuf, "{\"bitrate\": %d, \"format\": \"%s\"}", config.station.bitrate, getFormat(config.configFmt)); break;
      case MODE:          sprintf (wsbuf, "{\"mode\": \"%s\"}", player.status() == PLAYING ? "playing" : "stopped"); telnet.info(); break;
      case EQUALIZER:     sprintf (wsbuf, "{\"bass\": %d, \"middle\": %d, \"trebble\": %d}", config.store.bass, config.store.middle, config.store.trebble); break;
      case BALANCE:       sprintf (wsbuf, "{\"balance\": %d}", config.store.balance); break;
      case SDINIT:        sprintf (wsbuf, "{\"sdinit\": %d}", SDC_CS!=255); break;
      case GETPLAYERMODE: sprintf (wsbuf, "{\"playermode\": \"%s\"}", config.getMode()==PM_SDCARD?"modesd":"modeweb"); break;
      #ifdef USE_SD
        case CHANGEMODE:    config.changeMode(newConfigMode); return; break;
      #endif
      default:          break;
    }
    if (strlen(wsbuf) > 0) {
      if (clientId == 0) { websocket.textAll(wsbuf); }else{ websocket.text(clientId, wsbuf); }
  #ifdef MQTT_ROOT_TOPIC
      if (clientId == 0 && (request.type == STATION || request.type == ITEM || request.type == TITLE || request.type == MODE)) mqttPublishStatus();
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
    ESP.restart();
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
        display.putRequest(AUDIOINFO);
        return;
      }
      if (strcmp(cmd, "vumeter") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.vumeter, valb);
        display.putRequest(SHOWVUMETER);
        return;
      }
      if (strcmp(cmd, "usespectrum") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.usespectrum, valb);
        // Пересчитать текущее отображение SA/VU без смены режима
        display.putRequest(SHOWVUMETER);
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
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.invertdisplay, valb);
        display.invert();
        // TEMPORARY DEBUG: use invert-display control to toggle INFO mode for LVGL testing. Remove when proper INFO entry/exit exists.
        display.putRequest(NEWMODE, display.mode() == INFO ? PLAYER : INFO);
        return;
      }
      if (strcmp(cmd, "numplaylist") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.numplaylist, valb);
        display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
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
        display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
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
        network.trueWeather=false;
        network.forceWeather = true;
        display.putRequest(SHOWWEATHER);
        return;
      }
      if (strcmp(cmd, "lat") == 0) {
        config.saveValue(config.store.weatherlat, val, 10, false);
        return;
      }
      if (strcmp(cmd, "lon") == 0) {
        config.saveValue(config.store.weatherlon, val, 10, false);
        return;
      }
      if (strcmp(cmd, "key") == 0) {
        config.saveValue(config.store.weatherkey, val, WEATHERKEY_LENGTH);
        network.trueWeather=false;
        display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
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
        // Notify AIPlugin BEFORE applying to store / Уведомляем AIPlugin ДО применения к store
        // (чтобы функция могла сравнить с текущим состоянием / so function can compare with current state)
        if (old_store_enabled != aicfg.enabled) {
          extern AIPlugin aiPluginInstance;
          aiPluginInstance.onAiEnabledChanged(aicfg.enabled);
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
        // Notify AIPlugin BEFORE applying to store if enabled state changed / Уведомляем AIPlugin ДО применения к store если состояние enabled изменилось
        if (was_enabled != aicfg.enabled || old_store_enabled != aicfg.enabled) {
          extern AIPlugin aiPluginInstance;
          aiPluginInstance.onAiEnabledChanged(aicfg.enabled);
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
        // Notify AIPlugin BEFORE applying to store if enabled state changed / Уведомляем AIPlugin ДО применения к store если состояние enabled изменилось
        if (was_enabled != aicfg.enabled || old_store_enabled != aicfg.enabled) {
          extern AIPlugin aiPluginInstance;
          aiPluginInstance.onAiEnabledChanged(aicfg.enabled);
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
        // Notify AIPlugin BEFORE applying to store if enabled state changed / Уведомляем AIPlugin ДО применения к store если состояние enabled изменилось
        if (was_enabled != aicfg.enabled || old_store_enabled != aicfg.enabled) {
          extern AIPlugin aiPluginInstance;
          aiPluginInstance.onAiEnabledChanged(aicfg.enabled);
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
        // Notify AIPlugin BEFORE applying to store if enabled state changed / Уведомляем AIPlugin ДО применения к store если состояние enabled изменилось
        if (was_enabled != aicfg.enabled || old_store_enabled != aicfg.enabled) {
          extern AIPlugin aiPluginInstance;
          aiPluginInstance.onAiEnabledChanged(aicfg.enabled);
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
        // Notify AIPlugin BEFORE applying to store if enabled state changed / Уведомляем AIPlugin ДО применения к store если состояние enabled изменилось
        if (was_enabled != aicfg.enabled || old_store_enabled != aicfg.enabled) {
          extern AIPlugin aiPluginInstance;
          aiPluginInstance.onAiEnabledChanged(aicfg.enabled);
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
          display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
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
          display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
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
          network.trueWeather=false;
          display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
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
          
          // Notify AIPlugin BEFORE applying to store if state changed / Уведомляем AIPlugin ДО применения к store если состояние изменилось
          if (old_store_enabled != false) {
            extern AIPlugin aiPluginInstance;
            aiPluginInstance.onAiEnabledChanged(false);
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
  nsRequestParams_t nsrequest;
  nsrequest.type = request;
  nsrequest.clientId = clientId;
  xQueueSend(nsQueue, &nsrequest, NSQ_SEND_DELAY);
}

void NetServer::resetQueue(){
  if(nsQueue!=NULL) xQueueReset(nsQueue);
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

void handleUploadWeb(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  DBGVB("File: %s, size:%u bytes, index: %u, final: %s\n", filename.c_str(), len, index, final?"true":"false");
  
  // Handle AI prompt upload / Обработка загрузки AI промпта
  // Accept any .txt file for AI prompt (filename ignored, always saved as /ai/ai_prompt.txt)
  // Принимаем любой .txt файл для AI prompt (имя файла игнорируется, всегда сохраняется как /ai/ai_prompt.txt)
  // Atomic upload: write to /ai/ai_prompt.tmp, replace only on success
  // Атомарная загрузка: пишем в /ai/ai_prompt.tmp, заменяем только при успехе
  bool is_prompt_file = filename.endsWith(".txt") && filename != "playlist.csv" && filename != "wifi.csv";
  
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
  if (!index) {
    String spath = "/www/";
    if(filename=="playlist.csv" || filename=="wifi.csv") spath = "/data/";
    request->_tempFile = LittleFS.open(spath + filename , "w");
  }
  if (len) {
    request->_tempFile.write(data, len);
  }
  if (final) {
    request->_tempFile.close();
    if(filename=="playlist.csv") config.indexPlaylist();
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
    if (strcmp(request->url().c_str(), "/update") == 0 || strcmp(request->url().c_str(), "/settings") == 0 || strcmp(request->url().c_str(), "/ir") == 0) {
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
      config.store.volume = v;
      player.setVol(v);
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
