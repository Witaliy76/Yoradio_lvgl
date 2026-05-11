#include "network.h"
#include "display.h"
#include "options.h"
#include "config.h"
#include "telnet.h"
#include "netserver.h"
#include "player.h"
#include "mqtt.h"
#include "freertos/semphr.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/tcpip.h"

#ifndef WIFI_ATTEMPTS
  #define WIFI_ATTEMPTS  16
#endif

MyNetwork network;

TaskHandle_t syncTaskHandle;
//TaskHandle_t reconnectTaskHandle;

bool getWeather(char *wstr);
void doSync(void * pvParameters);

static constexpr uint32_t WEATHER_FIRST_SYNC_GRACE_MS = 30000;
static uint32_t s_weather_ready_at_ms = 0;

namespace {
SemaphoreHandle_t s_dns_mutex = nullptr;
SemaphoreHandle_t s_dns_done = nullptr;

struct DnsResolveState {
  volatile bool waiting = false;
  volatile uint32_t activeGeneration = 0;
  err_t err = ERR_VAL;
  ip_addr_t addr{};
};

DnsResolveState s_dns_state;
uint32_t s_dns_generation = 0;

SemaphoreHandle_t dnsMutex() {
  if (!s_dns_mutex) {
    s_dns_mutex = xSemaphoreCreateMutex();
  }
  return s_dns_mutex;
}

SemaphoreHandle_t dnsDoneSemaphore() {
  if (!s_dns_done) {
    s_dns_done = xSemaphoreCreateBinary();
  }
  return s_dns_done;
}

uint32_t nextDnsGeneration() {
  s_dns_generation++;
  if (s_dns_generation == 0) {
    s_dns_generation = 1;
  }
  return s_dns_generation;
}

void dnsFoundCallback(const char*, const ip_addr_t* ipaddr, void* arg) {
  const uint32_t callbackGeneration = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
  if (callbackGeneration == 0) return;
  if (!s_dns_state.waiting || callbackGeneration != s_dns_state.activeGeneration) {
    return;
  }
  if (ipaddr && IP_IS_V4(ipaddr)) {
    s_dns_state.addr = *ipaddr;
    s_dns_state.err = ERR_OK;
  } else {
    s_dns_state.err = ERR_VAL;
  }
  s_dns_state.waiting = false;
  if (s_dns_done) {
    xSemaphoreGive(s_dns_done);
  }
}

bool ipAddrToArduino(const ip_addr_t& src, IPAddress& out) {
  if (!IP_IS_V4_VAL(src)) return false;
  const uint32_t raw = ip_addr_get_ip4_u32(&src);
  if (raw == 0U) return false;
  out = IPAddress(raw);
  return true;
}
} // namespace

