#include "weather_state.h"

#include <Arduino.h>
#include <atomic>

#ifndef YORADIO_WEATHER_REQ_DIAG
#define YORADIO_WEATHER_REQ_DIAG 0
#endif

/*
 * Weather W1 — double-buffer + seqlock publication for WeatherState.
 * Weather W1 — double-buffer + seqlock публикация WeatherState.
 *
 * W-R3 adds status-only publish helpers that preserve the active payload while
 * updating fetch_in_progress / last_error / stale / last_attempt_at_ms.
 * W-R3: status-only публикация сохраняет payload и обновляет только метаданные.
 */

namespace {

WeatherState              s_buf[2];  // zero-init: version=0, forecast_valid=false
std::atomic<uint8_t>      s_active{0};
std::atomic<uint32_t>     s_seq{0};  // even = stable, odd = writer busy

// Flip inactive slot through the seqlock. bump_version only for full payload publishes.
// Переключение неактивного слота через seqlock; version++ только при полной публикации.
void publishStaged(WeatherState staged, bool bump_version) {
    const uint8_t next = static_cast<uint8_t>(s_active.load(std::memory_order_relaxed) ^ 1u);

    s_seq.fetch_add(1u, std::memory_order_release);

    if (bump_version) {
        staged.version = s_seq.load(std::memory_order_relaxed) + 1u;
    }

    s_buf[next] = staged;

    s_active.store(next, std::memory_order_release);
    s_seq.fetch_add(1u, std::memory_order_release);
}

WeatherState copyActiveSnapshot() {
    const uint8_t idx = s_active.load(std::memory_order_acquire);
    return s_buf[idx];
}

bool activeHasForecastPayload(const WeatherState& s) {
    return s.forecast_valid && s.current.valid;
}

#if YORADIO_WEATHER_REQ_DIAG
const char* weatherLastErrorTag(WeatherLastError err) {
    switch (err) {
        case WeatherLastError::None:           return "none";
        case WeatherLastError::FetchFailed:    return "fetch_failed";
        case WeatherLastError::InternalLow:    return "internal_low";
        case WeatherLastError::NotConfigured:  return "not_configured";
        case WeatherLastError::NotConnected:   return "not_connected";
        default:                               return "?";
    }
}
#endif

} // namespace

void weatherPublishState(const WeatherState& src) {
    WeatherState staged = src;
    staged.fetch_in_progress = false;
    staged.last_error        = WeatherLastError::None;
    staged.last_attempt_at_ms = millis();
    staged.stale             = false;
    publishStaged(staged, true);
}

void weatherStateMarkFetchBegin() {
    WeatherState staged = copyActiveSnapshot();
    staged.fetch_in_progress  = true;
    staged.last_error         = WeatherLastError::None;
    staged.last_attempt_at_ms = millis();
    // stale preserved while refresh runs over LKG / stale сохраняется при refresh поверх LKG
#if YORADIO_WEATHER_REQ_DIAG
    Serial.printf("[WEATHER_STATE] refresh=begin has_data=%d\n",
                  (int)activeHasForecastPayload(staged));
#endif
    publishStaged(staged, false);
}

void weatherStateMarkFetchDeferred() {
    WeatherState staged = copyActiveSnapshot();
    staged.fetch_in_progress  = false;
    staged.last_error         = WeatherLastError::InternalLow;
    staged.last_attempt_at_ms = millis();
    // Do not force stale on defer when LKG exists / не помечаем stale при defer с LKG
#if YORADIO_WEATHER_REQ_DIAG
    Serial.printf("[WEATHER_STATE] refresh=end result=deferred error=internal_low has_data=%d\n",
                  (int)activeHasForecastPayload(staged));
#endif
    publishStaged(staged, false);
}

void weatherStateMarkFetchFailed(WeatherLastError error) {
    WeatherState staged = copyActiveSnapshot();
    staged.fetch_in_progress  = false;
    staged.last_error         = error;
    staged.last_attempt_at_ms = millis();
    if (staged.forecast_valid) {
        staged.stale = true;
    } else {
        staged.stale = false;
    }
#if YORADIO_WEATHER_REQ_DIAG
    Serial.printf("[WEATHER_STATE] refresh=end result=failed error=%s has_data=%d stale=%d\n",
                  weatherLastErrorTag(error),
                  (int)activeHasForecastPayload(staged),
                  (int)staged.stale);
#endif
    publishStaged(staged, false);
}

bool weatherGetStateSnapshot(WeatherState* out) {
    if (!out) return false;

    for (int attempt = 0; attempt < 8; ++attempt) {
        const uint32_t seq1 = s_seq.load(std::memory_order_acquire);
        if (seq1 & 1u) {
            continue;
        }
        const uint8_t idx = s_active.load(std::memory_order_acquire);
        *out = s_buf[idx];
        std::atomic_thread_fence(std::memory_order_acquire);
        const uint32_t seq2 = s_seq.load(std::memory_order_acquire);
        if (seq1 == seq2 && !(seq2 & 1u)) {
            return true;
        }
    }

    const uint32_t seq = s_seq.load(std::memory_order_acquire);
    if (seq & 1u) {
        return false;
    }
    *out = s_buf[s_active.load(std::memory_order_acquire)];
    std::atomic_thread_fence(std::memory_order_acquire);
    const uint32_t seq2 = s_seq.load(std::memory_order_acquire);
    if (seq == seq2 && !(seq2 & 1u)) {
        return true;
    }
    return false;
}
