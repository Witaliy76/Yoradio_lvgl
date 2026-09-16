#ifndef weather_state_h
#define weather_state_h

#include <stdint.h>
#include <math.h>

/*
 * Shared weather model (current + hourly + daily snapshot), owned by core.
 * Погодная модель (current / hourly / daily), владелец — core.
 *
 * Ownership / lifecycle:
 *   - Writer: weather-sync context only (core, doSync on Core 0) via weatherPublishState().
 *   - Readers: any task (DspTask/LVGL later, telnet, etc.) via weatherGetStateSnapshot().
 *   - This module holds NO network/JSON code — it is a passive, compact data sink.
 *
 * Владелец/жизненный цикл:
 *   - Писатель: только weather-sync контекст (core, doSync, Core 0) — weatherPublishState().
 *   - Читатели: любая задача (позже DspTask/LVGL) — weatherGetStateSnapshot().
 *   - Здесь нет сетевого/JSON кода — только компактное хранилище состояния.
 *
 * Runtime metadata (fetch_in_progress, last_error, stale) lives in the same POD
 * snapshot; UI derives Empty/Loading/Ready/Stale/Unavailable without a broad status enum.
 * Runtime-метаданные в том же POD-снапшоте; UI выводит состояния из фактов.
 *
 * Author: Witaliy76 - https://github.com/Witaliy76
 */

// POST-S6: canonical hPa→mmHg conversion, shared by UI display and serial diagnostics
// so the two never drift onto separate formulas. Internal state stays hPa (§8); mmHg
// is a presentation-only conversion applied at render/log time.
// POST-S6: единая конвертация hPa→мм.рт.ст., общая для UI и serial-диагностики, чтобы
// не разошлись на две формулы. Внутреннее состояние остаётся в hPa; мм.рт.ст. — только
// для отображения/лога.
static constexpr float kHpaToMmHg = 0.750061683f;
static inline int weatherHpaToMmHg(float hpa) {
    return static_cast<int>(lroundf(hpa * kHpaToMmHg));
}

// W-R3/W-R4: single compile-time regular refresh interval (seconds).
// W-R3/W-R4: единый compile-time интервал регулярного обновления (секунды).
static constexpr uint32_t WEATHER_REGULAR_INTERVAL_SEC = 3600u;
static constexpr uint32_t WEATHER_STALE_AFTER_MS =
    WEATHER_REGULAR_INTERVAL_SEC * 2u * 1000u;

// Slots stored may exceed what a future Weather Page renders (store more than UI needs).
// Слотов хранится больше, чем потребуется UI (запас на будущее).
static constexpr uint8_t WEATHER_HOURLY_SLOTS = 8;  // future hourly strip / будущая почасовая лента
static constexpr uint8_t WEATHER_DAILY_SLOTS  = 5;  // up to 5 days from 3h forecast / до 5 дней

// W-R3: compact terminal/deferred error for UI derivation — runtime only, not NVS.
// W-R3: компактная ошибка для вывода UI — только runtime, не NVS.
enum class WeatherLastError : uint8_t {
    None = 0,
    FetchFailed,
    InternalLow,
    NotConfigured,
    NotConnected,
};

// A4.0: location metadata (city + country) shared by current and forecast sources.
// A4.0: метаданные локации (город + страна) — общие для current и forecast.
// Stored at WeatherState root — belongs to the observation point, not to any single endpoint.
// Хранится на уровне WeatherState root — относится к точке наблюдения, а не к эндпоинту.
struct WeatherLocation {
    bool valid;
    char city[64];    // UTF-8 NUL-terminated; strlcpy-safe truncation; localized OWM names
    char country[4];  // ISO 3166-1 alpha-2 NUL-terminated, e.g. "RU\0"
};

// A4.0: source that populated WeatherState.current in this snapshot.
// A4.0: источник, заполнивший WeatherState.current в этом снапшоте.
// Publication metadata — not a meteorological value; stored at WeatherState root.
// Метаданные публикации, не метеовеличина; хранится на уровне root.
enum class WeatherCurrentSource : uint8_t {
    None             = 0,  // never published
    CurrentEndpoint,       // true /weather response
    ForecastFallback,      // derived from /forecast list[0]
};