bool networkResolveHostForConnect(const char* host, IPAddress& out, uint32_t timeoutMs) {
  out = static_cast<uint32_t>(0);
  if (!host || host[0] == '\0') return false;

  IPAddress literal;
  if (literal.fromString(host)) {
    out = literal;
    return true;
  }

  if (network.status != CONNECTED || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  SemaphoreHandle_t mutex = dnsMutex();
  SemaphoreHandle_t done = dnsDoneSemaphore();
  if (!mutex || !done) return false;

  const TickType_t waitTicks = pdMS_TO_TICKS(timeoutMs);
  if (xSemaphoreTake(mutex, waitTicks) != pdTRUE) {
    Serial.printf("[DNS] resolve busy: %s\n", host);
    return false;
  }

  while (xSemaphoreTake(done, 0) == pdTRUE) {}

  const uint32_t generation = nextDnsGeneration();

  s_dns_state.waiting = true;
  s_dns_state.activeGeneration = generation;
  s_dns_state.err = ERR_INPROGRESS;
  ip_addr_set_zero_ip4(&s_dns_state.addr);

  ip_addr_t resolved{};
  err_t err;
#if LWIP_TCPIP_CORE_LOCKING
  LOCK_TCPIP_CORE();
#endif
  // Start raw lwIP DNS under the TCPIP core lock; wait for async answer after unlocking.
  // Запуск raw DNS под core-lock; ожидание ответа уже без блокировки TCPIP core.
  err = dns_gethostbyname_addrtype(host, &resolved, dnsFoundCallback,
                                   reinterpret_cast<void*>(static_cast<uintptr_t>(generation)),
                                   LWIP_DNS_ADDRTYPE_IPV4);
#if LWIP_TCPIP_CORE_LOCKING
  UNLOCK_TCPIP_CORE();
#endif

  bool ok = false;
  if (err == ERR_OK) {
    s_dns_state.waiting = false;
    s_dns_state.activeGeneration = 0;
    ok = ipAddrToArduino(resolved, out);
  } else if (err == ERR_INPROGRESS) {
    if (xSemaphoreTake(done, waitTicks) == pdTRUE && s_dns_state.err == ERR_OK) {
      ok = ipAddrToArduino(s_dns_state.addr, out);
      s_dns_state.activeGeneration = 0;
    } else {
      // Timeout: retire this generation; late callback is ignored by generation check.
      // Timeout: поколение помечено stale; поздний callback не тронет новый resolve.
      s_dns_state.waiting = false;
      s_dns_state.activeGeneration = 0;
    }
  } else {
    s_dns_state.waiting = false;
    s_dns_state.activeGeneration = 0;
  }

  if (!ok) {
    Serial.printf("[DNS] resolve failed: %s err=%d\n", host, (int)(err == ERR_INPROGRESS ? s_dns_state.err : err));
  }
  xSemaphoreGive(mutex);
  return ok;
}

static void markWeatherReadyAfterConnect() {
  s_weather_ready_at_ms = millis() + WEATHER_FIRST_SYNC_GRACE_MS;
}

static bool isWeatherEnabledForSync() {
  return network.weatherBuf != nullptr &&
         config.store.showweather &&
         strlen(config.store.weatherkey) != 0 &&
         network.status == CONNECTED;
}

static bool isWeatherGraceElapsed() {
  return s_weather_ready_at_ms != 0 &&
         (int32_t)(millis() - s_weather_ready_at_ms) >= 0;
}

void ticks() {
  if(!display.ready()) return; //waiting for SD is ready
  pm.on_ticker();
  static const uint16_t weatherSyncInterval=1800;
  //static const uint16_t weatherSyncIntervalFail=10;
#if RTCSUPPORTED
  static const uint32_t timeSyncInterval=86400;
  static uint32_t timeSyncTicks = 0;
#else
  static const uint16_t timeSyncInterval=3600;
  static uint16_t timeSyncTicks = 0;
#endif
  static uint16_t weatherSyncTicks = 0;
  static bool divrssi;
  timeSyncTicks++;
  weatherSyncTicks++;
  divrssi = !divrssi;
  if(network.status == CONNECTED){
    const bool weatherEnabled = isWeatherEnabledForSync();
    if (!weatherEnabled) {
      network.forceWeather = false;
    }
    const bool weatherSyncReady = network.forceWeather && weatherEnabled && isWeatherGraceElapsed();
    if(network.forceTimeSync || weatherSyncReady){
      xTaskCreatePinnedToCore(doSync, "doSync", 1024 * 4, NULL, 0, &syncTaskHandle, 0);
    }
    if(timeSyncTicks >= timeSyncInterval){
      timeSyncTicks=0;
      network.forceTimeSync = true;
    }
    if(weatherSyncTicks >= weatherSyncInterval){
      weatherSyncTicks=0;
      if (weatherEnabled) {
        network.forceWeather = true;
      } else {
        network.forceWeather = false;
      }
    }
  }
#ifndef DSP_LCD
  if(config.store.screensaverEnabled && display.mode()==PLAYER && !player.isRunning()){
    config.screensaverTicks++;
    if(config.screensaverTicks > config.store.screensaverTimeout+SCREENSAVERSTARTUPDELAY){
      if(config.store.screensaverBlank){
        display.putRequest(NEWMODE, SCREENBLANK);
      }else{
        display.putRequest(NEWMODE, SCREENSAVER);
      }
    }
  }
  if(config.store.screensaverPlayingEnabled && display.mode()==PLAYER && player.isRunning()){
    config.screensaverPlayingTicks++;
    if(config.screensaverPlayingTicks > config.store.screensaverPlayingTimeout+SCREENSAVERSTARTUPDELAY){
      if(config.store.screensaverPlayingBlank){
        display.putRequest(NEWMODE, SCREENBLANK);
      }else{
        display.putRequest(NEWMODE, SCREENSAVER);
      }
    }
  }
#endif
#if RTCSUPPORTED
  if(config.isRTCFound()){
    rtc.getTime(&network.timeinfo);
    mktime(&network.timeinfo);
    display.putRequest(CLOCK);
  }
#else
  if(network.timeinfo.tm_year>100 || network.status == SDREADY) {
    network.timeinfo.tm_sec++;
    mktime(&network.timeinfo);
    display.putRequest(CLOCK);
  }
#endif
  if(player.isRunning() && config.getMode()==PM_SDCARD) netserver.requestOnChange(SDPOS, 0);
  
  
  if(divrssi) {
    if(network.status == CONNECTED){
      netserver.setRSSI(WiFi.RSSI());
      netserver.requestOnChange(NRSSI, 0);
      display.putRequest(DSPRSSI, netserver.getRSSI());
    }
#ifdef USE_SD
    if(display.mode()!=SDCHANGE) player.sendCommand({PR_CHECKSD, 0});
#endif
    player.sendCommand({PR_VUTONUS, 0});
  }
  
  // Периодическая обработка результатов AI (каждые ~300ms через rate limiting в _pumpResults)
  // Periodic AI result processing (~300ms via rate limiting in _pumpResults)
  // Объявление extern будет в начале файла если понадобится, или через forward declaration
  // extern declaration will be at file start if needed, or via forward declaration
}

void MyNetwork::WiFiReconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  network.beginReconnect = false;
  network.runtimeReconnectSuspendedForSetup = false;
  markWeatherReadyAfterConnect();
  player.lockOutput = false;
  delay(100);
  display.putRequest(NEWMODE, PLAYER);
  if(config.getMode()==PM_SDCARD) {
    network.status=CONNECTED;
    display.putRequest(NEWIP, 0);
  }else{
    display.putRequest(NEWMODE, PLAYER);
    if (network.lostPlaying) player.sendCommand({PR_PLAY, config.lastStation()});
  }
  #ifdef MQTT_ROOT_TOPIC
    connectToMqtt();
  #endif
}

