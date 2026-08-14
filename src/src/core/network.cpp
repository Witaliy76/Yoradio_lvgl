#include "network.h"
#include "../ai/ai_subsystem.h"
#include "display.h"
#include "options.h"
#include "config.h"
#include "telnet.h"
#include "netserver.h"
#include "player.h"
#include "mqtt.h"
#include "weather_fetch.h"       // Weather W1: forecast fetch hook / хук прогноза
#include "weather_state.h"       // W-R3: fetch lifecycle metadata / метаданные цикла fetch
#include "net_dns_resolver.h"     // HF-W-DNS: generic selected-DNS resolver / выбранный DNS-резолвер
#include "weather_edge_session.h" // HF-W-DNS: per-cycle edge tracking / отслеживание рёбер
#include "../i18n/i18n.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/tcpip.h"
#include <cerrno>
#include <cstring>  // strerror — REQ_DIAG connect errno only / только под REQ_DIAG

#ifndef YORADIO_WEATHER_REQ_DIAG
#define YORADIO_WEATHER_REQ_DIAG 0
#endif

// W-R1C.3A: doSync stack high-water diagnostics — off by default.
// W-R1C.3A: диагностика high-water стека doSync — выключена по умолчанию.
#ifndef YORADIO_WEATHER_STACK_DIAG
#define YORADIO_WEATHER_STACK_DIAG 0
#endif

#if YORADIO_WEATHER_STACK_DIAG
// Observation-only: ESP-IDF returns min free stack in bytes (not vanilla FreeRTOS words).
// Только наблюдение: ESP-IDF возвращает min free stack в байтах.
static void logDoSyncStackUsage(const char* mode, const char* phase) {
  const configSTACK_DEPTH_TYPE min_free_stack = uxTaskGetStackHighWaterMark2(nullptr);
  const uint32_t configured = kDoSyncTaskStackBytes;
  const uint32_t min_free = static_cast<uint32_t>(min_free_stack);
  const uint32_t max_used = (min_free <= configured) ? (configured - min_free) : 0u;
  Serial.printf("[WEATHER_STACK] mode=%s phase=%s configured=%u min_free=%u max_used=%u\n",
                mode, phase, configured, min_free, max_used);
}
#endif

#ifndef WIFI_ATTEMPTS
  #define WIFI_ATTEMPTS  16
#endif

MyNetwork network;

TaskHandle_t syncTaskHandle;
//TaskHandle_t reconnectTaskHandle;

// HF-W-DNS: edge-session-aware getWeather — called only from doSync.
bool getWeather(WeatherEdgeSession& edgeSession);
void doSync(void * pvParameters);

// A4.0: file-scope current-conditions staging — populated by getWeather(), consumed by
// runWeatherForecastFetch() in the same doSync cycle. Single-threaded by contract.
// A4.0: staging текущих условий — заполняется getWeather(), потребляется runWeatherForecastFetch().
// Однопоточно по контракту (только Core0/doSync).
static WeatherTrueCurrent s_current_tc{};
static bool               s_current_tc_valid = false;

// OpenWeather units are a shared request policy, not locale-owned text.
static constexpr char kWeatherUnits[] = "metric";

// W-R3/A4.0: forecast fetch lifecycle — status-only publish on non-success; success via
// weatherPublishState (called inside weatherFetchForecast on Published result).
// W-R3/A4.0: цикл fetch — status-only при ошибке; успех через weatherPublishState.
static void finishWeatherForecastFetch(WeatherForecastFetchResult result,
                                        bool current_ok,
                                        const WeatherTrueCurrent* tc) {
  switch (result) {
    case WeatherForecastFetchResult::Published:
      // Already published by weatherFetchForecast() with true-current overlay applied.
      // Уже опубликовано weatherFetchForecast() с overlay true-current (если был).
#if YORADIO_WEATHER_REQ_DIAG
      {
        WeatherState snap{};
        if (weatherGetStateSnapshot(&snap)) {
          Serial.printf("[WEATHER_STATE] refresh=end result=success"
                        " has_data=%d stale=%d error=%d\n",
                        (int)(snap.forecast_valid && snap.current.valid),
                        (int)snap.stale, (int)snap.last_error);
        }
      }
#endif
      break;
    case WeatherForecastFetchResult::DeferredInternalLow:
      // A4.0/§11: current ok but forecast deferred — overlay current over LKG, set InternalLow.
      // A4.0/§11: current ok, forecast отложен — overlay current на LKG, InternalLow.
      if (current_ok && tc && tc->valid) {
        weatherPublishCurrentOverLkg(*tc, WeatherLastError::InternalLow, false);
      } else {
        weatherStateMarkFetchDeferred();
      }
      break;
    case WeatherForecastFetchResult::NotConfigured:
      weatherStateMarkFetchFailed(WeatherLastError::NotConfigured);
      break;
    case WeatherForecastFetchResult::NotConnected:
      weatherStateMarkFetchFailed(WeatherLastError::NotConnected);
      break;
    default:
      // A4.0 Case C: current ok, forecast terminal failure — overlay current over LKG.
      // A4.0 Case C: current ok, forecast провалился — overlay current на LKG.
      if (current_ok && tc && tc->valid) {
        weatherPublishCurrentOverLkg(*tc, WeatherLastError::FetchFailed, true);
      } else {
        weatherStateMarkFetchFailed(WeatherLastError::FetchFailed);
      }
      break;
  }
}

