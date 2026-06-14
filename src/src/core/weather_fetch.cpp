#include "weather_fetch.h"
#include "weather_state.h"
#include "config.h"     // config.store.*, options.h (HIDE_WEATHER), Config
#include "network.h"    // network, networkResolveHostForConnect()

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>     // v7.4.2
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

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

// Compile-time verbose diagnostics, mirroring YORADIO_WEATHER_DIAG for current weather.
// Компиляционная подробная диагностика — по аналогии с YORADIO_WEATHER_DIAG.
#ifndef YORADIO_WEATHER_FC_DIAG
#define YORADIO_WEATHER_FC_DIAG 0
#endif

namespace {

// ── Tunables / Параметры ──────────────────────────────────────────────────────
constexpr uint8_t  kForecastCnt          = 24;          // 24 × 3h = 72h (≈3 days)
constexpr uint16_t kMaxForecastPoints    = 48;          // hard cap on parsed points
constexpr size_t   kMinInternalFreeBytes = 40u * 1024u; // keep internal headroom (WiFiClient/TLS-AI/etc.)
constexpr size_t   kMinPsramBlockBytes   = 24u * 1024u; // require a usable PSRAM arena
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

uint8_t pop_to_pct(float pop) {
    if (pop < 0.0f) pop = 0.0f;
    if (pop > 1.0f) pop = 1.0f;
    long v = lroundf(pop * 100.0f);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    return static_cast<uint8_t>(v);
}

// File-scope scratch builder — kept off the 4 KB doSync stack on purpose.
// Single-threaded by contract (weather-sync context only).
// Скрэтч-билдер в .bss — намеренно не на 4 КБ стеке doSync; однопоточный по контракту.
WeatherState s_builder;

// ── Diagnostics / Диагностика ──────────────────────────────────────────────────
void fc_log_skip(const char* reason, size_t int_free, size_t ps_free, size_t ps_block) {
    Serial.printf("[WEATHER_FC] skip reason=%s int_free=%u psram_free=%u psram_block=%u\n",
                  reason, (unsigned)int_free, (unsigned)ps_free, (unsigned)ps_block);
}

void fc_log_fail(const char* stage, uint32_t t0, int httpCode) {
    Serial.printf("[WEATHER_FC] fail stage=%s http=%d elapsed_ms=%lu\n",
                  stage, httpCode, (unsigned long)(millis() - t0));
}

void fc_log_parse_fail(const char* err, uint32_t t0, int httpCode) {
    Serial.printf("[WEATHER_FC] fail stage=parse err=%s http=%d elapsed_ms=%lu\n",
                  err, httpCode, (unsigned long)(millis() - t0));
}

void fc_log_success(uint32_t t0, int httpCode, uint16_t points, uint8_t days) {
    Serial.printf("[WEATHER_FC] success http=%d points=%u days=%u elapsed_ms=%lu next_s=1800\n",
                  httpCode, (unsigned)points, (unsigned)days, (unsigned long)(millis() - t0));
}

#if YORADIO_WEATHER_FC_DIAG
void fc_log_heap(const char* tag) {
    Serial.printf("[WEATHER_FC] heap[%s] int_free=%u int_block=%u psram_free=%u psram_block=%u\n",
                  tag,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

// W1.5 diag readback buffer — file scope so the dump never touches the 4 KB doSync stack.
// Only compiled when the forecast diag flag is on (no production RAM cost otherwise).
// W1.5: буфер чтения для дампа — file scope, чтобы не задевать 4 КБ стек doSync; компилируется только под диаг-флагом.
WeatherState s_fc_diag_snap;

// Dump the just-published WeatherState (read back via the public seqlock getter, so it also
// exercises the reader path). Summary counts come from local fetch state (parsed vs published).
// Дамп опубликованного WeatherState через публичный seqlock-getter (заодно проверка читателя);
// счётчики summary — из локального состояния fetch (распарсено vs опубликовано).
void fc_dump_published(uint16_t parsed_points, uint8_t hourly_published,
                       uint8_t daily_published, int32_t tz) {
    if (!weatherGetStateSnapshot(&s_fc_diag_snap)) {
        Serial.println("[WEATHER_FC] dump skipped: snapshot busy");
        return;
    }
    const WeatherState& s = s_fc_diag_snap;
    Serial.printf("[WEATHER_FC] dump summary parsed_points=%u hourly_published=%u "
                  "daily_published=%u updated_at=%lu version=%lu tz=%ld\n",
                  (unsigned)parsed_points, (unsigned)hourly_published, (unsigned)daily_published,
                  (unsigned long)s.forecast_updated_at, (unsigned long)s.version, (long)tz);
    for (uint8_t i = 0; i < WEATHER_HOURLY_SLOTS; ++i) {
        const WeatherHourly& h = s.hourly[i];
        Serial.printf("[WEATHER_FC] hourly[%u] valid=%d ts=%lu temp=%.1f pop=%u icon=%s code=%u\n",
                      (unsigned)i, (int)h.valid, (unsigned long)h.ts, (double)h.temp_c,
                      (unsigned)h.rain_probability, h.owm_icon, (unsigned)h.owm_code);
    }
    for (uint8_t i = 0; i < WEATHER_DAILY_SLOTS; ++i) {
        const WeatherDaily& d = s.daily[i];
        Serial.printf("[WEATHER_FC] daily[%u] valid=%d day_ts=%lu tmin=%.1f tmax=%.1f "
                      "popmax=%u icon=%s dom=%u\n",
                      (unsigned)i, (int)d.valid, (unsigned long)d.day_ts, (double)d.temp_min_c,
                      (double)d.temp_max_c, (unsigned)d.rain_probability_max, d.owm_icon,
                      (unsigned)d.dominant_owm_code);
    }
}
#else
inline void fc_log_heap(const char*) {}
inline void fc_dump_published(uint16_t, uint8_t, uint8_t, int32_t) {}
#endif

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

bool weatherFetchForecast(const char* units, const char* lang) {
    // Gate mirrors the current-weather enablement; keep behavior independent of UI.
    // Гейт повторяет включение текущей погоды; не зависит от UI.
    if (!config.store.showweather || strlen(config.store.weatherkey) == 0) {
        return false;
    }
    if (network.status != CONNECTED) {
        return false;
    }
    if (!units) units = "metric";
    if (!lang)  lang  = "en";

    const uint32_t t0 = millis();
    fc_log_heap("before");

    // ── Guards: internal heap + PSRAM must have headroom before parse-heavy work. ──
    // Гарды: проверяем internal heap и PSRAM до тяжёлого парсинга.
    const size_t int_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t ps_free   = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t ps_block  = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (ps_free == 0 || ps_block < kMinPsramBlockBytes) {
        fc_log_skip("psram_low", int_free, ps_free, ps_block);
        return false; // keep last-known-good
    }
    if (int_free < kMinInternalFreeBytes) {
        fc_log_skip("internal_low", int_free, ps_free, ps_block);
        return false; // keep last-known-good
    }

    // ── Resolve + connect (reuse existing DNS approach; HTTP/80, no TLS). ──
    const char* host = "api.openweathermap.org";
    IPAddress ip;
    if (!networkResolveHostForConnect(host, ip)) {
        fc_log_fail("dns", t0, -1);
        return false;
    }

    WiFiClient client;
    if (!client.connect(ip, 80, kConnectTimeoutMs)) {
        client.stop();
        fc_log_fail("connect", t0, -1);
        return false;
    }

    // HTTP/1.0 → server returns a non-chunked body, safe to stream straight into the parser.
    // HTTP/1.0 → тело без chunked, поток можно отдавать прямо парсеру.
    {
        char req[320];
        snprintf(req, sizeof(req),
                 "GET /data/2.5/forecast?lat=%s&lon=%s&units=%s&lang=%s&cnt=%u&appid=%s HTTP/1.0\r\n"
                 "Host: %s\r\nConnection: close\r\n\r\n",
                 config.store.weatherlat, config.store.weatherlon, units, lang,
                 (unsigned)kForecastCnt, config.store.weatherkey, host);
        client.print(req);
    }

    // Wait for the response to start.
    {
        const uint32_t to = millis();
        while (client.available() == 0) {
            if (!client.connected()) { client.stop(); fc_log_fail("disconnected", t0, -1); return false; }
            if (millis() - to > kReadWaitTimeoutMs) { client.stop(); fc_log_fail("read_wait", t0, -1); return false; }
            delay(5);
        }
    }

    // Status line: "HTTP/1.0 200 OK".
    int httpCode = -1;
    {
        char line[96];
        read_line(client, line, sizeof(line), kReadWaitTimeoutMs);
        const char* sp = strchr(line, ' ');
        if (sp) httpCode = atoi(sp + 1);
    }
    if (httpCode != 200) {
        client.stop();
        fc_log_fail("http", t0, httpCode);
        return false;
    }

    // Skip headers until the blank separator line.
    {
        char line[160];
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
            return false;
        }
    }

    // ── Parse: PSRAM-backed JsonDocument + Filter + stream deserialization. ──
    PsramJsonAllocator alloc;

    JsonDocument filter(&alloc);
    filter["city"]["timezone"] = true; // for local-day grouping
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
        return false; // keep last-known-good
    }

    JsonArray list = doc["list"].as<JsonArray>();
    if (list.isNull() || list.size() == 0) {
        fc_log_fail("empty_list", t0, httpCode);
        return false;
    }
    const int32_t tz = doc["city"]["timezone"] | 0;

    // ── Build into the scratch builder; publish only on full success. ──
    memset(&s_builder, 0, sizeof(s_builder));

    int32_t day_keys[WEATHER_DAILY_SLOTS];
    uint8_t day_dom_sev[WEATHER_DAILY_SLOTS] = {0};
    for (uint8_t i = 0; i < WEATHER_DAILY_SLOTS; ++i) day_keys[i] = INT32_MIN;
    uint8_t day_count = 0;

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

        // hourly strip
        if (hourly_count < WEATHER_HOURLY_SLOTS) {
            WeatherHourly& h     = s_builder.hourly[hourly_count++];
            h.valid              = true;
            h.ts                 = dt;
            h.temp_c             = temp;
            h.rain_probability   = pop;
            h.owm_code           = code;
            copy_icon(h.owm_icon, icon);
        }

        // daily aggregation keyed by local day (dt + city timezone offset)
        const int32_t local_day = static_cast<int32_t>((static_cast<int64_t>(dt) + tz) / 86400);
        int slot = -1;
        for (uint8_t d = 0; d < day_count; ++d) {
            if (day_keys[d] == local_day) { slot = d; break; }
        }
        if (slot < 0 && day_count < WEATHER_DAILY_SLOTS) {
            slot = day_count++;
            day_keys[slot]          = local_day;
            WeatherDaily& nd        = s_builder.daily[slot];
            nd.valid                = true;
            nd.day_ts               = dt;
            nd.temp_min_c           = (tmin < temp) ? tmin : temp;
            nd.temp_max_c           = (tmax > temp) ? tmax : temp;
            nd.rain_probability_max = pop;
            nd.dominant_owm_code    = code;
            copy_icon(nd.owm_icon, icon);
            day_dom_sev[slot]       = owm_severity(code);
        } else if (slot >= 0) {
            WeatherDaily& nd = s_builder.daily[slot];
            if (tmin < nd.temp_min_c) nd.temp_min_c = tmin;
            if (temp < nd.temp_min_c) nd.temp_min_c = temp;
            if (tmax > nd.temp_max_c) nd.temp_max_c = tmax;
            if (temp > nd.temp_max_c) nd.temp_max_c = temp;
            if (pop  > nd.rain_probability_max) nd.rain_probability_max = pop;
            const uint8_t sev = owm_severity(code);
            if (sev > day_dom_sev[slot]) {
                day_dom_sev[slot]    = sev;
                nd.dominant_owm_code = code;
                copy_icon(nd.owm_icon, icon);
            }
        }
        // days beyond WEATHER_DAILY_SLOTS are ignored (cnt=24 ≈ 3 days → safe)

        ++point_count;
    }

    if (point_count == 0) {
        fc_log_fail("no_points", t0, httpCode);
        return false;
    }

    s_builder.forecast_valid      = true;
    s_builder.stale               = false;
    s_builder.forecast_updated_at = millis();
    // version is stamped inside weatherPublishState().
    weatherPublishState(s_builder);

    fc_log_heap("after");
    fc_log_success(t0, httpCode, point_count, day_count);
    // W1.5: detailed parsed/published dump (gated; one-shot per successful publish, not per UI update).
    // W1.5: подробный дамп распарсенного/опубликованного (под флагом; раз на публикацию, не на кадр UI).
    fc_dump_published(point_count, hourly_count, day_count, tz);
    return true;
}

#else // HIDE_WEATHER

bool weatherFetchForecast(const char* units, const char* lang) {
    (void)units;
    (void)lang;
    return false;
}

#endif // !HIDE_WEATHER