void MyNetwork::WiFiLostConnection(WiFiEvent_t event, WiFiEventInfo_t info){
  if (network.runtimeReconnectSuspendedForSetup) {
    // S6V9G: no LOST/reconnect/player while LVGL Wi-Fi Setup owns radio after runtime Recovery suspend.
    // S6V9G: без LOST/reconnect — Setup владеет радио после suspend (без Serial spam).
    return;
  }
  if (!network.beginReconnect) {
    Serial.printf("Lost connection, reconnecting to %s...\n", config.ssids[config.store.lastSSID - 1].ssid);
    if (config.getMode() == PM_SDCARD) {
      network.status = SDREADY;
      display.putRequest(NEWIP, 0);
    } else {
      network.lostPlaying = player.isRunning();
      if (network.lostPlaying) {
        player.lockOutput = true;
        player.sendCommand({PR_STOP, 0});
      }
      display.putRequest(NEWMODE, LOST);
    }
    network.beginReconnect = true;
    // S6V9D: only first STA_DISCONNECTED triggers WiFi.reconnect — avoids ESP-IDF "sta is connecting" storm.
    // S6V9D: только первый disconnect вызывает reconnect; повторные события не дергают WiFi.reconnect().
    WiFi.reconnect();
  }
}

bool MyNetwork::wifiBegin(bool silent){
  uint8_t ls = (config.store.lastSSID == 0 || config.store.lastSSID > config.ssidsCount) ? 0 : config.store.lastSSID - 1;
  uint8_t startedls = ls;
  uint8_t errcnt = 0;
  bool attempted = false;
  WiFi.mode(WIFI_STA);
  // На время перебора сетей отключим авто-переподключение,
  // чтобы избегать состояния ESP_ERR_WIFI_STATE при смене SSID
  WiFi.setAutoReconnect(false);
  /*
  char buf[MDNS_LENGTH];
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  if(strlen(config.store.mdnsname)>0){
    WiFi.setHostname(config.store.mdnsname);
  }else{
    snprintf(buf, MDNS_LENGTH, "yoradio-%x", config.getChipId());
    WiFi.setHostname(buf);
  }
  */
  while (true) {
    if(!silent){
      Serial.printf("##[BOOT]#\tAttempt to connect to %s\n", config.ssids[ls].ssid);
      Serial.print("##[BOOT]#\t");
      display.putRequest(BOOTSTRING, ls);
    }
    if (attempted) {
      // S6V10E: boot saved-SSID iteration must not fully stop/deinit Wi-Fi between SSIDs.
      // WiFi.disconnect(true) churns esp_netif registration and can hit ESP_ERR_WIFI_STOP_STATE (12308).
      // S6V10E: при переборе SSID на boot не гасим Wi-Fi стек полностью между попытками.
      // disconnect(true) пересоздаёт netif и может дать ESP_ERR_WIFI_STOP_STATE (12308).
      WiFi.disconnect(false);
      delay(150);
    }
    attempted = true;
    WiFi.begin(config.ssids[ls].ssid, config.ssids[ls].password);
    while (WiFi.status() != WL_CONNECTED) {
      if(!silent) Serial.print(".");
      delay(500);
      if(REAL_LEDBUILTIN!=255 && !silent) digitalWrite(REAL_LEDBUILTIN, !digitalRead(REAL_LEDBUILTIN));
      errcnt++;
      if (errcnt > WIFI_ATTEMPTS) {
        errcnt = 0;
        // Переходим к следующему SSID
        ls++;
        if (ls > config.ssidsCount - 1) ls = 0;
        if(!silent) Serial.println();
        // Обновим надпись немедленно для следующей сети
        if(!silent){
          Serial.printf("##[BOOT]#\tSwitching to %s\n", config.ssids[ls].ssid);
          Serial.print("##[BOOT]#\t");
          display.putRequest(BOOTSTRING, ls);
        }
        break;
      }
    }
    if (WiFi.status() != WL_CONNECTED && ls == startedls) {
      // Вернём поведение авто‑reconnect в исходное состояние
      WiFi.setAutoReconnect(true);
      return false; break;
    }
    if (WiFi.status() == WL_CONNECTED) {
      config.setLastSSID(ls + 1);
      // После успешного подключения включаем авто‑reconnect
      WiFi.setAutoReconnect(true);
      return true; break;
    }
  }
  return false;
}