// A4.0: weatherStateMarkFetchBegin() is now called in doSync BEFORE getWeather(),
// covering the entire combined current+forecast cycle.
// A4.0: weatherStateMarkFetchBegin() теперь вызывается в doSync ДО getWeather(),
// покрывая весь комбинированный цикл current+forecast.
static void runWeatherForecastFetch(WeatherEdgeSession& session,
                                     const WeatherTrueCurrent* true_current) {
  const WeatherForecastFetchResult result =
      weatherFetchForecast(kWeatherUnits, i18n::locale().weatherApiLanguage,
                           session, true_current);
  finishWeatherForecastFetch(result,
                              true_current != nullptr && true_current->valid,
                              true_current);
}

static bool isDoSyncTaskActive() {
  if (syncTaskHandle == nullptr) {
    return false;
  }
  const eTaskState st = eTaskGetState(syncTaskHandle);
  return st != eDeleted;
}

// W-R1C.4: schedule outcome — explicit refresh diagnostics + busy handoff semantics.
// W-R1C.4: результат планирования — диагностика явного refresh и семантика busy.
enum class DoSyncScheduleResult : uint8_t {
  Started = 0,
  Busy,
  CreateFailed,
};

namespace {
SemaphoreHandle_t s_dosync_schedule_mutex = nullptr;

// Bounded mutex wait — never portMAX_DELAY (matches net_dns_resolver pattern).
// Ограниченное ожидание mutex — без portMAX_DELAY (как в net_dns_resolver).
constexpr uint32_t kDoSyncScheduleMutexWaitMs = 1500;

SemaphoreHandle_t doSyncScheduleMutex() {
  if (!s_dosync_schedule_mutex) {
    s_dosync_schedule_mutex = xSemaphoreCreateMutex();
  }
  return s_dosync_schedule_mutex;
}
} // namespace

// Serialized check-and-create — ticks(), LVGL footer and WebUI may call concurrently.
// Сериализованный check-and-create — ticks(), LVGL footer и WebUI могут вызывать параллельно.
static DoSyncScheduleResult scheduleDoSyncIfIdleEx() {
  SemaphoreHandle_t mutex = doSyncScheduleMutex();
  if (!mutex) {
    return DoSyncScheduleResult::CreateFailed;
  }
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(kDoSyncScheduleMutexWaitMs)) != pdTRUE) {
    return DoSyncScheduleResult::CreateFailed;
  }

  DoSyncScheduleResult result;
  if (isDoSyncTaskActive()) {
    result = DoSyncScheduleResult::Busy;
  } else {
    const BaseType_t ok = xTaskCreatePinnedToCore(
        doSync, "doSync", kDoSyncTaskStackBytes, NULL, 0, &syncTaskHandle, 0);
    result = (ok == pdPASS) ? DoSyncScheduleResult::Started : DoSyncScheduleResult::CreateFailed;
  }
  xSemaphoreGive(mutex);
  return result;
}

// Returns true when doSync was created; false if busy or create failed.
// true — задача создана; false — doSync занят или xTaskCreate не удался.
static bool scheduleDoSyncIfIdle() {
  return scheduleDoSyncIfIdleEx() == DoSyncScheduleResult::Started;
}

// E34 corrective: common doSync exit — clear syncTaskHandle under schedule mutex, then self-delete.
// Without this, eTaskGetState(stale handle) may stay non-eDeleted → permanent schedule=busy.
// E34 corrective: общий выход doSync — сброс syncTaskHandle под schedule mutex, затем self-delete.
static void doSyncReleaseHandleAndSelfDelete() {
  SemaphoreHandle_t mutex = doSyncScheduleMutex();
  if (mutex != nullptr) {
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
      const TaskHandle_t self = xTaskGetCurrentTaskHandle();
      if (syncTaskHandle == self) {
        syncTaskHandle = nullptr;
      }
      xSemaphoreGive(mutex);
    } else if (syncTaskHandle == xTaskGetCurrentTaskHandle()) {
      // Mutex take failed — still clear own handle to avoid permanent busy.
      // Не взяли mutex — всё равно сбрасываем свой handle, иначе вечный busy.
      syncTaskHandle = nullptr;
    }
  } else if (syncTaskHandle == xTaskGetCurrentTaskHandle()) {
    syncTaskHandle = nullptr;
  }
  vTaskDelete(nullptr);
}

static constexpr uint32_t WEATHER_FIRST_SYNC_GRACE_MS = 30000;
static uint32_t s_weather_ready_at_ms = 0;
// E21W0-A: last attempt timestamp for since_last_ms / метка последней попытки погоды
static uint32_t s_weather_last_attempt_ms = 0;
// Set in doSync before forceWeather is cleared / выставляется в doSync до сброса forceWeather
static bool s_weather_diag_forced = false;

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
#if !defined(HIDE_WEATHER)
  return config.store.showweather &&
         strlen(config.store.weatherkey) != 0 &&
         network.status == CONNECTED;
#else
  return false;
#endif
}

static bool isWeatherGraceElapsed() {
  return s_weather_ready_at_ms != 0 &&
         (int32_t)(millis() - s_weather_ready_at_ms) >= 0;
}

#if !defined(HIDE_WEATHER)
#ifndef YORADIO_WEATHER_DIAG
#define YORADIO_WEATHER_DIAG 0
#endif

// W-R1B: request/response location diagnostics — off by default.
// W-R1B: диагностика запросов/ответов — выключена по умолчанию.
#ifndef YORADIO_WEATHER_REQ_DIAG
#define YORADIO_WEATHER_REQ_DIAG 0
#endif

