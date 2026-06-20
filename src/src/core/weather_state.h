#ifndef weather_state_h
#define weather_state_h

#include <stdint.h>

/*
 * Weather W1 — core-owned weather model (current + hourly + daily snapshot).
 * Погодная модель, владелец — core: срезы current / hourly / daily.
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
 * W-R3: runtime metadata (fetch_in_progress, last_error, stale) lives in the same POD
 * snapshot; UI derives Empty/Loading/Ready/Stale/Unavailable without a broad status enum.
 * W-R3: runtime-метаданные в том же POD-снапшоте; UI выводит состояния из фактов.
 */

// W-R3/W-R4: single compile-time regular refresh interval (seconds).
// W-R3/W-R4: единый compile-time интервал регулярного обновления (секунды).
static constexpr uint32_t WEATHER_REGULAR_INTERVAL_SEC = 1800u;
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

// Single "now" snapshot. For W1 it is filled from the closest forecast point (list[0]).
// Срез «сейчас». В W1 заполняется ближайшей точкой прогноза (list[0]).
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
    char      condition[32];     // weather[0].description (truncated)
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
    WeatherCurrent current;
    WeatherHourly  hourly[WEATHER_HOURLY_SLOTS];
    WeatherDaily   daily[WEATHER_DAILY_SLOTS];
    bool           forecast_valid;        // true once a forecast was parsed at least once
    bool           stale;                 // W-R3: set on terminal failure when LKG payload exists
    uint32_t       forecast_updated_at;   // millis() at last successful payload publish
    int32_t        forecast_tz_sec;       // OWM city.timezone at publish (s); runtime-only, not NVS
    bool           fetch_in_progress;     // W-R3: doSync forecast attempt in flight
    WeatherLastError last_error;          // W-R3: last completed attempt outcome (runtime only)
    uint32_t       last_attempt_at_ms;    // W-R3: millis() at last begin/complete/defer
    uint32_t       version;               // monotonic payload publish counter (0 = never published)
};

// Publish a fully-built state (writer / core weather-sync context only).
// Double-buffered: copies src into the inactive buffer, then flips atomically.
// Resets W-R3 runtime fields to Ready semantics (see weather_state.cpp).
// Публикация готового состояния (только писатель в weather-sync контексте).
void weatherPublishState(const WeatherState& src);

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