void searchWiFi(void * pvParameters){
  if(!network.wifiBegin(true)){
    delay(10000);
    xTaskCreatePinnedToCore(searchWiFi, "searchWiFi", 1024 * 4, NULL, 0, NULL, 0);
  }else{
    network.status = CONNECTED;
    netserver.begin(true);
    telnet.begin(true);
    network.setWifiParams();
    markWeatherReadyAfterConnect();
    network.forceWeather = isWeatherEnabledForSync();
    display.putRequest(NEWIP, 0);
  }
  vTaskDelete( NULL );
}

#define DBGAP false

void MyNetwork::begin() {
  BOOTLOG("network.begin");
  runtimeReconnectSuspendedForSetup = false;
  config.initNetwork();
  ctimer.detach();
  forceTimeSync = true;
  forceWeather = false;
  if (config.ssidsCount == 0 || DBGAP) {
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
    if (!DBGAP) {
      // S6V9C: LVGL Recovery path — no automatic AP on boot; AP starts only on Hotspot page.
      // S6V9C: LVGL — не поднимаем AP при старте; AP только при входе на Hotspot page.
      Serial.println("[Network] Wi-Fi Recovery needed; AP not started");
      status = FAILED;
      Serial.println("##[BOOT]#\tdone");
      return;
    }
#endif
    raiseSoftAP();
    return;
  }
  if(config.getMode()!=PM_SDCARD){
    if(!wifiBegin()){
#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
      // S6V9C: LVGL Recovery path — no automatic AP on failed STA; AP starts only on Hotspot page.
      // S6V9C: LVGL — не поднимаем AP при неудаче STA; AP только при входе на Hotspot page.
      Serial.println("[Network] Wi-Fi Recovery needed; AP not started");
      status = FAILED;
      Serial.println("##[BOOT]#\tdone");
      return;
#else
      raiseSoftAP();
      Serial.println("##[BOOT]#\tdone");
      return;
#endif
    }
    Serial.println(".");
    status = CONNECTED;
    setWifiParams();
    markWeatherReadyAfterConnect();
    forceWeather = isWeatherEnabledForSync();
  }else{
    status = SDREADY;
    xTaskCreatePinnedToCore(searchWiFi, "searchWiFi", 1024 * 4, NULL, 0, NULL, 0);
  }
  
  Serial.println("##[BOOT]#\tdone");
  if(REAL_LEDBUILTIN!=255) digitalWrite(REAL_LEDBUILTIN, LOW);
  
#if RTCSUPPORTED
  if(config.isRTCFound()){
    rtc.getTime(&network.timeinfo);
    mktime(&network.timeinfo);
    display.putRequest(CLOCK);
  }
#endif
  ctimer.attach(1, ticks);
  if (network_on_connect) network_on_connect();
  pm.on_connect();
}

