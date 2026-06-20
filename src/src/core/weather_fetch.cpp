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
#include <cerrno>
#include <cstring>  // strerror — REQ_DIAG connect errno only / только под REQ_DIAG

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

// W-R1B: request/response/aggregation diagnostics — off by default.
// W-R1B: диагностика запросов/ответов/агрегации — выключена по умолчанию.
#ifndef YORADIO_WEATHER_REQ_DIAG
#define YORADIO_WEATHER_REQ_DIAG 0
#endif

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
constexpr uint8_t  kForecastCnt          = 24;          // 24 × 3h = 72h (≈3 days)
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
        return false;
    }

    if (int_free < kPendingTaskAdmissionMinFreeBytes ||
        int_block < static_cast<size_t>(kDoSyncTaskStackBytes)) {
#if YORADIO_WEATHER_REQ_DIAG
        Serial.printf("[WEATHER_FC] pending wait reason=admission_low int_free=%u required=%u int_block=%u\n",
                      (unsigned)int_free, (unsigned)kPendingTaskAdmissionMinFreeBytes, (unsigned)int_block);
#endif
        return false;
    }

    s_fc_pending_only_run = true;
#if YORADIO_WEATHER_REQ_DIAG
    Serial.printf("[WEATHER_FC] pending armed int_free=%u required=%u\n",
                  (unsigned)int_free, (unsigned)kPendingTaskAdmissionMinFreeBytes);
#endif
    return true;
}

WeatherForecastFetchResult weatherFetchForecast(const char* units, const char* lang,
                                                WeatherEdgeSession& session) {
    // Gate mirrors the current-weather enablement; keep behavior independent of UI.
    // Гейт повторяет включение текущей погоды; не зависит от UI.
    if (!config.store.showweather || strlen(config.store.weatherkey) == 0) {
        return WeatherForecastFetchResult::NotConfigured;
    }
    if (network.status != CONNECTED) {
        return WeatherForecastFetchResult::NotConnected;
    }
    if (!units) units = "metric";
    if (!lang)  lang  = "en";

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
        weatherForecastMarkPending();
#if YORADIO_WEATHER_REQ_DIAG
        Serial.printf("[WEATHER_FC] defer reason=internal_low pending=1 int_free=%u int_block=%u\n",
                      (unsigned)int_free,
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
#endif
        return WeatherForecastFetchResult::DeferredInternalLow;
    }

    // Real attempt starting — clear pending before network/parse work.
    // Реальная попытка — сбрасываем pending до сети/парсинга.
    if (s_fc_pending) {
#if YORADIO_WEATHER_REQ_DIAG
        Serial.println("[WEATHER_FC] pending cleared reason=attempt_started");
#endif
        s_fc_pending = false;
    }

#if YORADIO_WEATHER_REQ_DIAG
    // Log config and request parameters before first network call. / Логируем параметры до сети.
    Serial.printf("[WEATHER_CFG] lat=\"%s\" lon=\"%s\" units=%s lang=%s key_present=%d key_len=%u\n",
                  config.store.weatherlat, config.store.weatherlon,
                  units, lang,
                  strlen(config.store.weatherkey) > 0 ? 1 : 0,
                  (unsigned)strlen(config.store.weatherkey));
    Serial.printf("[WEATHER_REQ] path=forecast lat=%s lon=%s units=%s lang=%s cnt=%u preferred=%d\n",
                  config.store.weatherlat, config.store.weatherlon,
                  units, lang, (unsigned)kForecastCnt, (int)session.hasPreferred());
#endif

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
             config.store.weatherlat, config.store.weatherlon, units, lang,
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
#if YORADIO_WEATHER_REQ_DIAG
                Serial.printf("[WEATHER_NET] path=forecast fail attempt=0 source=system stage=dns\n");
#endif
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

#if YORADIO_WEATHER_REQ_DIAG
            Serial.printf("[WEATHER_NET] path=forecast dns resolver=%s status=%s candidates=%u\n",
                          sourceName,
                          dnsStatus == NetDnsQueryStatus::Success ? "ok" : "fail",
                          (unsigned)candidateCount);
#endif

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
#if YORADIO_WEATHER_REQ_DIAG
                Serial.printf("[WEATHER_NET] path=forecast skip resolver=%s reason=duplicate ip=%s\n",
                              sourceName, candidates[ci].toString().c_str());
#endif
            }

            if (edgeIP == IPAddress(0, 0, 0, 0)) {
                continue; // all candidates already attempted
            }
        }

        // ── Deduplication (covers preferred reuse too, in case of repeated cycle edge) ──
        if (session.wasAttempted(edgeIP)) {
#if YORADIO_WEATHER_REQ_DIAG
            Serial.printf("[WEATHER_NET] path=forecast skip source=%s reason=duplicate ip=%s\n",
                          sourceName, edgeIP.toString().c_str());
#endif
            continue;
        }
        session.markAttempted(edgeIP);

