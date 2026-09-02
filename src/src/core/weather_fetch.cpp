// OpenWeatherMap forecast fetch implementation.
// Author: Witaliy76 - https://github.com/Witaliy76
#include "weather_fetch.h"
#include "weather_state.h"
#include "config.h"          // config.store.*, options.h (HIDE_WEATHER), Config
#include "network.h"         // network, networkResolveHostForConnect()
#include "weather_edge_session.h" // HF-W-DNS: edge session for forecast transport
#include "net_dns_resolver.h"     // HF-W-DNS: generic selected-DNS resolver

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>     // v7.4.2
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <cstring>
#include <climits>  // INT32_MAX/MIN for owm_local_day_key bounds / границы day key

/*
 * Weather W1 — forecast fetch + parse + daily aggregation + publish.
 * See weather_fetch.h for ownership/threading contract.
 *
 * Memory strategy (per W1 plan):
 *   - No full-body buffering into String / internal heap.
 *   - ArduinoJson v7 JsonDocument backed by a PSRAM allocator.
 *   - DeserializationOption::Filter keeps only the fields we need.
 *   - Deserialize directly from the WiFiClient stream (after HTTP headers).
 *   - Final WeatherState is compact and lives in internal RAM (this TU's .bss builder
 *     + the double buffer in weather_state.cpp).
 */

#if !defined(HIDE_WEATHER)



// HF-W-DNS: forecast transport failure-injection hooks — default OFF in production.
// Enable locally in src/myoptions.h for runtime acceptance tests only.
// Включить локально в myoptions.h только для acceptance-тестов.
#ifndef YORADIO_WEATHER_EDGE_TEST_FORECAST_SYSTEM_CONNECT_FAIL
#define YORADIO_WEATHER_EDGE_TEST_FORECAST_SYSTEM_CONNECT_FAIL 0
#endif
#ifndef YORADIO_WEATHER_EDGE_TEST_FORECAST_PREFERRED_READ_WAIT
#define YORADIO_WEATHER_EDGE_TEST_FORECAST_PREFERRED_READ_WAIT 0
#endif
#ifndef YORADIO_WEATHER_EDGE_TEST_FORECAST_DNS0_FAIL
#define YORADIO_WEATHER_EDGE_TEST_FORECAST_DNS0_FAIL 0
#endif

// Heap guards — shared by fetch + pending poll / гарды heap для fetch и pending poll
// 38 KB is a conservative operational floor calibrated against observed radio + WebSocket
// runtime. Forecast parsing uses PSRAM; internal heap is still protected from genuinely
// low-memory execution.
// 38 КБ — консервативный операционный порог по наблюдениям radio + WebSocket; парсинг в PSRAM.
constexpr size_t kMinInternalFreeBytes = 38u * 1024u;  // in-task fetch guard / гард внутри fetch
constexpr size_t kMinPsramBlockBytes   = 24u * 1024u;

// W-R1C.2/3B: pre-task admission — separate from kMinInternalFreeBytes (parser guard).
// Task-admission threshold for pending poll → doSync create; not the in-fetch parser floor.
// Порог admission для pending poll → doSync; это не гард парсера внутри fetch.
constexpr size_t kPendingTaskAdmissionMinFreeBytes = 45u * 1024u;  // 46080 — idle WebSocket + radio headroom

