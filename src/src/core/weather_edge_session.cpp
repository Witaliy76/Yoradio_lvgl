#include "weather_edge_session.h"
#include <string.h>

/*
 * WeatherEdgeSession — implementation.
 * No dynamic allocation, no mutex (single-threaded doSync ownership).
 * Нет динамических аллокаций; мьютекс не нужен (однопоточный doSync).
 */

void WeatherEdgeSession::resetCycle() {
    preferred              = IPAddress(0, 0, 0, 0);
    preferredValid         = false;
    preferredResolverIndex = 0;
    for (uint8_t i = 0; i < kWeatherEdgeMaxAttempts; ++i) {
        attempted[i] = IPAddress(0, 0, 0, 0);
    }
    attemptedCount = 0;
}

void WeatherEdgeSession::beginRequest() {
    // Reset only request-level state; cycle-level preferred is preserved.
    // Сбрасываем только request-level; cycle-level preferred сохраняется.
    for (uint8_t i = 0; i < kWeatherEdgeMaxAttempts; ++i) {
        attempted[i] = IPAddress(0, 0, 0, 0);
    }
    attemptedCount = 0;
}

bool WeatherEdgeSession::wasAttempted(const IPAddress& ip) const {
    for (uint8_t i = 0; i < attemptedCount; ++i) {
        if (attempted[i] == ip) return true;
    }
    return false;
}

bool WeatherEdgeSession::markAttempted(const IPAddress& ip) {
    if (attemptedCount >= kWeatherEdgeMaxAttempts) return false;
    attempted[attemptedCount++] = ip;
    return true;
}

void WeatherEdgeSession::setPreferred(const IPAddress& ip, uint8_t resolverIndex) {
    preferred              = ip;
    preferredValid         = true;
    preferredResolverIndex = resolverIndex;
}