#if YORADIO_WEATHER_REQ_DIAG
        Serial.printf("[WEATHER_NET] path=forecast attempt=%u source=%s ip=%s reuse=%d\n",
                      (unsigned)attemptIdx, sourceName, edgeIP.toString().c_str(),
                      (int)isReuse);
#endif

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
#if YORADIO_WEATHER_REQ_DIAG
            const int connect_errno = errno;
            Serial.printf("[WEATHER_NET] path=forecast fail attempt=%u source=%s "
                          "stage=connect ip=%s errno=%d\n",
                          (unsigned)attemptIdx, sourceName, edgeIP.toString().c_str(),
                          connect_errno);
#endif
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
#if YORADIO_WEATHER_REQ_DIAG
                Serial.printf("[WEATHER_NET] path=forecast fail attempt=%u source=%s "
                              "stage=read_wait_before_status ip=%s\n",
                              (unsigned)attemptIdx, sourceName, edgeIP.toString().c_str());
#endif
                client.stop();
                continue; // ReadWaitBeforeStatus → try next edge
            }
        }

        // Bytes arrived — this edge is usable for the HTTP response.
        // After this point: do NOT rotate on HTTP-status or parse failures.
        // После первого байта — НЕ ротируем по HTTP-статусу или ошибкам парсинга.
        fc_connected = true;
#if YORADIO_WEATHER_REQ_DIAG
        Serial.printf("[WEATHER_NET] path=forecast success attempt=%u source=%s ip=%s\n",
                      (unsigned)attemptIdx, sourceName, edgeIP.toString().c_str());
#endif
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
    filter["city"]["timezone"] = true; // for local-day grouping
#if YORADIO_WEATHER_REQ_DIAG
    // W-R1B: extra city metadata retained only when diagnostics are on — no WeatherState change.
    // W-R1B: метаданные города только под диагностикой; WeatherState не меняется.
    filter["city"]["name"] = true;
    filter["city"]["country"] = true;
    filter["city"]["coord"]["lat"] = true;
    filter["city"]["coord"]["lon"] = true;
#endif
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

#if YORADIO_WEATHER_REQ_DIAG
    // Response metadata: city name, coordinates from server response, timezone offset.
    // Метаданные ответа: имя города, координаты из ответа сервера, смещение timezone.
    {
        const char* d_city    = doc["city"]["name"]    | "";
        const char* d_country = doc["city"]["country"] | "";
        const float d_lat     = doc["city"]["coord"]["lat"] | 0.0f;
        const float d_lon     = doc["city"]["coord"]["lon"] | 0.0f;
        const uint32_t d_first = list.size() > 0 ? (list[0]["dt"]                | 0u) : 0u;
        const uint32_t d_last  = list.size() > 0 ? (list[list.size()-1]["dt"]    | 0u) : 0u;
        Serial.printf("[WEATHER_RES] path=forecast ok=1 city=\"%s\" country=\"%s\""
                      " coord_lat=%.4f coord_lon=%.4f tz=%ld list=%u"
                      " first_dt=%lu last_dt=%lu\n",
                      d_city, d_country,
                      (double)d_lat, (double)d_lon,
                      (long)tz, (unsigned)list.size(),
                      (unsigned long)d_first, (unsigned long)d_last);
    }
    // Device NTP state vs OWM timezone. / Состояние NTP устройства vs timezone OWM.
    {
        const struct tm& devtm   = network.timeinfo;
        const bool       ntpok   = devtm.tm_year > 100;
        Serial.printf("[WEATHER_TIME] ntp_valid=%d device_year=%d device_mon=%d device_day=%d"
                      " owm_tz=%ld device_tz_h=%d device_tz_m=%d\n",
                      (int)ntpok,
                      devtm.tm_year + 1900, devtm.tm_mon + 1, devtm.tm_mday,
                      (long)tz,
                      (int)config.store.tzHour, (int)config.store.tzMin);
    }
#endif

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
        return WeatherForecastFetchResult::Failed;
    }

#if YORADIO_WEATHER_REQ_DIAG
    // Aggregation summary: how many points parsed, hourly/daily buckets produced.
    // Сводка агрегации: сколько точек разобрано, сколько hourly/daily бакетов получено.
    Serial.printf("[WEATHER_AGG] parsed=%u hourly=%u daily=%u current_valid=%d\n",
                  (unsigned)point_count, (unsigned)hourly_count, (unsigned)day_count,
                  (int)s_builder.current.valid);
    for (uint8_t _di = 0; _di < day_count; ++_di) {
        const WeatherDaily& _nd = s_builder.daily[_di];
        Serial.printf("[WEATHER_AGG] daily[%u] ts=%lu local_key=%ld valid=%d\n",
                      (unsigned)_di, (unsigned long)_nd.day_ts,
                      (long)day_keys[_di], (int)_nd.valid);
    }
#endif

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
                                                WeatherEdgeSession& session) {
    (void)units; (void)lang; (void)session;
    return WeatherForecastFetchResult::NotConfigured;
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