namespace {

// ── Tunables / Параметры ──────────────────────────────────────────────────────
constexpr uint8_t  kForecastCnt          = 40;          // 40 × 3h = 120h (OWM 5-day max)
constexpr uint16_t kMaxForecastPoints    = 48;          // hard cap on parsed points
constexpr uint32_t kConnectTimeoutMs     = 5000;
constexpr uint32_t kReadWaitTimeoutMs    = 4000;

// PSRAM-backed allocator for ArduinoJson v7 — keeps the parse arena out of internal heap.
// PSRAM-аллокатор для ArduinoJson v7 — арена парсинга вне internal heap.
struct PsramJsonAllocator final : ArduinoJson::Allocator {
    void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
    void  deallocate(void* p) override { heap_caps_free(p); }
    void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};

// Severe-weather priority for daily dominant condition.
// thunder > snow > rain > drizzle > fog/atmosphere > clouds > clear
// Приоритет суровости для доминирующего условия дня.
uint8_t owm_severity(uint16_t code) {
    const uint16_t grp = code / 100;
    if (grp == 2) return 6; // 2xx thunderstorm
    if (grp == 6) return 5; // 6xx snow
    if (grp == 5) return 4; // 5xx rain
    if (grp == 3) return 3; // 3xx drizzle
    if (grp == 7) return 2; // 7xx atmosphere (fog/mist/haze)
    if (code == 800) return 0; // clear
    if (grp == 8) return 1; // 80x clouds
    return 0;
}

void copy_icon(char dst[4], const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    strlcpy(dst, src, 4);
}

// Caller passes displayL10n PROGMEM strings; copy before RAM-only %s / snprintf.
// Вызывающий передаёт PROGMEM из displayL10n; копируем перед RAM-only %s / snprintf.
void copyWeatherLocaleProgmem(const char* progmemSrc, char* dst, size_t dstSize, const char* fallbackRam) {
    if (!dst || dstSize == 0) return;
    if (progmemSrc) {
        strncpy_P(dst, progmemSrc, dstSize - 1);
    } else if (fallbackRam) {
        strncpy(dst, fallbackRam, dstSize - 1);
    } else {
        dst[0] = '\0';
        return;
    }
    dst[dstSize - 1] = '\0';
}

uint8_t pop_to_pct(float pop) {
    if (pop < 0.0f) pop = 0.0f;
    if (pop > 1.0f) pop = 1.0f;
    long v = lroundf(pop * 100.0f);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    return static_cast<uint8_t>(v);
}

// W-R2: forecast-location calendar day key from UTC epoch + OWM timezone offset.
// W-R2: ключ локального календарного дня локации прогноза (UTC + city.timezone).
static inline int32_t owm_local_day_key(int64_t utcSeconds, int32_t timezoneOffsetSeconds) {
    const int64_t shifted = utcSeconds + static_cast<int64_t>(timezoneOffsetSeconds);
    int64_t day = shifted / 86400;
    if (shifted < 0 && (shifted % 86400) != 0) {
        --day;
    }
    if (day > INT32_MAX) return INT32_MAX;
    if (day < INT32_MIN) return INT32_MIN;
    return static_cast<int32_t>(day);
}

// File-scope scratch builder — kept off the 4 KB doSync stack on purpose.
// Single-threaded by contract (weather-sync context only).
// Скрэтч-билдер в .bss — намеренно не на 4 КБ стеке doSync; однопоточный по контракту.
WeatherState s_builder;

// ── Diagnostics / Диагностика ──────────────────────────────────────────────────
// Retained as no-op wrappers: the verbose bodies were removed with the FC diagnostics.
// Оставлены как пустые обёртки: подробные тела удалены вместе с FC-диагностикой.
void fc_log_reject(const char* reason, size_t int_free, size_t int_block,
                   size_t required, uint8_t pending) {
    (void)reason;
    (void)int_free;
    (void)int_block;
    (void)required;
    (void)pending;
}

void fc_log_reject_psram(const char* reason, size_t int_free,
                         size_t ps_free, size_t ps_block) {
    (void)reason;
    (void)int_free;
    (void)ps_free;
    (void)ps_block;
}

void fc_log_skip(const char* reason, size_t int_free, size_t ps_free, size_t ps_block) {
    fc_log_reject_psram(reason, int_free, ps_free, ps_block);
}

void fc_log_fail(const char* stage, uint32_t t0, int httpCode) {
    Serial.printf("[WEATHER_FC] fail stage=%s http=%d elapsed_ms=%lu\n",
                  stage, httpCode, (unsigned long)(millis() - t0));
}

void fc_log_parse_fail(const char* err, uint32_t t0, int httpCode) {
    Serial.printf("[WEATHER_FC] fail stage=parse err=%s http=%d elapsed_ms=%lu\n",
                  err, httpCode, (unsigned long)(millis() - t0));
}

void fc_log_success(uint32_t t0, int httpCode, uint16_t points, uint8_t future_days) {
    Serial.printf("[WEATHER_FC] success http=%d points=%u future_days=%u elapsed_ms=%lu next_s=%u\n",
                  httpCode, (unsigned)points, (unsigned)future_days, (unsigned long)(millis() - t0),
                  (unsigned)WEATHER_REGULAR_INTERVAL_SEC);
}

inline void fc_log_heap(const char*) {}
inline void fc_dump_published(uint16_t, uint8_t, uint8_t, int32_t) {}

// Read one CRLF-terminated line into buf (CR/LF stripped). Returns chars stored.
// Чтение одной строки до \n (CR/LF убираются). Возвращает число символов.
size_t read_line(WiFiClient& client, char* buf, size_t cap, uint32_t timeoutMs) {
    size_t n = 0;
    const uint32_t start = millis();
    while (n + 1 < cap) {
        int c = client.read();
        if (c < 0) {
            if (!client.connected() && client.available() == 0) break;
            if (millis() - start > timeoutMs) break;
            delay(2);
            continue;
        }
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[n++] = static_cast<char>(c);
    }
    buf[n] = '\0';
    return n;
}

} // namespace