void MyNetwork::setWifiParams(){
  WiFi.setSleep(false);
  WiFi.onEvent(WiFiReconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiLostConnection, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  weatherBuf=NULL;
  trueWeather = false;
  #if (DSP_MODEL!=DSP_DUMMY || defined(USE_NEXTION)) && !defined(HIDE_WEATHER)
    weatherBuf = (char *) malloc(sizeof(char) * WEATHER_STRING_L);
    memset(weatherBuf, 0, WEATHER_STRING_L);
  #endif
  if(strlen(config.store.sntp1)>0 && strlen(config.store.sntp2)>0){
    configTime(config.store.tzHour * 3600 + config.store.tzMin * 60, config.getTimezoneOffset(), config.store.sntp1, config.store.sntp2);
  }else if(strlen(config.store.sntp1)>0){
    configTime(config.store.tzHour * 3600 + config.store.tzMin * 60, config.getTimezoneOffset(), config.store.sntp1);
  }
}

void MyNetwork::requestTimeSync(bool withTelnetOutput, uint8_t clientId) {
  if (withTelnetOutput) {
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    if (config.store.tzHour < 0) {
      telnet.printf(clientId, "##SYS.DATE#: %s%03d:%02d\n> ", timeStringBuff, config.store.tzHour, config.store.tzMin);
    } else {
      telnet.printf(clientId, "##SYS.DATE#: %s+%02d:%02d\n> ", timeStringBuff, config.store.tzHour, config.store.tzMin);
    }
  }
}

void rebootTime() {
  ESP.restart();
}

void MyNetwork::raiseSoftAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPassword);
  Serial.println("##[BOOT]#");
  BOOTLOG("************************************************");
  BOOTLOG("Running in AP mode");
  BOOTLOG("Connect to AP %s with password %s", apSsid, apPassword);
  BOOTLOG("and go to http:/192.168.4.1/ to configure");
  BOOTLOG("************************************************");
  status = SOFT_AP;
  if(config.store.softapdelay>0)
    rtimer.once(config.store.softapdelay*60, rebootTime);
}

void MyNetwork::recoveryEnsureSoftAP() {
  const wifi_mode_t wm = WiFi.getMode();
  const bool        ap_mode = (wm == WIFI_AP || wm == WIFI_AP_STA);
  if (ap_mode) {
    const IPAddress ap_ip = WiFi.softAPIP();
    if (static_cast<uint32_t>(ap_ip) != 0U) {
      if (status != SOFT_AP) {
        status = SOFT_AP;
      }
      // Idempotent no-op: avoid Serial spam when UI re-enters Hotspot / идемпотентно, без шума в Serial.
      return;
    }
  }
  Serial.println("[Network] Open Hotspot mode active");
  raiseSoftAP();
}

void MyNetwork::recoveryStopSoftAP() {
  // S6V9C: called when leaving LVGL Hotspot page (Back button) — stop SoftAP, cancel softapdelay.
  // S6V9C: вызывается при выходе с LVGL Hotspot page — гасим AP, отменяем softapdelay.
  rtimer.detach();  // cancel softapdelay reboot / отмена таймера перезагрузки
  WiFi.softAPdisconnect(true);
  // S6V9F: do not call WiFi.mode(WIFI_STA) here — redundant with softAPdisconnect(true) and can churn esp_netif
  // (mDNS/Async stability); scan/connect paths set STA/AP_STA as needed. / Без лишнего mode(STA) — меньше netif-шторма.
  Serial.println("[Network] Open Hotspot mode stopped");
  if (WiFi.status() == WL_CONNECTED) {
    status = CONNECTED;
  } else {
    status = FAILED;
  }
}

void MyNetwork::recoverySuspendReconnectForSetup() {
  // S6V9E: only from Display LOST→Recovery escalation (≥60s); not boot-fail, not first LOST tick.
  // S6V9E: только из эскалации Display LOST→Recovery; не boot-fail, не первые 60s LOST.
  Serial.println("[Network] Runtime reconnect suspended for Wi-Fi Recovery");
  WiFi.setAutoReconnect(false);
  beginReconnect = false;
  runtimeReconnectSuspendedForSetup = true;
  // STA-only for Recovery scan/connect; does not start SoftAP (S6V9C Hotspot path unchanged).
  // Только STA для скана/коннекта; SoftAP не поднимаем.
  WiFi.mode(WIFI_STA);
}

