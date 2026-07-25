#ifndef weather_edge_session_h
#define weather_edge_session_h

/*
 * Per-doSync-cycle Weather transport edge state (preferred DNS IP and attempt tracking).
 * Состояние транспортного края погоды на один цикл doSync.
 *
 * State is divided into two levels:
 *
 *   Cycle-level  (preserved between current and forecast within one full doSync cycle):
 *     preferred     — fallback IP that succeeded for the current request
 *     preferredValid
 *     preferredResolverIndex — index into netDnsDefaultFallbackServers() list
 *
 *   Request-level (reset for each individual HTTP request via beginRequest()):
 *     attempted[]   — IPs that have had a TCP connect attempt this request
 *     attemptedCount
 *
 * Lifecycle in a full cycle (doSync forceWeather path):
 *   1. resetCycle()       — full reset; once at the start of the doSync cycle
 *   2. getWeather(...)    — uses request-level tracking; sets preferred on fallback success
 *   3. [session.beginRequest() called internally by weatherFetchForecast]
 *   4. weatherFetchForecast(..., session) — inherits preferred; fresh request-level tracking
 *
 * Lifecycle in a pending-only cycle (doSync forecast-only path):
 *   1. resetCycle()       — full reset; no preferred from any prior cycle
 *   2. weatherFetchForecast(..., session) — fresh; uses system→Cloudflare→Quad9 order
 *
 * Threading: single-threaded by contract (doSync, Core 0 only).
 * Must NOT be stored as a global or static persistent object. No NVS writes.
 *
 * Author: Witaliy76 - https://github.com/Witaliy76
 */

#include <IPAddress.h>
#include <cstdint>
#include <cstddef>

// Maximum unique TCP-level attempts per individual HTTP request.
// 1 system + 1 Cloudflare candidate + 1 Quad9 candidate = 3.
constexpr uint8_t kWeatherEdgeMaxAttempts = 3;

struct WeatherEdgeSession {
    // ── Cycle-level: preserved across current + forecast requests ─────────────
    // Set by getWeather() when a fallback resolver edge succeeds.
    // Forecast uses this to try the known-good IP first and skip exhausted resolvers.
    // Устанавливается getWeather() при успехе fallback; forecast использует для reuse.
    IPAddress preferred;
    bool      preferredValid;
    uint8_t   preferredResolverIndex; // index into netDnsDefaultFallbackServers() list

    // ── Request-level: reset by beginRequest() before each HTTP request ────────
    // IPs already used for a TCP connect attempt in this request (deduplication).
    // IP, уже использованные для TCP-подключения в данном запросе.
    IPAddress attempted[kWeatherEdgeMaxAttempts];
    uint8_t   attemptedCount;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // Full reset: clears cycle-level preferred and request-level tracking.
    // Call once at the start of a new doSync cycle (before getWeather or forecast).
    // Полный сброс: вызывать один раз в начале нового цикла doSync.
    void resetCycle();

    // Request-level reset: clears attempted[] and attemptedCount only.
    // Preserves cycle-level preferred for forecast reuse.
    // Called internally by weatherFetchForecast() before its transport loop.
    // Сброс request-level; preserved preferred для повторного использования forecast.
    void beginRequest();

    // ── Request-level operations ───────────────────────────────────────────────
    bool wasAttempted(const IPAddress& ip) const;
    // Returns false if the attempt list is already full (all 3 slots consumed).
    bool markAttempted(const IPAddress& ip);
    bool isFull() const { return attemptedCount >= kWeatherEdgeMaxAttempts; }

    // ── Cycle-level setters/accessors ──────────────────────────────────────────
    // Call when a fallback resolver edge successfully delivers response bytes.
    // resolverIndex = index into netDnsDefaultFallbackServers() list.
    // Вызывать когда fallback-резолвер успешно доставил байты ответа.
    void      setPreferred(const IPAddress& ip, uint8_t resolverIndex);
    IPAddress getPreferred()              const { return preferred; }
    bool      hasPreferred()              const { return preferredValid; }
    uint8_t   getPreferredResolverIndex() const { return preferredResolverIndex; }
};

#endif // weather_edge_session_h