// ── A4.0: current-conditions JSON parser (ArduinoJson v7, PSRAM allocator) ──────────────
// Single-parse path: feeds WeatherTrueCurrent (WeatherState) and ##WEATHER### diagnostic fields.
// Единый проход парсинга: кормит WeatherTrueCurrent (WeatherState) и поля для ##WEATHER###.
bool weatherParseCurrentBody(const char* body, WeatherCurrentParsed* out) {
    if (!body || !out) return false;

    PsramJsonAllocator alloc;

    // A4.0: filter — all required fields including new: weather[0].id, dt, sys.country, grnd_level, gust.
    // A4.0: фильтр — все нужные поля включая новые: id, dt, sys.country, grnd_level, gust.
    JsonDocument filter(&alloc);
    filter["weather"][0]["description"] = true;
    filter["weather"][0]["icon"]        = true;
    filter["weather"][0]["id"]          = true;
    filter["main"]["temp"]              = true;
    filter["main"]["feels_like"]        = true;
    filter["main"]["pressure"]          = true;
    filter["main"]["humidity"]          = true;
    filter["main"]["grnd_level"]        = true;  // preferred for mmHg if present
    filter["wind"]["speed"]             = true;
    filter["wind"]["deg"]               = true;
    filter["wind"]["gust"]              = true;  // optional; used by ##WEATHER### gust suffix
    filter["dt"]                        = true;
    filter["name"]                      = true;
    filter["sys"]["country"]            = true;

    JsonDocument doc(&alloc);
    const DeserializationError derr = deserializeJson(doc, body,
                                                      DeserializationOption::Filter(filter));
    if (derr) {
        Serial.printf("[WEATHER_FC] current parse fail err=%s\n", derr.c_str());
        return false;
    }

    // Required fields — bail if absent.
    if (doc["main"]["temp"].isNull() ||
        doc["weather"][0]["description"].isNull() ||
        doc["weather"][0]["icon"].isNull()) {
        Serial.println("[WEATHER_FC] current parse missing required fields");
        return false;
    }

    memset(out, 0, sizeof(*out));

    // ── Meteorological values (hPa, m/s, raw degrees) ──────────────────────
    out->tc.temp_c       = doc["main"]["temp"]       | 0.0f;
    out->tc.feels_like_c = doc["main"]["feels_like"] | out->tc.temp_c;
    out->tc.humidity     = doc["main"]["humidity"]   | 0;
    out->tc.wind_speed   = doc["wind"]["speed"]      | 0.0f;
    out->tc.wind_deg     = doc["wind"]["deg"]        | 0;
    out->tc.owm_code     = doc["weather"][0]["id"]   | 0;
    out->tc.updated_at   = doc["dt"]                 | 0u;

    // Pressure: prefer grnd_level (more accurate at altitude) over sea-level pressure.
    // Давление: grnd_level предпочтительнее sea-level при наличии.
    const int grnd_val  = doc["main"]["grnd_level"] | -1;
    const int pres_hpa  = doc["main"]["pressure"]   | 0;
    const bool has_grnd = (grnd_val > 0);
    out->tc.pressure_hpa = (uint16_t)(has_grnd ? grnd_val : pres_hpa);

    const char* icon = doc["weather"][0]["icon"] | "";
    const char* desc = doc["weather"][0]["description"] | "";
    copy_icon(out->tc.owm_icon, icon);
    strlcpy(out->tc.condition,  desc, sizeof(out->tc.condition));
    strlcpy(out->full_desc,     desc, sizeof(out->full_desc));

    // ── Location from /weather response ──────────────────────────────────────
    const char* city    = doc["name"]           | "";
    const char* country = doc["sys"]["country"] | "";
    if (city[0] != '\0') {
        out->tc.location.valid = true;
        strlcpy(out->tc.location.city,    city,    sizeof(out->tc.location.city));
        strlcpy(out->tc.location.country, country, sizeof(out->tc.location.country));
    }

    // ── Human-readable ##WEATHER### diagnostic helper fields ─────────────────
    // ── Вспомогательные поля для serial-диагностики ##WEATHER### ─────────────
    // pressure_mmhg: OWM hPa → mmHg with optional altitude adjustment.
    // Давление: OWM hPa → мм.рт.ст. с поправкой на высоту.
#ifndef GRND_HEIGHT
#define GRND_HEIGHT 0
#endif
    const int g_height = (int)((float)GRND_HEIGHT / 11.0f);
    if (has_grnd) {
        out->pressure_mmhg = (int)((float)grnd_val / 1.333f);       // no altitude adjustment
    } else {
        out->pressure_mmhg = (int)((float)pres_hpa / 1.333f) - g_height;
    }

    // wind_dir_idx: raw degrees → 0..15 compass index for wind[] PROGMEM array.
    out->wind_dir_idx = (int)((float)out->tc.wind_deg / 22.5f);
    if (out->wind_dir_idx > 15) out->wind_dir_idx = 15;
    if (out->wind_dir_idx < 0)  out->wind_dir_idx = 0;

    // gust (optional)
    const float gust_f = doc["wind"]["gust"] | 0.0f;
    out->has_gust = (gust_f > 0.05f);
    out->gust_mps = (int)gust_f;

    // humidity as decimal string for ##WEATHER### serial line.
    // Влажность строкой для serial-строки ##WEATHER###.
    snprintf(out->humidity_str, sizeof(out->humidity_str), "%u", (unsigned)out->tc.humidity);

    out->tc.valid = true;
    return true;
}

// A4.0: Case C / §11 — copy active LKG, overlay true-current fields, publish in one flip.
// A4.0: Case C / §11 — копируем LKG, накладываем true-current, публикуем за один flip.
//
// Called when /weather succeeded but /forecast failed (Case C) or was deferred (§11).
// Используется когда /weather успешен, а /forecast провалился (Case C) или отложен (§11).
//
// Single publication via weatherPublishStateWithResult():
//   - version++
//   - last_error  = supplied error
//   - stale       = (make_stale && forecast_valid)  for Case C
//                   previously-active stale value    for §11 (deferred, make_stale=false)
//   - one seqlock flip — no intermediate Ready state exposed to readers
// Единая публикация через weatherPublishStateWithResult() — нет промежуточного состояния.
void weatherPublishCurrentOverLkg(const WeatherTrueCurrent& tc,
                                   WeatherLastError error,
                                   bool make_stale) {
    WeatherState staged{};
    // Seed from active LKG — preserves existing hourly/daily/forecast payload and metadata.
    // Засеваем из активного LKG — сохраняем hourly/daily/forecast payload и метаданные.
    if (!weatherGetStateSnapshot(&staged)) {
        // Snapshot unavailable (seqlock busy) — still publish current so data is not lost.
        // Снапшот недоступен — публикуем current чтобы данные не потерялись.
        memset(&staged, 0, sizeof(staged));
    }

    // Stale policy (computed before overlay, from the LKG snapshot):
    // Case C (make_stale=true):  stale iff LKG already had forecast data.
    // §11   (make_stale=false): preserve existing stale — deferred must not clear prior stale.
    // Политика stale: Case C → stale если был forecast; §11 → сохраняем prior stale.
    const bool publish_stale = make_stale ? staged.forecast_valid : staged.stale;

    // Overlay true-current meteorological fields.
    // Накладываем метеоданные true-current.
    staged.current.valid         = true;
    staged.current.temp_c        = tc.temp_c;
    staged.current.feels_like_c  = tc.feels_like_c;
    staged.current.humidity      = tc.humidity;
    staged.current.pressure_hpa  = tc.pressure_hpa;
    staged.current.wind_speed    = tc.wind_speed;
    staged.current.wind_deg      = tc.wind_deg;
    copy_icon(staged.current.owm_icon, tc.owm_icon);
    staged.current.owm_code      = tc.owm_code;
    strlcpy(staged.current.condition, tc.condition, sizeof(staged.current.condition));
    staged.current.updated_at    = tc.updated_at;
    // rain_probability: /weather has no pop field — preserve LKG forecast proxy value.
    // rain_probability: у /weather нет pop — сохраняем прокси из LKG прогноза.

    staged.current_source = WeatherCurrentSource::CurrentEndpoint;

    // Location priority: current > existing LKG location.
    // Приоритет локации: current > существующий LKG.
    if (tc.location.valid) {
        staged.location = tc.location;
    }
    // else: staged.location from LKG is preserved.


    // Single-flip version-bumping publication — no intermediate state, no double flip.
    // Единая публикация с version++ — нет промежуточного состояния, нет двойного flip.
    weatherPublishStateWithResult(staged, error, publish_stale);
}