void MyNetwork::requestWeatherSync(){
  display.putRequest(NEWWEATHER);
}


void doSync( void * pvParameters ) {
  static uint8_t tsFailCnt = 0;
  //static uint8_t wsFailCnt = 0;
  if(network.forceTimeSync){
    network.forceTimeSync = false;
    if(getLocalTime(&network.timeinfo)){
      tsFailCnt = 0;
      network.forceTimeSync = false;
      mktime(&network.timeinfo);
      display.putRequest(CLOCK);
      network.requestTimeSync(true);
      #if RTCSUPPORTED
        if (config.isRTCFound()) rtc.setTime(&network.timeinfo);
      #endif
    }else{
      if(tsFailCnt<4){
        network.forceTimeSync = true;
        tsFailCnt++;
      }else{
        network.forceTimeSync = false;
        tsFailCnt=0;
      }
    }
  }
  if (!isWeatherEnabledForSync()) {
    // S6V10G: disabled weather must not reach DNS/NetworkClient path; keep time sync independent.
    // S6V10G: при выключенной погоде не заходим в DNS/NetworkClient; синхронизация времени отдельно.
    network.forceWeather = false;
  } else if (!isWeatherGraceElapsed()) {
    // S6V10H: keep first weather request pending until boot/connect pressure is over.
    // S6V10H: первый запрос погоды ждёт grace, чтобы не пересекаться с ранним audio/Main preload.
    network.forceWeather = true;
  } else if(network.forceWeather){
    network.forceWeather = false;
    network.trueWeather=getWeather(network.weatherBuf);
  }
  vTaskDelete( NULL );
}

bool getWeather(char *wstr) {
#if (DSP_MODEL!=DSP_DUMMY || defined(USE_NEXTION)) && !defined(HIDE_WEATHER)
  WiFiClient client;
  const char* host  = "api.openweathermap.org";

  IPAddress serverIP;
  if (!networkResolveHostForConnect(host, serverIP)) {
    Serial.println("##WEATHER###: DNS resolve failed");
    return false;
  }
  if (!client.connect(serverIP, 80)) {
    Serial.println("##WEATHER###: connection  failed");
    return false;
  }
  char httpget[250] = {0};
  sprintf(httpget, "GET /data/2.5/weather?lat=%s&lon=%s&units=%s&lang=%s&appid=%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", config.store.weatherlat, config.store.weatherlon, weatherUnits, weatherLang, config.store.weatherkey, host);
  client.print(httpget);
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 2000UL) {
      Serial.println("##WEATHER###: client available timeout !");
      client.stop();
      return false;
    }
  }
  timeout = millis();
  String line = "";
  if (client.connected()) {
    while (client.available())
    {
      line = client.readStringUntil('\n');
      if (strstr(line.c_str(), "\"temp\"") != NULL) {
        client.stop();
        break;
      }
      if ((millis() - timeout) > 500)
      {
        client.stop();
        Serial.println("##WEATHER###: client read timeout !");
        return false;
      }
    }
  }
  if (strstr(line.c_str(), "\"temp\"") == NULL) {
    Serial.println("##WEATHER###: weather not found !");
    return false;
  }

//		  Serial.printf("## OPENWEATHERMAP ###: *\n%s,\n*\n", line.c_str());

  char *tmpe;
  char *tmps;
  char *tmpc;
  int pressi, deg, gusti;
  #ifndef GRND_HEIGHT
    #define GRND_HEIGHT  0
  #endif
  int g_height = (float)(GRND_HEIGHT / 11);
  const char* cursor = line.c_str();
  char desc[120], temp[20], hum[20], press[20], icon[5], gust[20], porv[10], stanc[50];

  tmps = strstr(cursor, "\"description\":\"");
  if (tmps == NULL) { Serial.println("##WEATHER###: description not found !"); return false;}
  tmps += 15;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: description content not found !"); return false;}
  strlcpy(desc, tmps, tmpe - tmps + 1);
  cursor = tmps;
