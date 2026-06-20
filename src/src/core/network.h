#ifndef network_h
#define network_h
#include <Ticker.h>
#include "time.h"
#include "WiFi.h"
#include "rtcsupport.h"

#define apSsid      "yoRadioAP"
#define apPassword  ""
//#define TSYNC_DELAY 10800000    // 1000*60*60*3 = 3 hours
#define TSYNC_DELAY       3600000     // 1000*60*60   = 1 hour
#define WEATHER_STRING_L  254

// W-R1C.2/3B: doSync FreeRTOS stack — bytes (ESP-IDF xTaskCreatePinnedToCore API).
// W-R1C.3B: 4608 B from W-R1C.3A high-water (max_used=3496, reserve≈1112).
constexpr uint32_t kDoSyncTaskStackBytes = 4608u;

enum n_Status_e { CONNECTED, SOFT_AP, FAILED, SDREADY };

class MyNetwork {
  public:
    n_Status_e status;
    struct tm timeinfo;
    bool firstRun, forceTimeSync, forceWeather;
    bool lostPlaying = false, beginReconnect = false;
    // S6V9G: true after recoverySuspendReconnectForSetup — WiFiLostConnection must not drive LOST/reconnect during LVGL Setup.
    // S6V9G: после suspend — игнорируем runtime disconnect в Wi-Fi Setup / Recovery.
    bool runtimeReconnectSuspendedForSetup = false;
    //uint8_t tsFailCnt, wsFailCnt;
    Ticker ctimer;
    char *weatherBuf;
    bool trueWeather;
    // Stage 6.1C: compact Main glance (OWM code + °C) — filled in getWeather(); not full weatherBuf.
    // 6.1C: компактный glance на Main — код OWM + °C; не полная строка погоды.
    char weatherOwmIcon[8]{};
    float weatherLastTempC{0.f};
    bool weatherGlanceValid{false};
  public:
    MyNetwork() {};
    void begin();
    void requestTimeSync(bool withTelnetOutput=false, uint8_t clientId=0);
    // Legacy hook — intentionally no-op (was empty stub; do not re-arm forceWeather here).
    // Устаревший хук — намеренно no-op (не ставить forceWeather повторно).
    void requestWeatherSync();
    // A2b: explicit UI/manual refresh entry — sets forceWeather for one doSync cycle.
    // A2b: явный вход ручного refresh из UI — один цикл doSync.
    void forceWeatherRefreshFromUi();
    void setWifiParams();
    bool wifiBegin(bool silent=false);
    // Wi-Fi S6V7A: idempotent softAP for LVGL Recovery Hotspot / безопасный повторный подъём AP для Hotspot UI.
    void recoveryEnsureSoftAP();
    // S6V9C: stop SoftAP and cancel softapdelay when leaving LVGL Hotspot page / гасим AP и отменяем softapdelay.
    void recoveryStopSoftAP();
    // S6V9E: after runtime LOST timeout → LVGL Wi-Fi Recovery — stop driver/runtime reconnect; radio for Scan/Connect.
    // S6V9E: после таймаута LOST → Recovery — глушим auto/manual reconnect, радио для Scan/Connect.
    void recoverySuspendReconnectForSetup();
  private:
    Ticker rtimer;
    void raiseSoftAP();
    static void WiFiLostConnection(WiFiEvent_t event, WiFiEventInfo_t info);
    static void WiFiReconnected(WiFiEvent_t event, WiFiEventInfo_t info);
};

extern MyNetwork network;

bool networkResolveHostForConnect(const char* host, IPAddress& out, uint32_t timeoutMs = 5000);

extern __attribute__((weak)) void network_on_connect();

#endif
