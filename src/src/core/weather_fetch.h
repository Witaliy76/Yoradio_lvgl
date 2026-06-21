#ifndef weather_fetch_h
#define weather_fetch_h

#include <cstdint>
#include "weather_state.h"  // A4.0: WeatherLocation, WeatherCurrentSource used by staging types

/*
 * Weather W1 — OpenWeatherMap 5 day / 3 hour forecast fetch.
 * Weather W1 — загрузка прогноза OWM 5 дней / 3 часа.
 *
 * Transport: plain HTTP (api.openweathermap.org:80), NO TLS — deliberately, so the
 * forecast path does not spin up a second mbedTLS context competing with the AI HTTPS
 * provider for internal heap. Mirrors the existing current-weather request.
 * Транспорт: обычный HTTP (порт 80), без TLS — намеренно, чтобы не поднимать второй
 * mbedTLS-контекст рядом с AI HTTPS. Повторяет подход текущего запроса погоды.
 *
 * Ownership / threading:
 *   - MUST be called only from the weather-sync context (core, doSync on Core 0).
 *   - NEVER call from LVGL/DspTask or any UI path — no network access from UI.
 *   - On success publishes a fresh WeatherState; on any failure keeps last-known-good.
 *   - Только из weather-sync контекста (core, doSync, Core 0); никогда из UI/LVGL.
 *   - При успехе публикует новый WeatherState; при ошибке — keep last-known-good.
 *
 * `units` / `lang` are passed in by the caller (locale-owned PROGMEM strings) to avoid
 * coupling this core module to the display-layer l10n include chain / L10N_LANGUAGE.
 * Copied from PROGMEM to RAM inside weatherFetchForecast() before snprintf / logs.
 * `units`/`lang` передаёт вызывающий (PROGMEM из locale); внутри копируются в RAM.
 *
 * Fetch outcome — lets doSync distinguish deferred low-memory from other failures.
 * Результат fetch — doSync отличает отложенный low-memory от прочих ошибок.
 */
enum class WeatherForecastFetchResult : uint8_t {
    Published,            // parsed + published / разобран и опубликован
    NotConfigured,        // weather off or no API key / погода выкл или нет ключа
    NotConnected,         // Wi-Fi not ready / Wi-Fi не готов
    DeferredInternalLow,  // internal heap guard — pending retry / гард heap — отложенный retry
    Failed,               // network/HTTP/parse/other guard / сеть/HTTP/parse/другой гард
};

// HF-W-DNS: forward declaration only — full include in weather_fetch.cpp.
// Using a reference to WeatherEdgeSession does not require complete type in the header.
struct WeatherEdgeSession;

// A4.0: temporary staging struct — true current conditions from the /weather endpoint.
// Populated by weatherParseCurrentBody(); consumed by weatherFetchForecast() for overlay.
// A4.0: временная структура — истинные текущие условия с эндпоинта /weather.
// Заполняется weatherParseCurrentBody(); потребляется weatherFetchForecast() для overlay.
struct WeatherTrueCurrent {
    bool      valid;
    float     temp_c;
    float     feels_like_c;
    uint8_t   humidity;
    uint16_t  pressure_hpa;   // raw OWM main.pressure (or grnd_level if present), hPa
    float     wind_speed;
    uint16_t  wind_deg;       // raw degrees 0-359
    char      owm_icon[4];
    uint16_t  owm_code;       // weather[0].id
    char      condition[64];  // weather[0].description; 64 B for UTF-8 localized descriptions
    uint32_t  updated_at;     // OWM dt (UTC seconds)
    WeatherLocation location; // name + sys.country from /weather response
};

// A4.0: full parse result from weatherParseCurrentBody().
// Carries WeatherTrueCurrent for WeatherState plus legacy fields for weatherBuf formatting.
// A4.0: полный результат парсинга — WeatherTrueCurrent для WeatherState + legacy поля для weatherBuf.
// Not changed now: legacy fields will be removed in A4.2 when getWeather is fully retired.
struct WeatherCurrentParsed {
    WeatherTrueCurrent tc;        // A4.0 state fields
    int    pressure_mmhg;         // hPa→mmHg for legacy weatherBuf (with GRND_HEIGHT adjustment)
    int    wind_dir_idx;          // 0..15 compass index for wind[] PROGMEM array
    bool   has_gust;              // true if wind.gust present in response
    int    gust_mps;              // gust speed m/s integer
    char   full_desc[120];        // full weather[0].description (tc.condition is 64 B; full_desc for weatherBuf)
    char   humidity_str[8];       // humidity as decimal string for legacy %s format
};

// A4.0: parse /weather JSON body (already read from HTTP stream).
// Uses ArduinoJson v7 + PSRAM allocator. Returns true on success.
// A4.0: парсинг тела /weather (уже прочитанного из потока). ArduinoJson v7 + PSRAM. true при успехе.
bool weatherParseCurrentBody(const char* body, WeatherCurrentParsed* out);

// A4.0: Case C / §11 — overlay true current over the active LKG forecast payload.
// Copies LKG, overlays current fields + location, bumps version, publishes.
// Used when current succeeded but forecast failed or was deferred.
// A4.0: Case C / §11 — наложить true current на активный LKG прогноз, version++, опубликовать.
// Используется когда current успешен, а forecast провалился или отложен.
void weatherPublishCurrentOverLkg(const WeatherTrueCurrent& tc,
                                   WeatherLastError error,
                                   bool make_stale);

// session carries cycle-level preferred edge (from current weather) and is used for
// request-level deduplication inside the forecast transport loop.
// session несёт cycle-level preferred (от current) и request-level дедупликацию в transport loop.
// A4.0: true_current optional overlay — nullptr for pending-only path.
// A4.0: необязательный overlay — nullptr для pending-only пути (без /weather запроса).
WeatherForecastFetchResult weatherFetchForecast(const char* units, const char* lang,
                                                WeatherEdgeSession& session,
                                                const WeatherTrueCurrent* true_current = nullptr);

/*
 * A2b: request an async weather refresh from UI or other non-network tasks.
 * Sets the core scheduler flag only — HTTP runs later in doSync (Core 0), never here.
 * A2b: асинхронный запрос обновления из UI; только флаг — HTTP позже в doSync, не здесь.
 */
void weatherRequestManualRefresh();

// W-R1C.1: coalesced deferred forecast — one pending flag, forecast-only retry in doSync.
// W-R1C.1: отложенный прогноз — один pending, retry только forecast в doSync.
void weatherForecastMarkPending();
bool weatherForecastIsPending();
bool weatherForecastPollPending();       // ticks: admission + cooldown; may arm pending-only doSync
bool weatherForecastTakePendingOnlyRun(); // doSync: consume one forecast-only pass
void weatherForecastDiscardPendingOnlyArm(); // doSync: coalesce when full forceWeather runs

#endif // weather_fetch_h