// W-R1C.1: pending forecast state — file .bss, Core0/doSync path only (not WebUI/LVGL).
// W-R1C.1: состояние отложенного прогноза — .bss, только Core0/doSync.
static bool     s_fc_pending = false;
static uint32_t s_fc_pending_check_ms = 0;
static bool     s_fc_pending_only_run = false;
constexpr uint32_t kPendingCheckIntervalMs = 5000;

void weatherForecastMarkPending() {
    s_fc_pending = true;
}

bool weatherForecastIsPending() {
    return s_fc_pending;
}

bool weatherForecastTakePendingOnlyRun() {
    if (!s_fc_pending_only_run) {
        return false;
    }
    s_fc_pending_only_run = false;
    return true;
}

void weatherForecastDiscardPendingOnlyArm() {
    s_fc_pending_only_run = false;
}

// ticks() path: rate-limited heap poll; arms forecast-only doSync when admission passes.
// ticks(): опрос heap с cooldown; arm только при прохождении admission-порога.
bool weatherForecastPollPending() {
    if (!s_fc_pending) {
        return false;
    }
    if (!config.store.showweather || strlen(config.store.weatherkey) == 0) {
        return false;
    }
    if (network.status != CONNECTED) {
        return false;
    }

    const uint32_t now = millis();
    if ((uint32_t)(now - s_fc_pending_check_ms) < kPendingCheckIntervalMs) {
        return false;
    }

    const size_t int_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t int_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const size_t ps_free   = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t ps_block  = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    s_fc_pending_check_ms = now;

    if (ps_free == 0 || ps_block < kMinPsramBlockBytes) {
        fc_log_reject_psram("psram_low", int_free, ps_free, ps_block);
        return false;
    }

    if (int_free < kPendingTaskAdmissionMinFreeBytes ||
        int_block < static_cast<size_t>(kDoSyncTaskStackBytes)) {
        const bool heap_low  = int_free < kPendingTaskAdmissionMinFreeBytes;
        const bool stack_low = int_block < static_cast<size_t>(kDoSyncTaskStackBytes);
        // Distinct reasons — heap floor vs contiguous block for doSync stack allocation.
        // Разные причины — порог heap vs непрерывный блок под стек doSync.
        const char* reason = (heap_low && stack_low) ? "pending_heap_and_stack_low"
                             : heap_low ? "pending_heap_low"
                             : "pending_stack_block_low";
        fc_log_reject(reason, int_free, int_block, kPendingTaskAdmissionMinFreeBytes, 1);
        return false;
    }

    s_fc_pending_only_run = true;
    return true;
}

