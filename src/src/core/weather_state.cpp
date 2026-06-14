#include "weather_state.h"
#include <atomic>
#include <string.h>

/*
 * Weather W1 — double-buffer + seqlock publication for WeatherState.
 * Weather W1 — double-buffer + seqlock публикация WeatherState.
 *
 * Concurrency model / Модель конкуренции:
 *   - Single writer (weatherPublishState) from weather-sync context (doSync, Core 0).
 *   - Multiple readers (weatherGetStateSnapshot), including future DspTask/LVGL readers
 *     that may run on a different core than the writer.
 *   - Lock-free readers: no mutex, no blocking in DspTask.
 *   - Один писатель; несколько читателей, в т.ч. с другого ядра; без mutex.
 *
 * Cross-core safety / Безопасность между ядрами:
 *   - Classic seqlock: odd seq = publish in progress, even seq = stable snapshot.
 *   - std::atomic acquire/release fences visibility across ESP32-S3 cores.
 *   - Double-buffer: writer always fills the inactive slot; readers copy the active slot.
 *   - Seqlock: нечётный seq = публикация идёт, чётный = стабильный снапшот; atomic fences.
 */

namespace {

WeatherState              s_buf[2];  // zero-init: version=0, forecast_valid=false
std::atomic<uint8_t>      s_active{0};
std::atomic<uint32_t>     s_seq{0};  // even = stable, odd = writer busy

} // namespace

void weatherPublishState(const WeatherState& src) {
    const uint8_t next = static_cast<uint8_t>(s_active.load(std::memory_order_relaxed) ^ 1u);

    // Odd seq → readers spin/retry instead of copying a half-published buffer.
    // Нечётный seq → читатели retry, не копируют полуопубликованный буфер.
    s_seq.fetch_add(1u, std::memory_order_release);

    WeatherState staged = src;
    staged.version = s_seq.load(std::memory_order_relaxed) + 1u; // stamped even version
    s_buf[next] = staged;

    s_active.store(next, std::memory_order_release);
    s_seq.fetch_add(1u, std::memory_order_release); // back to even → snapshot stable
}

bool weatherGetStateSnapshot(WeatherState* out) {
    if (!out) return false;

    // Seqlock retry loop — bounded to avoid infinite spin on pathological contention.
    // Seqlock-retry с ограничением попыток.
    for (int attempt = 0; attempt < 8; ++attempt) {
        const uint32_t seq1 = s_seq.load(std::memory_order_acquire);
        if (seq1 & 1u) {
            continue; // writer busy / писатель занят
        }
        const uint8_t idx = s_active.load(std::memory_order_acquire);
        *out = s_buf[idx];
        std::atomic_thread_fence(std::memory_order_acquire);
        const uint32_t seq2 = s_seq.load(std::memory_order_acquire);
        if (seq1 == seq2 && !(seq2 & 1u)) {
            return true;
        }
    }

    // Fallback: one attempt with pre- AND post-copy seq verification.
    //
    // Without the post-copy check the double-buffer alone cannot prevent a torn read:
    // if one full write cycle completes between the reader's s_active load and its buffer
    // copy, the formerly-active slot becomes inactive and a new writer can start overwriting
    // it concurrently with the reader's copy — seq is never re-checked in the old path.
    //
    // Запасной путь: одна попытка с проверкой seq ДО и ПОСЛЕ копирования.
    // Без пост-проверки двойной буфер не защищает: один полный цикл записи между load idx
    // и копированием делает «активный» слот inactive — следующий писатель начинает его
    // переписывать одновременно с читателем (первая проверка seq это не увидит).
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
    return false; // write activity detected between checks — do not expose potentially torn data
    // Активность записи между проверками — не возвращаем потенциально разорванный снапшот.
}