//    Serial.printf("#CONTROL#: descr.: %s,\n", desc);

  // "sky clear","icon":"01d"}],
  tmps = strstr(cursor, "\"icon\":\"");
  if (tmps == NULL) { Serial.println("##WEATHER###: icon not found !"); return false;}
  tmps += 8;
  tmpe = strstr(tmps, "\"}");
  if (tmpe == NULL) { Serial.println("##WEATHER###: icon content not found !"); return false;}
  strlcpy(icon, tmps, tmpe - tmps + 1);
  cursor = tmps;

  tmps = strstr(cursor, "\"temp\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: temp not found !"); return false;}
  tmps += 7;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: temp content not found !"); return false;}
  strlcpy(temp, tmps, tmpe - tmps + 1);
  cursor = tmps;
  float tempf = atof(temp);
//    Serial.printf("#CONTROL#: temp: %+.1fC\n", tempf);

  tmps = strstr(cursor, "\"feels_like\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: feels_like not found !"); return false;}
  tmps += 13;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: feels_like content not found !"); return false;}
  strlcpy(temp, tmps, tmpe - tmps + 1);
  cursor = tmps;
  float tempfl = atof(temp);
//  (void)tempfl;						// ?
//    Serial.printf("#CONTROL#: feels like: %+.0fC\n", tempfl);

  tmps = strstr(cursor, "\"pressure\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: pressure not found !"); return false;}
  tmps += 11;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: pressure content not found !"); return false;}
  strlcpy(press, tmps, tmpe - tmps + 1);
  cursor = tmps;
      pressi = (float)atoi(press) / 1.333 - g_height;		// ������� � ��.��.��., ���� � ����� ����� pressi �������� (-21) �� ���. ���������
//      Serial.printf("#CONTROL#: pres.: %d mmHg\n", pressi);

  tmps = strstr(cursor, "humidity\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: humidity not found !"); return false;}
  tmps += 10;
  tmpe = strstr(tmps, ",\"");
  tmpc = strstr(tmps, "},");
  if (tmpe == NULL) { Serial.println("##WEATHER###: humidity not found !"); return false;}
  cursor = tmps;
  strlcpy(hum, tmps, tmpe - tmps + (tmpc>tmpe?1:(tmpc - tmpe +1)));
//      Serial.printf("#CONTROL#: humidity: %s %%\n", hum);

  tmps = strstr(cursor, "\"grnd_level\":");
  bool grnd_level_pr = (tmps != NULL);
  if(grnd_level_pr){
    tmps += 13;
    tmpe = strstr(tmps, "},");
    tmpc = strstr(tmps, ",\"");						// ��� ����� �� [},]
    if (tmpe == NULL) { Serial.println("##WEATHER###: grnd_level not found ! Use pressure");}
    strlcpy(press, tmps, tmpe - tmps + (tmpc>tmpe?1:(tmpc - tmpe +1)));	// ������� � press ������ ������ (press="991")
    cursor = tmps;
    pressi = (float)atoi(press) / 1.333;			// ������������� � ����� �����, �������� � ��.��.��. (pressi=743)
 			 }
//      Serial.printf("#CONTROL#: press. grnd_level: %d mmHg\n", pressi);

  tmps = strstr(cursor, "\"speed\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: wind speed not found !"); return false;}
  tmps += 8;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: wind speed content not found !"); return false;}
  strlcpy(temp, tmps, tmpe - tmps + 1);
  cursor = tmps;
  float wind_speed = atof(temp);
//  (void)wind_speed;					// ?
//    Serial.printf("#CONTROL#: wind: %.0f m/s\n", wind_speed);
  
  tmps = strstr(cursor, "\"deg\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: wind deg not found !"); return false;}
  tmps += 6;
  tmpe = strstr(tmps, ",\"");
  tmpc = strstr(tmps, "},");				// ��� ����� ��[},]
  if (tmpe == NULL) { Serial.println("## WEATHER ###: deg content not found !"); return false;}
  strlcpy(temp, tmps, tmpe - tmps + (tmpc>tmpe?1:(tmpc - tmpe +1)));	// ������� � temp ������ ������ (temp="316")
  cursor = tmps;
      deg = atof(temp);
  int wind_deg = atof(temp)/22.5;		// ������. � ����� ����� � �������� � ��������� (wind_deg=14) 