// E21W0-C: quiet runtime + optional verbose diag / тихий runtime и опциональная диагностика
namespace weather_diag {

// One compact failure line per failed attempt / одна компактная строка на неудачную попытку
static void logFailCompact(const char* stage, uint32_t t0, uint8_t retry,
                           const IPAddress* ip, int httpCode) {
  Serial.printf("[WEATHER] fail stage=%s retry=%u", stage, (unsigned)retry);
  if (ip != nullptr && static_cast<uint32_t>(*ip) != 0U) {
    Serial.printf(" ip=%s", ip->toString().c_str());
  }
  if (httpCode >= 0) {
    Serial.printf(" http=%d", httpCode);
  }
  Serial.printf(" elapsed_ms=%lu\n", (unsigned long)(millis() - t0));
}

#if YORADIO_WEATHER_DIAG
static void heapSnapshot() {
  Serial.printf("[WEATHER] heap.free=%u internal_largest=%u psram_free=%u\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)ESP.getFreePsram());
}

static size_t pathLenEstimate() {
  return 48U + strlen(config.store.weatherlat) + strlen(config.store.weatherlon) +
         strlen(kWeatherUnits) + strlen(i18n::locale().weatherApiLanguage) +
         strlen(config.store.weatherkey);
}

static void logStart() {
  const uint32_t now = millis();
  const uint32_t sinceLast =
      (s_weather_last_attempt_ms == 0U) ? 0U : (now - s_weather_last_attempt_ms);
  Serial.printf(
      "[WEATHER] start showweather=%d force_requested=%d grace_elapsed=%d "
      "host=api.openweathermap.org port=80 scheme=http path_len=%u "
      "avail_to_ms=2000 read_to_ms=500 dns_to_ms=5000 wifi_status=%d ip=%s rssi=%d "
      "boot_ms=%lu since_last_ms=%lu audio_playing=%d\n",
      (int)config.store.showweather,
      (int)s_weather_diag_forced,
      (int)isWeatherGraceElapsed(),
      (unsigned)pathLenEstimate(),
      (int)WiFi.status(),
      WiFi.localIP().toString().c_str(),
      WiFi.RSSI(),
      (unsigned long)now,
      (unsigned long)sinceLast,
      (int)player.isRunning());
  heapSnapshot();
  s_weather_last_attempt_ms = now;
}

static void logBodyPrefix(const char* raw) {
  if (raw == nullptr || raw[0] == '\0') {
    return;
  }
  char buf[96];
  strlcpy(buf, raw, sizeof(buf));
  char* appid = strstr(buf, "appid=");
  if (appid != nullptr) {
    strlcpy(appid, "appid=…", sizeof(buf) - (size_t)(appid - buf));
  }
  Serial.printf("[WEATHER] body_prefix=\"%s\"\n", buf);
}

static void logParseFail(const char* field, uint32_t t0, int httpCode, size_t bodyBytes,
                         const char* line) {
  Serial.printf("[WEATHER] fail stage=parse field=%s elapsed_ms=%lu http_code=%d body_bytes~=%u\n",
                field, (unsigned long)(millis() - t0), httpCode, (unsigned)bodyBytes);
  logBodyPrefix(line);
  heapSnapshot();
}

static void logSuccess(uint32_t t0, int httpCode, float tempC, const char* icon,
                       const char* desc) {
  Serial.printf(
      "[WEATHER] success stage=done http_code=%d temp=%.1f icon=%s desc=\"%s\" "
      "elapsed_ms=%lu next_interval_s=%u current_ok=1\n",
      httpCode, tempC, icon, desc, (unsigned long)(millis() - t0),
      (unsigned)WEATHER_REGULAR_INTERVAL_SEC);
  heapSnapshot();
}
#else
static inline void logStart() {}
static inline void logParseFail(const char*, uint32_t, int, size_t, const char*) {}
static inline void logSuccess(uint32_t, int, float, const char*, const char*) {}
#endif

} // namespace weather_diag
#endif // !HIDE_WEATHER

void ticks() {
  if(!display.ready()) return; //waiting for SD is ready
  aiSubsystem.onTicker();
  static const uint16_t weatherSyncInterval =
      static_cast<uint16_t>(WEATHER_REGULAR_INTERVAL_SEC);
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
      scheduleDoSyncIfIdle();
    }
    // W-R1C.1: deferred forecast-only retry — no current repeat, heap poll in weather_fetch.
    // W-R1C.1: отложенный только-прогноз — без повтора current, опрос heap в weather_fetch.
    if (!network.forceTimeSync && !weatherSyncReady && weatherEnabled && isWeatherGraceElapsed()) {
      if (weatherForecastPollPending()) {
        // W-R1C.2: busy doSync — drop arm only; logical pending stays for next cooldown poll.
        // W-R1C.2: doSync занят — сбрасываем только arm; pending остаётся до следующего poll.
        if (!scheduleDoSyncIfIdle()) {
          weatherForecastDiscardPendingOnlyArm();
        }
      }
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
    }
#ifdef USE_SD
    // Block 8-E8: SDCHANGE mode removed — always allow SD check when connected.
    player.sendCommand({PR_CHECKSD, 0});
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
  // E36FS1b: restart WebUI listener after STA GOT_IP / после reconnect перезапуск listener.
  netserver.begin(true);
  display.putRequest(NEWMODE, PLAYER);
  if(config.getMode()==PM_SDCARD) {
    network.status=CONNECTED;
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
    // E36FS1b: drop TCP listener while STA has no IP / гасим listener без IP.
    netserver.begin(true);
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
  }
  vTaskDelete( NULL );
}