WeatherForecastFetchResult weatherFetchForecast(const char* units, const char* lang,
                                                WeatherEdgeSession& session,
                                                const WeatherTrueCurrent* true_current) {
    // Gate mirrors the current-weather enablement; keep behavior independent of UI.
    // Гейт повторяет включение текущей погоды; не зависит от UI.
    if (!config.store.showweather || strlen(config.store.weatherkey) == 0) {
        return WeatherForecastFetchResult::NotConfigured;
    }
    if (network.status != CONNECTED) {
        return WeatherForecastFetchResult::NotConnected;
    }

    // displayL10n_*.h: units/lang are PROGMEM — not safe for snprintf %s as-is.
    // displayL10n_*.h: units/lang в PROGMEM — нельзя напрямую в snprintf %s.
    char unitsRam[12];  // metric | imperial | standard
    char langRam[8];    // en | ru | …
    copyWeatherLocaleProgmem(units, unitsRam, sizeof(unitsRam), "metric");
    copyWeatherLocaleProgmem(lang, langRam, sizeof(langRam), "en");
    const char* unitsUse = unitsRam;
    const char* langUse  = langRam;

    const uint32_t t0 = millis();
    fc_log_heap("before");

    // ── Guards: internal heap + PSRAM must have headroom before parse-heavy work. ──
    // Гарды: проверяем internal heap и PSRAM до тяжёлого парсинга.
    // DeferredInternalLow returns BEFORE any DNS fallback work — no edge session consumed.
    // DeferredInternalLow — до любой работы с DNS; edge-сессия не трогается.
    const size_t int_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t ps_free   = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t ps_block  = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (ps_free == 0 || ps_block < kMinPsramBlockBytes) {
        fc_log_skip("psram_low", int_free, ps_free, ps_block);
        return WeatherForecastFetchResult::Failed; // keep last-known-good
    }
    if (int_free < kMinInternalFreeBytes) {
        const size_t int_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        fc_log_reject("internal_low", int_free, int_block, kMinInternalFreeBytes, 1);
        weatherForecastMarkPending();
        return WeatherForecastFetchResult::DeferredInternalLow;
    }

    // Real attempt starting — clear pending before network/parse work.
    // Реальная попытка — сбрасываем pending до сети/парсинга.
    if (s_fc_pending) {
        s_fc_pending = false;
    }


    // ── HF-W-DNS: lazy edge-fallback transport loop ──────────────────────────────
    // Reset request-level tracking; cycle-level preferred from current is preserved.
    // Сбрасываем request-level; cycle-level preferred от current сохраняется.
    session.beginRequest();

    const char* host = "api.openweathermap.org";
    const bool usePreferred = session.hasPreferred();
    const uint8_t prefResolverIdx = usePreferred ? session.getPreferredResolverIndex() : 0u;

    size_t fbCount = 0;
    const NetDnsServer* fbServers = netDnsDefaultFallbackServers(fbCount);

    // Attempt sequence:
    //   usePreferred=false: [system_dns, fbServers[0], fbServers[1]]  — max 3 TCP
    //   usePreferred=true:  [preferred_IP, fbServers[prefIdx+1], ...] — max 2 TCP (CF) or 1 (Q9)
    // No system DNS retry when preferred is set — system was already transport-bad this cycle.
    // При наличии preferred — system DNS не повторяется (он уже провалился в этом цикле).
    WiFiClient client;
    bool fc_connected = false;

    // Build the forecast request once — same for all edge attempts.
    // static → off the doSync stack. Single-threaded by contract.
    // static → вне стека doSync. Однопоточно по контракту.
    static char fc_req[320];
    snprintf(fc_req, sizeof(fc_req),
             "GET /data/2.5/forecast?lat=%s&lon=%s&units=%s&lang=%s&cnt=%u&appid=%s HTTP/1.0\r\n"
             "Host: %s\r\nConnection: close\r\n\r\n",
             config.store.weatherlat, config.store.weatherlon, unitsUse, langUse,
             (unsigned)kForecastCnt, config.store.weatherkey, host);

    for (uint8_t attemptIdx = 0; attemptIdx < kWeatherEdgeMaxAttempts; ++attemptIdx) {
        IPAddress edgeIP;
        const char* sourceName = "system";
        bool isReuse = false;

        // ── Determine which edge to try this iteration ───────────────────────
        if (usePreferred && attemptIdx == 0) {
            // Use the preferred edge from the current request directly — no DNS query.
            // Reusing known-good IP from current weather (same cycle).
            // Используем known-good IP от current (тот же цикл) — без DNS-запроса.
            edgeIP     = session.getPreferred();
            sourceName = (prefResolverIdx < (uint8_t)fbCount)
                         ? fbServers[prefResolverIdx].name : "preferred";
            isReuse    = true;

        } else if (!usePreferred && attemptIdx == 0) {
            // System / router DNS — same path as existing code.
            // Системный DNS — тот же путь, что и в исходном коде.
            if (!networkResolveHostForConnect(host, edgeIP)) {
                fc_log_fail("dns", t0, -1);
                continue; // DNS failed → try fallback resolvers
            }
            sourceName = "system";

        } else {
            // Fallback resolver (Cloudflare, Quad9).
            // When usePreferred: skip resolvers up to and including prefResolverIdx.
            // When !usePreferred: attempt 0 was system; subsequent = resolver[0], resolver[1].
            // При usePreferred: пропускаем резолверы до prefResolverIdx включительно.
            uint8_t fbSlot;
            if (usePreferred) {
                // attempt 0 = preferred; attempt 1,2 = resolvers after prefResolverIdx
                fbSlot = prefResolverIdx + attemptIdx; // attemptIdx >= 1
            } else {
                // attempt 0 = system; attempt 1,2 = resolver[0], resolver[1]
                fbSlot = (uint8_t)(attemptIdx - 1u);
            }

            if (fbSlot >= (uint8_t)fbCount) break; // no more configured resolvers

            sourceName = fbServers[fbSlot].name;

#if YORADIO_WEATHER_EDGE_TEST_FORECAST_DNS0_FAIL
            if (fbSlot == 0) {
                Serial.printf("[WEATHER_NET] path=forecast dns resolver=%s INJECTED_FAIL\n",
                              sourceName);
                continue; // simulate DNS0 failure
            }
#endif

            // Query the selected fallback DNS server.
            // Запрашиваем выбранный fallback DNS-сервер.
            IPAddress candidates[4];
            size_t candidateCount = 0;
            const NetDnsQueryStatus dnsStatus = netDnsQueryA(
                host, fbServers[fbSlot].address,
                candidates, 4, candidateCount,
                800u, "weather-forecast");


            if (dnsStatus != NetDnsQueryStatus::Success || candidateCount == 0) {
                continue; // DNS query failed → try next resolver
            }

            // Select first candidate not already attempted by this forecast request.
            // Выбираем первый кандидат, не пробованный в данном forecast-запросе.
            edgeIP = IPAddress(0, 0, 0, 0);
            for (size_t ci = 0; ci < candidateCount; ++ci) {
                if (!session.wasAttempted(candidates[ci])) {
                    edgeIP = candidates[ci];
                    break;
                }
            }

            if (edgeIP == IPAddress(0, 0, 0, 0)) {
                continue; // all candidates already attempted
            }
        }

        // ── Deduplication (covers preferred reuse too, in case of repeated cycle edge) ──
        if (session.wasAttempted(edgeIP)) {
            continue;
        }
        session.markAttempted(edgeIP);


        // ── TCP connect ──────────────────────────────────────────────────────
        client.stop(); // clean state before each attempt

#if YORADIO_WEATHER_EDGE_TEST_FORECAST_SYSTEM_CONNECT_FAIL
        if (!usePreferred && attemptIdx == 0) {
            Serial.printf("[WEATHER_NET] path=forecast fail attempt=0 source=system "
                          "stage=connect INJECTED_FAIL\n");
            continue; // simulate system connect failure
        }
#endif
#if YORADIO_WEATHER_EDGE_TEST_FORECAST_PREFERRED_READ_WAIT
        // Inject read_wait failure on preferred edge (attempt 0 when usePreferred).
        bool _inject_preferred_rw = (usePreferred && attemptIdx == 0);
#else
        constexpr bool _inject_preferred_rw = false;
#endif

        if (!client.connect(edgeIP, 80, kConnectTimeoutMs)) {
            continue; // ConnectFailed → try next edge
        }

        // ── Send request ─────────────────────────────────────────────────────
        // HTTP/1.0 → non-chunked body, safe to stream straight into the parser.
        // HTTP/1.0 → тело без chunked, поток можно отдавать прямо парсеру.
        client.print(fc_req);

        // ── Wait for first response byte ──────────────────────────────────────
        // Qualifying failure: no bytes before ANY status/header line arrives.
        // Do NOT rotate on body/parse failures — only on pre-status transport failures.
        // Ротация только при сбое до первого байта HTTP-ответа; не при body/parse ошибках.
        {
            bool gotByte = false;

#if YORADIO_WEATHER_EDGE_TEST_FORECAST_PREFERRED_READ_WAIT
            if (_inject_preferred_rw) {
                Serial.printf("[WEATHER_NET] path=forecast fail attempt=0 source=%s "
                              "stage=read_wait_before_status INJECTED_FAIL\n", sourceName);
                client.stop();
                continue; // simulate read_wait before status
            }
#endif

            const uint32_t rwStart = millis();
            while ((uint32_t)(millis() - rwStart) < kReadWaitTimeoutMs) {
                if (client.available() > 0) { gotByte = true; break; }
                if (!client.connected()) break;
                delay(5);
            }
            if (!gotByte) {
                fc_log_fail("read_wait", t0, -1);
                client.stop();
                continue; // ReadWaitBeforeStatus → try next edge
            }
        }

        // Bytes arrived — this edge is usable for the HTTP response.
        // After this point: do NOT rotate on HTTP-status or parse failures.
        // После первого байта — НЕ ротируем по HTTP-статусу или ошибкам парсинга.
        fc_connected = true;
        break;
    } // end attempt loop

    if (!fc_connected) {
        fc_log_fail("connect_all_edges", t0, -1);
        return WeatherForecastFetchResult::Failed;
    }

    // ── Response parse — unchanged from baseline; client has bytes available. ──
    // Парсинг ответа — без изменений; client содержит доступные байты.

    // Status line: "HTTP/1.0 200 OK".
    int httpCode = -1;
    {
        static char line[96]; // HF1: 96 B off stack → .bss / 96 Б со стека в .bss
        read_line(client, line, sizeof(line), kReadWaitTimeoutMs);
        const char* sp = strchr(line, ' ');
        if (sp) httpCode = atoi(sp + 1);
    }
    if (httpCode != 200) {
        client.stop();
        fc_log_fail("http", t0, httpCode);
        return WeatherForecastFetchResult::Failed;
    }

    // Skip headers until the blank separator line.
    {
        static char line[160]; // HF1: 160 B off stack → .bss / 160 Б со стека в .bss
        bool ended = false;
        for (uint8_t i = 0; i < 64; ++i) { // bounded header scan
            const size_t n = read_line(client, line, sizeof(line), kReadWaitTimeoutMs);
            if (n == 0) { ended = true; break; } // blank line (CR/LF already stripped)
            if (!client.connected() && client.available() == 0) break;
        }
        if (!ended) {
            // Could not find header terminator → bail without publishing.
            client.stop();
            fc_log_fail("headers", t0, httpCode);
            return WeatherForecastFetchResult::Failed;
        }
    }

    // ── Parse: PSRAM-backed JsonDocument + Filter + stream deserialization. ──
    PsramJsonAllocator alloc;

    JsonDocument filter(&alloc);
    filter["city"]["timezone"] = true; // for local-day grouping and A4.0 publication
    // A4.0: city.name + city.country always in filter — needed for WeatherState.location.
    // A4.0: city.name + city.country всегда в фильтре — нужны для WeatherState.location.
    filter["city"]["name"]    = true;
    filter["city"]["country"] = true;
    // Array filter template: applies to every element of "list".
    // Шаблон фильтра для массива: применяется к каждому элементу "list".
    filter["list"][0]["dt"] = true;
    filter["list"][0]["main"]["temp"] = true;
    filter["list"][0]["main"]["temp_min"] = true;
    filter["list"][0]["main"]["temp_max"] = true;
    filter["list"][0]["main"]["feels_like"] = true;
    filter["list"][0]["main"]["pressure"] = true;
    filter["list"][0]["main"]["humidity"] = true;
    filter["list"][0]["weather"][0]["id"] = true;
    filter["list"][0]["weather"][0]["icon"] = true;
    filter["list"][0]["weather"][0]["description"] = true;
    filter["list"][0]["wind"]["speed"] = true;
    filter["list"][0]["wind"]["deg"] = true;
    filter["list"][0]["pop"] = true;

    JsonDocument doc(&alloc);
    const DeserializationError derr =
        deserializeJson(doc, client, DeserializationOption::Filter(filter));
    client.stop();

    if (derr) {
        fc_log_parse_fail(derr.c_str(), t0, httpCode);
        return WeatherForecastFetchResult::Failed; // keep last-known-good
    }

    JsonArray list = doc["list"].as<JsonArray>();
    if (list.isNull() || list.size() == 0) {
        fc_log_fail("empty_list", t0, httpCode);
        return WeatherForecastFetchResult::Failed;
    }
    const int32_t tz = doc["city"]["timezone"] | 0;


    // ── Build into the scratch builder; publish only on full success. ──
    memset(&s_builder, 0, sizeof(s_builder));
    s_builder.forecast_tz_sec = tz;

    // W-R2: three future local days → daily[1..3]; today excluded from daily slots.
    // W-R2: три будущих локальных дня → daily[1..3]; сегодня не публикуется в daily.
    const struct tm& devtm = network.timeinfo;
    const bool ntp_valid = devtm.tm_year > 100;
    int32_t today_key;
    const char* today_source;
    if (ntp_valid) {
        today_key    = owm_local_day_key(static_cast<int64_t>(time(nullptr)), tz);
        today_source = "ntp";
    } else {
        const uint32_t first_dt = list[0]["dt"] | 0u;
        if (first_dt == 0u) {
            fc_log_fail("no_first_dt", t0, httpCode);
            return WeatherForecastFetchResult::Failed;
        }
        today_key    = owm_local_day_key(static_cast<int64_t>(first_dt), tz);
        today_source = "first_point_fallback";
    }
    const int32_t target_max_key = today_key + 3;


    uint8_t day_dom_sev[3] = {0};   // severity scratch for daily[1..3]
    uint16_t day_points[3] = {0};   // per-target-day point counts (diag)
    uint32_t skipped_today_points = 0;

    uint8_t  hourly_count = 0;
    uint16_t point_count  = 0;

    for (JsonVariant item : list) {
        if (point_count >= kMaxForecastPoints) break;

        const uint32_t dt       = item["dt"] | 0u;
        const float    temp     = item["main"]["temp"] | 0.0f;
        const float    tmin     = item["main"]["temp_min"] | temp;
        const float    tmax     = item["main"]["temp_max"] | temp;
        const float    feels    = item["main"]["feels_like"] | temp;
        const uint16_t pressure = item["main"]["pressure"] | 0;
        const uint8_t  humidity = item["main"]["humidity"] | 0;
        const float    wspeed   = item["wind"]["speed"] | 0.0f;
        const uint16_t wdeg     = item["wind"]["deg"] | 0;
        const uint8_t  pop      = pop_to_pct(item["pop"] | 0.0f);
        const uint16_t code     = item["weather"][0]["id"] | 0;
        const char*    icon     = item["weather"][0]["icon"] | "";
        const char*    desc     = item["weather"][0]["description"] | "";

        // current proxy = closest (first) forecast point
        if (point_count == 0) {
            s_builder.current.valid            = true;
            s_builder.current.temp_c           = temp;
            s_builder.current.feels_like_c     = feels;
            s_builder.current.humidity         = humidity;
            s_builder.current.pressure_hpa     = pressure;
            s_builder.current.wind_speed       = wspeed;
            s_builder.current.wind_deg         = wdeg;
            s_builder.current.rain_probability = pop;
            s_builder.current.owm_code         = code;
            copy_icon(s_builder.current.owm_icon, icon);
            strlcpy(s_builder.current.condition, desc, sizeof(s_builder.current.condition));
            s_builder.current.updated_at       = dt;
        }

        // hourly strip — first 8 points only (unchanged by W-R2 horizon)
        if (hourly_count < WEATHER_HOURLY_SLOTS) {
            WeatherHourly& h     = s_builder.hourly[hourly_count++];
            h.valid              = true;
            h.ts                 = dt;
            h.temp_c             = temp;
            h.rain_probability   = pop;
            h.owm_code           = code;
            copy_icon(h.owm_icon, icon);
        }

        // W-R2: aggregate directly into daily[1..3] for target future local days.
        const int32_t local_day = owm_local_day_key(static_cast<int64_t>(dt), tz);
        if (local_day <= today_key) {
            ++skipped_today_points;
        } else if (local_day <= target_max_key) {
            const uint8_t slot   = static_cast<uint8_t>(local_day - today_key); // 1..3
            const uint8_t sev_ix = static_cast<uint8_t>(slot - 1u);
            WeatherDaily& nd     = s_builder.daily[slot];
            if (!nd.valid) {
                nd.valid                = true;
                nd.day_ts               = dt;
                nd.temp_min_c           = (tmin < temp) ? tmin : temp;
                nd.temp_max_c           = (tmax > temp) ? tmax : temp;
                nd.rain_probability_max = pop;
                nd.dominant_owm_code    = code;
                copy_icon(nd.owm_icon, icon);
                day_dom_sev[sev_ix]     = owm_severity(code);
                day_points[sev_ix]      = 1;
            } else {
                if (tmin < nd.temp_min_c) nd.temp_min_c = tmin;
                if (temp < nd.temp_min_c) nd.temp_min_c = temp;
                if (tmax > nd.temp_max_c) nd.temp_max_c = tmax;
                if (temp > nd.temp_max_c) nd.temp_max_c = temp;
                if (pop  > nd.rain_probability_max) nd.rain_probability_max = pop;
                const uint8_t sev = owm_severity(code);
                if (sev > day_dom_sev[sev_ix]) {
                    day_dom_sev[sev_ix]    = sev;
                    nd.dominant_owm_code   = code;
                    copy_icon(nd.owm_icon, icon);
                }
                ++day_points[sev_ix];
            }
        }
        // Points after target_max_key are ignored for daily publication.

        ++point_count;
    }

    if (point_count == 0) {
        fc_log_fail("no_points", t0, httpCode);
        return WeatherForecastFetchResult::Failed;
    }

    uint8_t future_days_published = 0;
    for (uint8_t s = 1; s <= 3; ++s) {
        if (s_builder.daily[s].valid) {
            ++future_days_published;
        }
    }
    // daily[0] and daily[4] remain invalid (memset + never written).


    // ── A4.0: location from forecast city metadata (always in filter now) ──────────
    // A4.0: локация из метаданных города прогноза (теперь всегда в фильтре).
    {
        const char* fc_city    = doc["city"]["name"]    | "";
        const char* fc_country = doc["city"]["country"] | "";

        // Location priority: true /weather response > forecast city > LKG (no overwrite).
        // Приоритет локации: /weather > city из прогноза > LKG (пустым не перезаписываем).
        if (true_current && true_current->valid && true_current->location.valid) {
            s_builder.location = true_current->location;
        } else if (fc_city[0] != '\0') {
            s_builder.location.valid = true;
            strlcpy(s_builder.location.city,    fc_city,    sizeof(s_builder.location.city));
            strlcpy(s_builder.location.country, fc_country, sizeof(s_builder.location.country));
        }
        // else: location stays invalid/zero — caller may preserve LKG if needed.
    }

    // ── A4.0: current_source default = ForecastFallback (current proxy from list[0]) ──
    // A4.0: по умолчанию current_source = ForecastFallback (прокси из list[0]).
    s_builder.current_source = WeatherCurrentSource::ForecastFallback;

    // ── A4.0: overlay true-current conditions if available (Case A) ────────────────
    // A4.0: наложить true-current если есть (Case A).
    if (true_current && true_current->valid) {
        s_builder.current.temp_c        = true_current->temp_c;
        s_builder.current.feels_like_c  = true_current->feels_like_c;
        s_builder.current.humidity      = true_current->humidity;
        s_builder.current.pressure_hpa  = true_current->pressure_hpa;
        s_builder.current.wind_speed    = true_current->wind_speed;
        s_builder.current.wind_deg      = true_current->wind_deg;
        copy_icon(s_builder.current.owm_icon, true_current->owm_icon);
        s_builder.current.owm_code      = true_current->owm_code;
        strlcpy(s_builder.current.condition, true_current->condition,
                sizeof(s_builder.current.condition));
        s_builder.current.updated_at    = true_current->updated_at;
        // rain_probability: no pop in /weather — preserve forecast list[0] proxy value.
        // rain_probability: pop нет в /weather — сохраняем прокси из list[0] прогноза.
        s_builder.current_source = WeatherCurrentSource::CurrentEndpoint;
    }


    s_builder.forecast_valid      = true;
    s_builder.stale               = false;
    s_builder.forecast_updated_at = millis();
    // version is stamped inside weatherPublishState().
    weatherPublishState(s_builder);

    fc_log_heap("after");
    fc_log_success(t0, httpCode, point_count, future_days_published);
    // W1.5: detailed parsed/published dump (gated; one-shot per successful publish, not per UI update).
    // W1.5: подробный дамп распарсенного/опубликованного (под флагом; раз на публикацию, не на кадр UI).
    fc_dump_published(point_count, hourly_count, future_days_published, tz);
    return WeatherForecastFetchResult::Published;
}