//  if(wind_deg<0) wind_deg = 16+wind_deg;			//������������� �� ������
//    Serial.printf("#CONTROL#: wind deg: %d rumbs (*%d*)\n", wind_deg, deg);
  
  		// ��������� ������� ["gust":13.09}] � ��������� ��� ����� ��������� � ������ gust
  tmps = strstr(cursor, "\"gust\":");			// ����� ["gust":] 7
  strlcpy(gust, const_getWeather, sizeof(gust));	// ������� � gust ("")
  if (tmps == NULL) { Serial.println("## WEATHER ###: gust not found !\n");}
  else {
	  tmps += 7;						// �������� 7
	  tmpe = strstr(tmps, "},");				// �� [},]
	  if (tmpe == NULL) { Serial.println("## WEATHER ###: gust content not found !");}
	  else {
		  strlcpy(temp, tmps, tmpe - tmps + 1);	// ������� � temp ��������� ������ (temp="13.09")
		      gusti = (float)atoi(temp);		// ������������� � ����� ����� (gusti=13)
		  if (gusti == 0) { Serial.println("## WEATHER ###: gust content is 0 !");}
		  else {
			  strlcpy(gust, prv, sizeof(gust));	// ������� � gust ��������� *prv (", ������ ")
			  itoa(gusti, porv, 10);				// ������������� gusti � ��������� ������ (porv="13")
			  strlcat(gust, porv, sizeof(gust));		// �������� � gust ��������� ������ porv (", ������ 13")
			  }
		  cursor = tmps;
		  }
	 }
//    Serial.printf("#CONTROL#: gusts: %s m/s\n", gust);

  tmps = strstr(cursor, "\"name\":\"");
  if (tmps == NULL) { Serial.println("##WEATHER###: name station not found !"); return false;}
  tmps += 8;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: name station not found !"); return false;}
  strlcpy(stanc, tmps, tmpe - tmps + 1);		// ������� � stanc ������������
//    Serial.printf("#CONTROL#: station: %s\n", stanc);
  
  #ifdef USE_NEXTION
    nextion.putcmdf("press_txt.txt=\"%dmm\"", pressi);
    nextion.putcmdf("hum_txt.txt=\"%d%%\"", atoi(hum));
    char cmd[30];
    snprintf(cmd, sizeof(cmd)-1,"temp_txt.txt=\"%.1f\"", tempf);
    nextion.putcmd(cmd);
    int iconofset;
    if(strstr(icon,"01")!=NULL)      iconofset = 0;
    else if(strstr(icon,"02")!=NULL) iconofset = 1;
    else if(strstr(icon,"03")!=NULL) iconofset = 2;
    else if(strstr(icon,"04")!=NULL) iconofset = 3;
    else if(strstr(icon,"09")!=NULL) iconofset = 4;
    else if(strstr(icon,"10")!=NULL) iconofset = 5;
    else if(strstr(icon,"11")!=NULL) iconofset = 6;
    else if(strstr(icon,"13")!=NULL) iconofset = 7;
    else if(strstr(icon,"50")!=NULL) iconofset = 8;
    else                             iconofset = 9;
    nextion.putcmd("cond_img.pic", 50+iconofset);
    nextion.weatherVisible(1);
  #endif
  
  Serial.printf("##WEATHER###: descr.: %s, temp.: %+.1f*C (feels like %+.0f*C) \007 press.: %d mm \007 hum.: %s%% \007 wind %s %.0f%s m/s (st. %s)\n", desc, tempf, tempfl, pressi, hum, wind[wind_deg], wind_speed, gust, stanc);
//  Serial.printf("##WEATHER###: description: %s, temp:%+.1f C, pressure:%dmmHg, humidity:%s%%\n", desc, tempf, pressi, hum);
  strlcpy(network.weatherOwmIcon, icon, sizeof(network.weatherOwmIcon));
  network.weatherLastTempC = tempf;
  network.weatherGlanceValid = true;
  #ifdef WEATHER_FMT_SHORT
  sprintf(wstr, weatherFmt, tempf, pressi, hum);
  #else
    #if EXT_WEATHER
      sprintf(wstr, weatherFmt, desc, tempf, tempfl, pressi, hum, wind[wind_deg], wind_speed, gust, stanc);
    #else
      sprintf(wstr, weatherFmt, desc, tempf, pressi, hum);
    #endif
  #endif
  network.requestWeatherSync();
  return true;
#endif // if (DSP_MODEL!=DSP_DUMMY || defined(USE_NEXTION)) && !defined(HIDE_WEATHER)
  return false;
}