// A4.0: current conditions — may now be true /weather endpoint data or forecast proxy.
// A4.0: текущие условия — может быть true данные /weather или прокси из /forecast.
struct WeatherCurrent {
    bool      valid;
    float     temp_c;
    float     feels_like_c;
    uint8_t   humidity;          // %
    uint16_t  pressure_hpa;      // hPa (raw OWM main.pressure)
    float     wind_speed;        // units per request (m/s for metric)
    uint16_t  wind_deg;
    uint8_t   rain_probability;  // 0..100 (pop * 100)
    char      owm_icon[4];       // e.g. "01d"
    uint16_t  owm_code;          // weather[0].id
    char      condition[64];     // weather[0].description; 64 B for UTF-8 localized descriptions
    uint32_t  updated_at;        // OWM dt of the source point (UTC seconds)
};

// One forecast point (3-hour step).
// Одна точка прогноза (шаг 3 часа).
struct WeatherHourly {
    bool      valid;
    uint32_t  ts;                // OWM dt (UTC seconds)
    float     temp_c;
    uint8_t   rain_probability;  // 0..100
    char      owm_icon[4];
    uint16_t  owm_code;
};

// Aggregated day built from 3-hour points (min/max + dominant condition).
// Агрегированный день из 3-часовых точек (min/max + доминирующее условие).
struct WeatherDaily {
    bool      valid;
    uint32_t  day_ts;            // representative dt for the day (UTC seconds)
    float     temp_min_c;
    float     temp_max_c;
    uint8_t   rain_probability_max;  // 0..100
    char      owm_icon[4];
    uint16_t  dominant_owm_code;     // by severe-weather priority
};

// Full published snapshot. POD — trivially copyable for double-buffer publish/read.
// Полный публикуемый снапшот. POD — тривиально копируется для double-buffer.
struct WeatherState {
    WeatherCurrent       current;
    WeatherHourly        hourly[WEATHER_HOURLY_SLOTS];
    WeatherDaily         daily[WEATHER_DAILY_SLOTS];
    bool                 forecast_valid;        // true once a forecast was parsed at least once
    bool                 stale;                 // W-R3: set on terminal failure when LKG payload exists
    uint32_t             forecast_updated_at;   // millis() at last successful payload publish
    int32_t              forecast_tz_sec;        // OWM city.timezone at publish (s); runtime-only, not NVS
    bool                 fetch_in_progress;     // W-R3: doSync forecast attempt in flight
    WeatherLastError     last_error;            // W-R3: last completed attempt outcome (runtime only)
    uint32_t             last_attempt_at_ms;    // W-R3: millis() at last begin/complete/defer
    uint32_t             version;               // monotonic payload publish counter (0 = never published)
    // A4.0: publication metadata — source of current conditions + observation location.
    // A4.0: метаданные публикации — источник текущих условий и локация наблюдения.
    WeatherCurrentSource current_source;        // which endpoint provided current data
    WeatherLocation      location;              // city/country shared by current + forecast
};

// Publish a fully-built state (writer / core weather-sync context only).
// Double-buffered: copies src into the inactive buffer, then flips atomically.
// Resets W-R3 runtime fields to Ready semantics (stale=false, last_error=None).
// Публикация готового состояния (только писатель в weather-sync контексте).
void weatherPublishState(const WeatherState& src);

// A4.0: version-bumping publish with explicit result metadata — one seqlock flip.
// For partial-success paths (Case C / §11): overlaid payload + chosen last_error + stale.
// Equivalent to weatherPublishState() but sets last_error and stale from caller instead
// of forcing them to None/false.
// A4.0: публикация с version++ и явными метаданными результата — один seqlock flip.
// Для частичного успеха (Case C / §11): наложенный payload + выбранные last_error и stale.
void weatherPublishStateWithResult(const WeatherState& payload,
                                   WeatherLastError error,
                                   bool stale);

// W-R3: status-only updates — copy active payload, mutate metadata, publish without version bump.
// W-R3: только метаданные — копия активного payload, без увеличения version.
void weatherStateMarkFetchBegin();
void weatherStateMarkFetchDeferred();
void weatherStateMarkFetchFailed(WeatherLastError error);

// Lock-free reader snapshot copy (seqlock + double-buffer). Safe from DspTask/LVGL,
// including cross-core readers on ESP32-S3 (std::atomic acquire/release, no mutex).
// Returns true if a consistent snapshot was copied; check out->forecast_valid for data presence.
// Чтение снапшота без mutex (seqlock + double-buffer); безопасно между ядрами ESP32-S3.
bool weatherGetStateSnapshot(WeatherState* out);

#endif // weather_state_h