#define DBGAP false

// E36FS1: non-zero IP required for AsyncWebServer / для TCP нужен реальный IP.
static bool networkIpNonZero(const IPAddress& ip) {
  return static_cast<uint32_t>(ip) != 0U;
}

bool MyNetwork::isTcpReady() const {
  if (status == CONNECTED) {
    return WiFi.status() == WL_CONNECTED && networkIpNonZero(WiFi.localIP());
  }
  if (status == SOFT_AP) {
    return networkIpNonZero(WiFi.softAPIP());
  }
  return false;
}

void MyNetwork::begin() {
  BOOTLOG("network.begin");
  runtimeReconnectSuspendedForSetup = false;
  config.initNetwork();
  ctimer.detach();
  forceTimeSync = true;
  forceWeather = false;
  if (config.ssidsCount == 0 || DBGAP) {
    if (!DBGAP) {
      // S6V9C / 8-E19C-1: Wi-Fi Recovery — no automatic AP on boot; AP only on Hotspot page.
      // S6V9C / 8-E19C-1: Recovery — AP при старте не поднимаем; AP только со страницы Hotspot.
      Serial.println("[Network] Wi-Fi Recovery needed; AP not started");
      status = FAILED;
      Serial.println("##[BOOT]#\tdone");
      return;
    }
    raiseSoftAP();
    return;
  }
  if(config.getMode()!=PM_SDCARD){
    const bool wifi_ok = wifiBegin();
    // Wi-Fi/NVS activity inside the boot bootstrap window can desynchronize the ST7701 RGB
    // scanout, and with the accepted RESTART_IN_VSYNC=OFF configuration nothing restores it on
    // its own. Recover once the episode has finished, whatever its outcome: the hazard belongs
    // to the window, not to the "connected" result. Without this the display stays shifted until
    // some later persistence episode happens to request a resync.
    // Активность Wi-Fi/NVS внутри загрузочного bootstrap может рассинхронизировать RGB scanout
    // ST7701; при принятой конфигурации RESTART_IN_VSYNC=OFF автоматического восстановления нет.
    // Ресинхрон по завершении эпизода, независимо от результата подключения.
    display.requestRgbResync();
    if(!wifi_ok){
      // S6V9C / 8-E19C-1: Wi-Fi Recovery — no automatic AP on failed STA; AP only on Hotspot page.
      // S6V9C / 8-E19C-1: Recovery — AP при неудаче STA не поднимаем; AP только со Hotspot page.
      Serial.println("[Network] Wi-Fi Recovery needed; AP not started");
      status = FAILED;
      Serial.println("##[BOOT]#\tdone");
      return;
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
}

void MyNetwork::setWifiParams(){
  WiFi.setSleep(false);
  WiFi.onEvent(WiFiReconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiLostConnection, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
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
      telnet.printf(clientId, "##SYS.DATE#: %s-%02d:%02d\n> ", timeStringBuff, -(int)config.store.tzHour, config.store.tzMin);
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
      // E36FS1: deferred NetServer when Hotspot re-entered / NetServer при повторном Hotspot.
      netserver.begin(true);
      return;
    }
  }
  Serial.println("[Network] Open Hotspot mode active");
  raiseSoftAP();
  // E36FS1: softAPIP may lag softAP() — brief poll before NetServer / IP может появиться с задержкой.
  const uint32_t deadline = millis() + 3000U;
  while ((int32_t)(millis() - deadline) < 0) {
    if (networkIpNonZero(WiFi.softAPIP())) {
      break;
    }
    delay(10);
  }
  netserver.begin(true);
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
  // E36FS1b: rebind on STA or stop when Hotspot closed / после Hotspot — STA или stop.
  netserver.begin(true);
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
  // Intentionally empty — legacy call sites (e.g. getWeather success) must not re-arm polling.
  // W1: forecast runs in the same doSync pass after getWeather(); UI uses forceWeatherRefreshFromUi().
  // Намеренно пусто — success path getWeather() не должен снова ставить forceWeather (вечный poll).
}

void MyNetwork::forceWeatherRefreshFromUi() {
  // W-R1C.1: explicit user action (Apply/footer) always requests full sync — current + forecast.
  // Cancel armed pending-only pass so Path A (force=1) takes priority over Path B.
  // W-R1C.1: явный запрос (Apply/footer) — всегда полный sync; Path A приоритетнее pending-only.
  weatherForecastDiscardPendingOnlyArm();
  forceWeather = true;
  // W-R1C.4: immediate non-blocking schedule attempt — HTTP only inside doSync on Core 0.
  // W-R1C.4: немедленная попытка schedule — HTTP только в doSync на Core 0, не в callback.
  const DoSyncScheduleResult sched = scheduleDoSyncIfIdleEx();
#if YORADIO_WEATHER_REQ_DIAG
  switch (sched) {
    case DoSyncScheduleResult::Started:
      Serial.println("[WEATHER_SCHED] explicit request schedule=started");
      break;
    case DoSyncScheduleResult::Busy:
      Serial.println("[WEATHER_SCHED] explicit request schedule=busy");
      break;
    case DoSyncScheduleResult::CreateFailed:
      Serial.println("[WEATHER_SCHED] explicit request schedule=create_failed");
      break;
  }
#endif
  (void)sched;
}


void doSync( void * pvParameters ) {
  static uint8_t tsFailCnt = 0;
  //static uint8_t wsFailCnt = 0;
#if YORADIO_WEATHER_STACK_DIAG
  const char* stack_diag_mode = "other";
#endif
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
#if YORADIO_WEATHER_STACK_DIAG
    stack_diag_mode = "full";
#endif
    s_weather_diag_forced = true;
    network.forceWeather = false;
    weatherForecastDiscardPendingOnlyArm(); // coalesce — full sync covers any armed pending-only pass
#if YORADIO_WEATHER_REQ_DIAG
    Serial.println("[WEATHER_SCHED] run force=1");
#endif
    // A4.0: weatherStateMarkFetchBegin() now covers the ENTIRE combined current+forecast cycle.
    // A4.0: weatherStateMarkFetchBegin() теперь покрывает ВЕСЬ комбинированный цикл.
    weatherStateMarkFetchBegin();

    // HF-W-DNS: one edge session per doSync cycle — cycle-level preferred preserved across
    // current→forecast; request-level tracking reset between them by beginRequest() inside
    // weatherFetchForecast. Destroyed when doSync exits; never persistent across cycles.
    // HF-W-DNS: одна edge-сессия на цикл; cycle-level preserved; request-level сбрасывается.
    WeatherEdgeSession weatherSession;
    weatherSession.resetCycle();

    // A4.0: reset staging before each full cycle.
    // A4.0: сбрасываем staging перед каждым полным циклом.
    s_current_tc = WeatherTrueCurrent{};
    s_current_tc_valid = false;

    const bool current_ok = getWeather(weatherSession);
    // Weather W1: forecast fetch in the same weather-sync context (Core 0 doSync), HTTP only,
    // never from UI. Parsing/aggregation lives in weather_fetch.* — not here. Failure is non-fatal
    // (keeps last-known-good WeatherState) and does not affect current weather / status row.
    // Weather W1: прогноз в том же weather-sync контексте (Core 0), HTTP, не из UI.
    // Парсинг/агрегация — в weather_fetch.*; ошибка некритична (last-known-good).
    // A4.0: pass true-current staging if /weather succeeded, nullptr otherwise (Case B).
    // A4.0: передаём true-current staging если /weather успешен; nullptr при отказе (Case B).
    runWeatherForecastFetch(weatherSession,
                             current_ok ? &s_current_tc : nullptr);
    s_weather_diag_forced = false;
  } else if (weatherForecastTakePendingOnlyRun()) {
#if YORADIO_WEATHER_STACK_DIAG
    stack_diag_mode = "pending_forecast";
#endif
    // W-R1C.1: forecast-only deferred retry — no getWeather() in this path.
    // W-R1C.1: отложенный retry только прогноза — без getWeather().
#if YORADIO_WEATHER_REQ_DIAG
    Serial.println("[WEATHER_SCHED] run pending_forecast=1");
    Serial.printf("[WEATHER_FC] pending retry start int_free=%u int_block=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
#endif
    // A4.0/§12: pending-only path — no /weather request; current_source = ForecastFallback.
    // A4.0/§12: pending-only — без /weather; current_source = ForecastFallback (list[0] прокси).
    // Not changed now: cross-task staging of an earlier true current is deliberately NOT used
    // here — that would require a global with lifetime beyond a single doSync cycle.
    // Не изменено: использование предыдущего true-current намеренно не поддерживается.
    weatherStateMarkFetchBegin();
    // HF-W-DNS: fresh session for pending-only forecast — no preferred from any prior cycle.
    // System DNS is tried first, then Cloudflare, then Quad9 (same as a cold start).
    // HF-W-DNS: свежая сессия для pending-only; preferred от предыдущего цикла не используется.
    WeatherEdgeSession pendingSession;
    pendingSession.resetCycle();
    runWeatherForecastFetch(pendingSession, nullptr);
  }
#if YORADIO_WEATHER_STACK_DIAG
  logDoSyncStackUsage(stack_diag_mode, "exit");
#endif
  doSyncReleaseHandleAndSelfDelete();
}

// HF-W-DNS: current-weather failure-injection hooks — default OFF in production.
// Enable locally in src/myoptions.h for runtime acceptance tests only.
// Включить локально в myoptions.h только для acceptance-тестов.
#ifndef YORADIO_WEATHER_EDGE_TEST_PRIMARY_CONNECT_FAIL
#define YORADIO_WEATHER_EDGE_TEST_PRIMARY_CONNECT_FAIL 0
#endif
#ifndef YORADIO_WEATHER_EDGE_TEST_PRIMARY_READ_WAIT
#define YORADIO_WEATHER_EDGE_TEST_PRIMARY_READ_WAIT 0
#endif
#ifndef YORADIO_WEATHER_EDGE_TEST_DNS0_FAIL
#define YORADIO_WEATHER_EDGE_TEST_DNS0_FAIL 0
#endif
#ifndef YORADIO_WEATHER_EDGE_TEST_FORCE_DUPLICATE
#define YORADIO_WEATHER_EDGE_TEST_FORCE_DUPLICATE 0
#endif

bool getWeather(WeatherEdgeSession& edgeSession) {
#if !defined(HIDE_WEATHER)
  WiFiClient client;
  const char* host  = "api.openweathermap.org";
  const uint32_t t0 = millis();
  int httpCode = -1;
  size_t bodyBytes = 0U;

  weather_diag::logStart();

  char weatherUnitsRam[12];
  char weatherLangRam[8];
  strlcpy(weatherUnitsRam, kWeatherUnits, sizeof(weatherUnitsRam));
  strlcpy(weatherLangRam, i18n::locale().weatherApiLanguage, sizeof(weatherLangRam));

#if YORADIO_WEATHER_REQ_DIAG
  Serial.printf("[WEATHER_CFG] lat=\"%s\" lon=\"%s\" units=%s lang=%s key_present=%d key_len=%u\n",
                config.store.weatherlat, config.store.weatherlon,
                weatherUnitsRam, weatherLangRam,
                strlen(config.store.weatherkey) > 0 ? 1 : 0,
                (unsigned)strlen(config.store.weatherkey));
  Serial.printf("[WEATHER_REQ] path=current lat=%s lon=%s units=%s lang=%s\n",
                config.store.weatherlat, config.store.weatherlon,
                weatherUnitsRam, weatherLangRam);
#endif

  // HF-W-DNS: lazy edge-fallback state machine.
  // System path first; fallback DNS queried only after a qualifying transport failure.
  // Lazy: Cloudflare/Quad9 запрашиваются только после транспортного сбоя.
  IPAddress serverIP; // IP of the successfully connected edge — used in legacy diag below
  bool connected = false;

  // Build the HTTP request once — it is the same for all edge attempts.
  // Строим HTTP-запрос один раз — он одинаков для всех попыток.
  static char httpget[250]; // static → off the doSync stack / static → вне стека doSync
  snprintf(httpget, sizeof(httpget),
           "GET /data/2.5/weather?lat=%s&lon=%s&units=%s&lang=%s&appid=%s HTTP/1.1\r\n"
           "Host: %s\r\nConnection: close\r\n\r\n",
           config.store.weatherlat, config.store.weatherlon,
           weatherUnitsRam, weatherLangRam,
           config.store.weatherkey, host);

  // ── Attempt loop: system (0), fallback DNS 0 (1), fallback DNS 1 (2) ────────
  // Maximum 3 TCP attempts; maximum 2 fallback DNS queries.
  // Максимум 3 TCP-попытки; максимум 2 запроса к fallback DNS.
  for (uint8_t attemptIdx = 0; attemptIdx < kWeatherEdgeMaxAttempts; ++attemptIdx) {
    IPAddress edgeIP;
    const char* sourceName = "system";

    if (attemptIdx == 0) {
      // ── Attempt source 0: system / router DNS ───────────────────────────────
      if (!networkResolveHostForConnect(host, edgeIP)) {
        weather_diag::logFailCompact("dns", t0, 0, nullptr, -1);
        Serial.println("##WEATHER###: DNS resolve failed");
#if YORADIO_WEATHER_REQ_DIAG
        Serial.printf("[WEATHER_NET] path=current fail attempt=0 source=system stage=dns\n");
#endif
        // Resolution failed — try fallback DNS directly
        // Резолвинг не прошёл — переходим к fallback DNS
        continue;
      }
      if (edgeSession.wasAttempted(edgeIP)) {
        // Deduplicate: system IP already tried (shouldn't happen on attempt 0, but safe)
        continue;
      }
      edgeSession.markAttempted(edgeIP);
#if YORADIO_WEATHER_REQ_DIAG
      Serial.printf("[WEATHER_NET] path=current attempt=0 source=system ip=%s\n",
                    edgeIP.toString().c_str());
#endif
#if YORADIO_WEATHER_DIAG
      Serial.printf("[WEATHER] dns ok retry=0 ip=%s elapsed_ms=%lu\n",
                    edgeIP.toString().c_str(), (unsigned long)(millis() - t0));
#endif
    } else {
      // ── Attempt source 1 or 2: fallback DNS server ──────────────────────────
      const uint8_t fbIdx = (uint8_t)(attemptIdx - 1u);
      size_t fbCount = 0;
      const NetDnsServer* fbServers = netDnsDefaultFallbackServers(fbCount);
      if (fbIdx >= (uint8_t)fbCount) break; // no more configured fallback servers

      sourceName = fbServers[fbIdx].name;

#if YORADIO_WEATHER_EDGE_TEST_DNS0_FAIL
      if (fbIdx == 0) {
        Serial.printf("[WEATHER_NET] path=current dns resolver=%s INJECTED_FAIL\n", sourceName);
        continue; // simulate DNS0 failure
      }
#endif

      // Query this fallback DNS server
      // Запрашиваем этот fallback DNS-сервер
      IPAddress candidates[4];
      size_t candidateCount = 0;
      const NetDnsQueryStatus dnsStatus = netDnsQueryA(
          host, fbServers[fbIdx].address,
          candidates, 4, candidateCount,
          800u, "weather-current");

#if YORADIO_WEATHER_REQ_DIAG
      Serial.printf("[WEATHER_NET] path=current dns resolver=%s status=%s candidates=%u\n",
                    sourceName,
                    dnsStatus == NetDnsQueryStatus::Success ? "ok" : "fail",
                    (unsigned)candidateCount);
#endif

      if (dnsStatus != NetDnsQueryStatus::Success || candidateCount == 0) {
        continue; // DNS query failed — try next fallback server
      }

      // Select first candidate not already attempted.
      // Выбираем первый не пробовавшийся IP.
      edgeIP = IPAddress(0, 0, 0, 0);
      for (size_t ci = 0; ci < candidateCount; ++ci) {
#if YORADIO_WEATHER_EDGE_TEST_FORCE_DUPLICATE
        if (fbIdx == 0 && ci == 0 && edgeSession.attemptedCount > 0) {
          // Inject: replace this candidate with the already-attempted system IP so the
          // real wasAttempted() check below genuinely returns true and skips it.
          // Инъекция: заменяем кандидата на уже пробованный IP — реальный wasAttempted() срабатывает.
          const IPAddress injected = edgeSession.attempted[0];
          Serial.printf("[WEATHER_NET] path=current INJECT_DUPLICATE resolver=%s "
                        "overridden_ip=%s was=%s\n",
                        sourceName, injected.toString().c_str(),
                        candidates[ci].toString().c_str());
          candidates[ci] = injected;
          // Fall through → wasAttempted(candidates[ci]) == true → real dedupe path runs.
        }
#endif
        if (!edgeSession.wasAttempted(candidates[ci])) {
          edgeIP = candidates[ci];
          break;
        }
#if YORADIO_WEATHER_REQ_DIAG
        Serial.printf("[WEATHER_NET] path=current skip resolver=%s reason=duplicate ip=%s\n",
                      sourceName, candidates[ci].toString().c_str());
#endif
      }

      if (edgeIP == IPAddress(0, 0, 0, 0)) {
        // All candidates were duplicates — advance to next resolver without TCP attempt
        continue;
      }
      edgeSession.markAttempted(edgeIP);
#if YORADIO_WEATHER_REQ_DIAG
      Serial.printf("[WEATHER_NET] path=current attempt=%u source=%s ip=%s\n",
                    (unsigned)attemptIdx, sourceName, edgeIP.toString().c_str());
#endif
    }

    // ── TCP connect ───────────────────────────────────────────────────────────
    client.stop(); // ensure clean state before each connect
    serverIP = edgeIP; // preserve for legacy diag functions below

#if YORADIO_WEATHER_EDGE_TEST_PRIMARY_CONNECT_FAIL
    if (attemptIdx == 0) {
      Serial.printf("[WEATHER_NET] path=current fail attempt=0 source=system stage=connect INJECTED_FAIL\n");
      continue; // simulate primary connect failure
    }
#endif

    if (!client.connect(edgeIP, 80)) {
#if YORADIO_WEATHER_REQ_DIAG
      const int connect_errno = errno;
      Serial.printf("[WEATHER] connect fail try=%u errno=%d (%s)\n",
                    (unsigned)attemptIdx, connect_errno, strerror(connect_errno));
      Serial.printf("[WEATHER_NET] path=current fail attempt=%u source=%s stage=connect ip=%s\n",
                    (unsigned)attemptIdx, sourceName, edgeIP.toString().c_str());
#endif
      continue; // ConnectFailed → try next edge
    }
#if YORADIO_WEATHER_DIAG
    Serial.printf("[WEATHER] connect ok retry=%u elapsed_ms=%lu\n",
                  (unsigned)attemptIdx, (unsigned long)(millis() - t0));
#endif

    // ── Send HTTP request (same for all edges — host header is always the hostname) ──
    client.print(httpget);

    // ── Wait for first response byte ──────────────────────────────────────────
    // read_wait timeout before ANY byte → qualifies for next edge rotation.
    // Таймаут до первого байта → переходим к следующему edge.
#if YORADIO_WEATHER_EDGE_TEST_PRIMARY_READ_WAIT
    if (attemptIdx == 0) {
      Serial.printf("[WEATHER_NET] path=current fail attempt=0 source=system stage=read_wait_before_status INJECTED_FAIL\n");
      client.stop();
      continue; // simulate read_wait before status
    }
#endif

    {
      const uint32_t rwStart = millis();
      bool gotByte = false;
      while ((uint32_t)(millis() - rwStart) < 2000UL) {
        if (client.available() > 0) { gotByte = true; break; }
        delay(1);
      }
      if (!gotByte) {
        // ReadWaitBeforeStatus — no HTTP bytes received → try next edge
        weather_diag::logFailCompact("read_wait", t0, attemptIdx, &serverIP, httpCode);
        Serial.println("##WEATHER###: client available timeout !");
#if YORADIO_WEATHER_REQ_DIAG
        Serial.printf("[WEATHER_NET] path=current fail attempt=%u source=%s stage=read_wait_before_status ip=%s\n",
                      (unsigned)attemptIdx, sourceName, edgeIP.toString().c_str());
#endif
        client.stop();
        continue; // ReadWaitBeforeStatus → try next edge
      }
    }

    // Bytes arrived — this edge is usable for the response.
    // Bytes arrived after this point: do NOT rotate on response/parse failures.
    connected = true;
    if (attemptIdx > 0) {
      // Store preferred fallback edge — forecast will try this IP first in the same cycle.
      // Сохраняем preferred fallback edge — forecast попробует его первым в том же цикле.
      const uint8_t resolverIdx = (uint8_t)(attemptIdx - 1u);
      edgeSession.setPreferred(edgeIP, resolverIdx);
#if YORADIO_WEATHER_REQ_DIAG
      Serial.printf("[WEATHER_NET] path=current success attempt=%u source=%s ip=%s preferred_set=1\n",
                    (unsigned)attemptIdx, sourceName, edgeIP.toString().c_str());
#endif
    } else {
#if YORADIO_WEATHER_REQ_DIAG
      Serial.printf("[WEATHER_NET] path=current success attempt=0 source=system ip=%s\n",
                    edgeIP.toString().c_str());
#endif
    }
    break; // proceed to response parsing
  }

  if (!connected) {
    weather_diag::logFailCompact("connect", t0, kWeatherEdgeMaxAttempts, &serverIP, -1);
    Serial.println("##WEATHER###: connection  failed");
    return false;
  }

  // ── Response: read HTTP headers + JSON body into `line` (OWM /weather is compact JSON).
  // ── Ответ: читаем HTTP-заголовки + тело в `line` (OWM /weather — compact JSON одной строкой).
  unsigned long timeout = millis();
  String line = "";
  if (client.connected()) {
    while (client.available()) {
      line = client.readStringUntil('\n');
      bodyBytes += line.length() + 1U;
      if (httpCode < 0 && line.startsWith("HTTP/")) {
        const int sp1 = line.indexOf(' ');
        if (sp1 >= 0) {
          const int sp2 = line.indexOf(' ', sp1 + 1);
          httpCode = line.substring(sp1 + 1, sp2 > 0 ? sp2 : line.length()).toInt();
        }
#if YORADIO_WEATHER_DIAG
        Serial.printf("[WEATHER] http_status=%d elapsed_ms=%lu\n",
                      httpCode, (unsigned long)(millis() - t0));
#endif
      }
      if (strstr(line.c_str(), "\"temp\"") != NULL) {
        client.stop();
        break;
      }
      if ((millis() - timeout) > 500) {
        client.stop();
        weather_diag::logFailCompact("read", t0, 0, &serverIP, httpCode);
        Serial.println("##WEATHER###: client read timeout !");
        return false;
      }
    }
  }
  if (strstr(line.c_str(), "\"temp\"") == NULL) {
    weather_diag::logFailCompact("http_body", t0, 0, &serverIP, httpCode);
#if YORADIO_WEATHER_DIAG
    weather_diag::logBodyPrefix(line.c_str());
#endif
    Serial.println("##WEATHER###: weather not found !");
    return false;
  }
#if YORADIO_WEATHER_DIAG
  Serial.printf("[WEATHER] body_match line_bytes=%u elapsed_ms=%lu\n",
                (unsigned)line.length(), (unsigned long)(millis() - t0));
#endif

  // ── A4.0: ArduinoJson v7 parse (replaces strstr block) ────────────────────────
  // Single parse feeds WeatherState merge via true-current staging.
  // Единый парсинг — staging для merge в WeatherState.
  WeatherCurrentParsed parsed{};
  if (!weatherParseCurrentBody(line.c_str(), &parsed)) {
#if YORADIO_WEATHER_DIAG
    weather_diag::logParseFail("json_parse", t0, httpCode, bodyBytes, line.c_str());
#else
    (void)bodyBytes;
    weather_diag::logFailCompact("parse", t0, 0, &serverIP, httpCode);
#endif
    Serial.println("##WEATHER###: parse failed !");
    return false;
  }

  // ── A4.0: populate true-current staging for forecast overlay ──────────────────
  // A4.0: заполняем staging для overlay в forecast.
  s_current_tc       = parsed.tc;
  s_current_tc_valid = true;

  // ── Human-readable serial diagnostic (##WEATHER###) ──────────────────────────
  // ── Человекочитаемая serial-диагностика (##WEATHER###) ─────────────────────
  char gust[20] = "";
  if (parsed.has_gust && parsed.gust_mps > 0) {
    char porv[10];
    strlcpy(gust, i18n::text(i18n::TextId::WeatherGustsPrefix), sizeof(gust));
    itoa(parsed.gust_mps, porv, 10);
    strlcat(gust, porv, sizeof(gust));
  }

  // ── Diagnostics ───────────────────────────────────────────────────────────────
#if YORADIO_WEATHER_DIAG
  weather_diag::logSuccess(t0, httpCode, parsed.tc.temp_c, parsed.tc.owm_icon,
                           parsed.full_desc);
#endif
#if YORADIO_WEATHER_REQ_DIAG
  Serial.printf("[WEATHER_RES] path=current ok=1"
                " name=\"%s\" country=\"%s\" temp=%.1f icon=%s"
                " code=%u dt=%lu\n",
                parsed.tc.location.city, parsed.tc.location.country,
                (double)parsed.tc.temp_c, parsed.tc.owm_icon,
                (unsigned)parsed.tc.owm_code, (unsigned long)parsed.tc.updated_at);
#endif
  Serial.printf("##WEATHER###: descr.: %s, temp.: %+.1f*C (feels like %+.0f*C)"
                " \007 press.: %d mm \007 hum.: %s%%"
                " \007 wind %s %.0f%s m/s (st. %s)\n",
                parsed.full_desc, (double)parsed.tc.temp_c, (double)parsed.tc.feels_like_c,
                parsed.pressure_mmhg, parsed.humidity_str,
                i18n::windDirection(static_cast<uint8_t>(parsed.wind_dir_idx)),
                (double)parsed.tc.wind_speed,
                gust, parsed.tc.location.city);

  // W1+A2b-loop-guard: do NOT call requestWeatherSync() here — forecast already runs in the same
  // doSync pass (weatherFetchForecast). Re-arming forceWeather caused infinite 1 Hz polling.
  // W1+A2b-loop-guard: не вызывать requestWeatherSync() — прогноз уже в том же doSync.
  return true;
#endif // if !defined(HIDE_WEATHER)
  return false;
}