// A2b: UI-safe manual refresh hook — flag only; doSync runs getWeather + forecast.
// A2b: ручной refresh из UI — только флаг; doSync выполнит getWeather + прогноз.
void weatherRequestManualRefresh() {
    Serial.println("[WEATHER] manual refresh requested");
    Serial.println("[WEATHER_FC] manual refresh requested");
    network.forceWeatherRefreshFromUi();
}

#else // HIDE_WEATHER

WeatherForecastFetchResult weatherFetchForecast(const char* units, const char* lang,
                                                WeatherEdgeSession& session,
                                                const WeatherTrueCurrent* true_current) {
    (void)units; (void)lang; (void)session; (void)true_current;
    return WeatherForecastFetchResult::NotConfigured;
}

bool weatherParseCurrentBody(const char* body, WeatherCurrentParsed* out) {
    (void)body; (void)out;
    return false;
}

void weatherPublishCurrentOverLkg(const WeatherTrueCurrent& tc,
                                   WeatherLastError error, bool make_stale) {
    (void)tc; (void)error; (void)make_stale;
}

void weatherRequestManualRefresh() {
    (void)0;
}

void weatherForecastMarkPending() {}
bool weatherForecastIsPending() { return false; }
bool weatherForecastPollPending() { return false; }
bool weatherForecastTakePendingOnlyRun() { return false; }
void weatherForecastDiscardPendingOnlyArm() {}

#endif // !HIDE_WEATHER
