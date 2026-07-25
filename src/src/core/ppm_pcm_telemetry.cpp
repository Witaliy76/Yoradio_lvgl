/*
 * Pre-Gain decoded stereo PCM telemetry (diagnostic builds only).
 * Телеметрия реального PCM до Gain (только diagnostic builds).
 *
 * Producer: Audio::playChunk pre-Gain tap (Core 1), integer-only hot path.
 * Consumer: Display::loop (Core 0), float/log/Serial here only.
 *
 * Author: Witaliy76 - https://github.com/Witaliy76
 */

#include "options.h"
#include "ppm_pcm_telemetry.h"

#if YORADIO_PPM_PCM_TELEMETRY_DIAG

#include "config.h"

#include <Arduino.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

static constexpr uint32_t kPcmTelemetryFramesPerBlock = 4096u;
static constexpr uint32_t kTelemetryRingCapacity      = 32u;

// ~−1 dBFS and near-full-scale thresholds on |sample| magnitude.
// Пороги ~−1 dBFS и near-full-scale по |sample|.
static constexpr int32_t kMinus1DbfsThreshold    = 29204;
static constexpr int32_t kNearFullScaleThreshold   = 32760;

static constexpr uint32_t kSettlingMs              = 3000u;
static constexpr uint32_t kSummaryWindowSeconds      = 30u;
static constexpr uint32_t kMinValidSampleRateHz      = 8000u;
static constexpr int32_t  kHistDbMin                 = -60;
static constexpr int32_t  kHistDbMax                 = 0;
static constexpr uint32_t kHistBinCount                = 61u;

static uint16_t pcm_sample_abs(int16_t sample) {
    const int32_t v = static_cast<int32_t>(sample);
    if (v == -32768) return 32768u;
    return static_cast<uint16_t>(v < 0 ? -v : v);
}

static void accum_sample(uint16_t abs_mag,
                         uint16_t& peak_abs,
                         uint64_t& sum_squares,
                         uint32_t& over_m1,
                         uint32_t& near_fs) {
    if (abs_mag > peak_abs) peak_abs = abs_mag;
    const int32_t mag = static_cast<int32_t>(abs_mag);
    sum_squares += static_cast<uint64_t>(mag) * static_cast<uint64_t>(mag);
    if (mag >= kMinus1DbfsThreshold) ++over_m1;
    if (mag >= kNearFullScaleThreshold) ++near_fs;
}

// ── Producer accumulator / Накопитель producer ─────────────────────────────

struct ProducerAccumulator {
    uint32_t block_id;
    uint32_t frame_count;
    uint16_t peak_abs_l;
    uint16_t peak_abs_r;
    uint64_t sum_squares_l;
    uint64_t sum_squares_r;
    uint32_t samples_over_minus_1_dbfs_l;
    uint32_t samples_over_minus_1_dbfs_r;
    uint32_t near_full_scale_samples_l;
    uint32_t near_full_scale_samples_r;
    uint32_t sample_rate_hz;
};

static ProducerAccumulator   s_acc = {};
static std::atomic<bool>     s_discard_pending{false};
static std::atomic<uint32_t> s_dropped_total{0};

static PpmPcmTelemetryBlock  s_ring[kTelemetryRingCapacity];
static std::atomic<uint32_t> s_ring_head{0};
static std::atomic<uint32_t> s_ring_tail{0};

static void reset_producer_accumulator() {
    s_acc.block_id                      = 0u;
    s_acc.frame_count                   = 0u;
    s_acc.peak_abs_l                    = 0u;
    s_acc.peak_abs_r                    = 0u;
    s_acc.sum_squares_l                 = 0u;
    s_acc.sum_squares_r                 = 0u;
    s_acc.samples_over_minus_1_dbfs_l   = 0u;
    s_acc.samples_over_minus_1_dbfs_r   = 0u;
    s_acc.near_full_scale_samples_l     = 0u;
    s_acc.near_full_scale_samples_r     = 0u;
    s_acc.sample_rate_hz                = 0u;
}

static bool ring_try_push(const PpmPcmTelemetryBlock& block) {
    const uint32_t tail = s_ring_tail.load(std::memory_order_relaxed);
    const uint32_t next = (tail + 1u) % kTelemetryRingCapacity;
    const uint32_t head = s_ring_head.load(std::memory_order_acquire);
    if (next == head) {
        s_dropped_total.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    s_ring[tail] = block;
    s_ring_tail.store(next, std::memory_order_release);
    return true;
}

static bool ring_try_pop(PpmPcmTelemetryBlock& out) {
    const uint32_t head = s_ring_head.load(std::memory_order_relaxed);
    const uint32_t tail = s_ring_tail.load(std::memory_order_acquire);
    if (head == tail) return false;
    out = s_ring[head];
    s_ring_head.store((head + 1u) % kTelemetryRingCapacity, std::memory_order_release);
    return true;
}

static void ring_drain_all() {
    PpmPcmTelemetryBlock tmp;
    while (ring_try_pop(tmp)) {}
}

static void flush_completed_block() {
    PpmPcmTelemetryBlock block = {};
    block.block_id                        = s_acc.block_id;
    block.frame_count                     = s_acc.frame_count;
    block.peak_abs_l                      = s_acc.peak_abs_l;
    block.peak_abs_r                      = s_acc.peak_abs_r;
    block.sum_squares_l                   = s_acc.sum_squares_l;
    block.sum_squares_r                   = s_acc.sum_squares_r;
    block.samples_over_minus_1_dbfs_l     = s_acc.samples_over_minus_1_dbfs_l;
    block.samples_over_minus_1_dbfs_r     = s_acc.samples_over_minus_1_dbfs_r;
    block.near_full_scale_samples_l       = s_acc.near_full_scale_samples_l;
    block.near_full_scale_samples_r       = s_acc.near_full_scale_samples_r;
    block.sample_rate_hz                  = s_acc.sample_rate_hz;

    if (!ring_try_push(block)) {
        // ring_try_push already counted drop / ring_try_push уже учёл drop
    }

    ++s_acc.block_id;
    s_acc.frame_count                   = 0u;
    s_acc.peak_abs_l                    = 0u;
    s_acc.peak_abs_r                    = 0u;
    s_acc.sum_squares_l                 = 0u;
    s_acc.sum_squares_r                 = 0u;
    s_acc.samples_over_minus_1_dbfs_l   = 0u;
    s_acc.samples_over_minus_1_dbfs_r   = 0u;
    s_acc.near_full_scale_samples_l     = 0u;
    s_acc.near_full_scale_samples_r     = 0u;
}

// ── Consumer state / Состояние consumer ────────────────────────────────────

struct SummaryAgg {
    uint32_t blocks;
    uint32_t frames;
    uint16_t peak_l;
    uint16_t peak_r;
    uint64_t sum_sq_l;
    uint64_t sum_sq_r;
    uint32_t over_m1_l;
    uint32_t over_m1_r;
    uint32_t fs_l;
    uint32_t fs_r;
    uint16_t hist_peak_l[kHistBinCount];
    uint16_t hist_peak_r[kHistBinCount];
};

static uint16_t           s_station_id       = 0xFFFFu;
static uint32_t           s_settling_until_ms = 0u;
static uint32_t           s_active_sample_rate_hz = 0u;
static SummaryAgg         s_summary          = {};
static uint32_t           s_summary_frames   = 0u;
static uint32_t           s_summary_target_frames = 0u;
static uint32_t           s_summary_rate_hz  = 0u;
static uint32_t           s_summary_lost     = 0u;
static uint32_t           s_summary_lost_base = 0u;

static float clamp_dbfs(float db) {
    if (db < -120.0f) return -120.0f;
    if (db > 0.0f) return 0.0f;
    return db;
}

static float peak_abs_to_dbfs(uint16_t peak_abs) {
    if (peak_abs == 0u) return -120.0f;
    return clamp_dbfs(20.0f * log10f(static_cast<float>(peak_abs) / 32768.0f));
}

static float rms_to_dbfs(uint64_t sum_squares, uint32_t frames) {
    if (frames == 0u || sum_squares == 0u) return -120.0f;
    const double rms = sqrt(static_cast<double>(sum_squares) / static_cast<double>(frames));
    return clamp_dbfs(20.0f * log10f(static_cast<float>(rms / 32768.0)));
}

static uint32_t peak_hist_bin(uint16_t peak_abs) {
    const float db = peak_abs_to_dbfs(peak_abs);
    int32_t bin = static_cast<int32_t>(lroundf(db)) - kHistDbMin;
    if (bin < 0) bin = 0;
    if (bin >= static_cast<int32_t>(kHistBinCount)) bin = static_cast<int32_t>(kHistBinCount) - 1;
    return static_cast<uint32_t>(bin);
}

static void reset_summary_agg() {
    s_summary = {};
    s_summary_frames = 0u;
    s_summary_target_frames = 0u;
    s_summary_rate_hz = 0u;
    s_summary_lost   = 0u;
    s_summary_lost_base = ppmPcmTelemetryDroppedBlocks();
}

static uint32_t summary_target_frames_for_rate(uint32_t sample_rate_hz) {
    if (sample_rate_hz < kMinValidSampleRateHz) return 0u;
    return sample_rate_hz * kSummaryWindowSeconds;
}

static void ensure_summary_target_rate(uint32_t sample_rate_hz) {
    if (sample_rate_hz < kMinValidSampleRateHz) return;
    if (s_summary_target_frames != 0u) return;
    s_summary_rate_hz = sample_rate_hz;
    s_summary_target_frames = summary_target_frames_for_rate(sample_rate_hz);
}

static void merge_block_into_summary(const PpmPcmTelemetryBlock& b) {
    ++s_summary.blocks;
    s_summary.frames += b.frame_count;
    if (b.peak_abs_l > s_summary.peak_l) s_summary.peak_l = b.peak_abs_l;
    if (b.peak_abs_r > s_summary.peak_r) s_summary.peak_r = b.peak_abs_r;
    s_summary.sum_sq_l += b.sum_squares_l;
    s_summary.sum_sq_r += b.sum_squares_r;
    s_summary.over_m1_l += b.samples_over_minus_1_dbfs_l;
    s_summary.over_m1_r += b.samples_over_minus_1_dbfs_r;
    s_summary.fs_l += b.near_full_scale_samples_l;
    s_summary.fs_r += b.near_full_scale_samples_r;
    ++s_summary.hist_peak_l[peak_hist_bin(b.peak_abs_l)];
    ++s_summary.hist_peak_r[peak_hist_bin(b.peak_abs_r)];
}

static float hist_percentile_peak_dbfs(const uint16_t* hist, float percentile) {
    if (s_summary.blocks == 0u) return -120.0f;

    const uint32_t target = static_cast<uint32_t>(
        ceilf(static_cast<float>(s_summary.blocks) * percentile));
    uint32_t seen = 0u;
    for (uint32_t bin = 0u; bin < kHistBinCount; ++bin) {
        seen += hist[bin];
        if (seen >= target) {
            const int32_t db = kHistDbMin + static_cast<int32_t>(bin);
            return clamp_dbfs(static_cast<float>(db));
        }
    }
    return 0.0f;
}

static void print_thirty_second_summary(uint16_t station) {
    if (s_summary.blocks == 0u || s_summary.frames == 0u) {
        reset_summary_agg();
        return;
    }

    const float l_p50  = hist_percentile_peak_dbfs(s_summary.hist_peak_l, 0.50f);
    const float l_p95  = hist_percentile_peak_dbfs(s_summary.hist_peak_l, 0.95f);
    const float l_max  = peak_abs_to_dbfs(s_summary.peak_l);
    const float l_rms  = rms_to_dbfs(s_summary.sum_sq_l, s_summary.frames);
    const float l_crest = l_max - l_rms;

    const float r_p50  = hist_percentile_peak_dbfs(s_summary.hist_peak_r, 0.50f);
    const float r_p95  = hist_percentile_peak_dbfs(s_summary.hist_peak_r, 0.95f);
    const float r_max  = peak_abs_to_dbfs(s_summary.peak_r);
    const float r_rms  = rms_to_dbfs(s_summary.sum_sq_r, s_summary.frames);
    const float r_crest = r_max - r_rms;

    const float over_m1_l_pct = 100.0f * static_cast<float>(s_summary.over_m1_l)
        / static_cast<float>(s_summary.frames);
    const float over_m1_r_pct = 100.0f * static_cast<float>(s_summary.over_m1_r)
        / static_cast<float>(s_summary.frames);

    Serial.printf(
        "[PCM_TLM_SUM] station=%u rate_hz=%lu seconds=30 blocks=%lu "
        "L p50_peak=%.1f p95_peak=%.1f max_peak=%.1f rms=%.1f crest=%.1f over_m1=%.3f%% fs=%lu "
        "R p50_peak=%.1f p95_peak=%.1f max_peak=%.1f rms=%.1f crest=%.1f over_m1=%.3f%% fs=%lu "
        "lost=%lu\n",
        static_cast<unsigned>(station),
        static_cast<unsigned long>(s_summary_rate_hz),
        static_cast<unsigned long>(s_summary.blocks),
        l_p50, l_p95, l_max, l_rms, l_crest, over_m1_l_pct,
        static_cast<unsigned long>(s_summary.fs_l),
        r_p50, r_p95, r_max, r_rms, r_crest, over_m1_r_pct,
        static_cast<unsigned long>(s_summary.fs_r),
        static_cast<unsigned long>(s_summary_lost));

    reset_summary_agg();
}

static void on_station_change(uint16_t old_station, uint16_t new_station, uint32_t now_ms) {
    Serial.printf("[PCM_TLM] reset reason=station_change old=%u new=%u\n",
                  static_cast<unsigned>(old_station),
                  static_cast<unsigned>(new_station));
    ppmPcmTelemetryDiscardPending();
    reset_summary_agg();
    s_station_id              = new_station;
    s_settling_until_ms       = now_ms + kSettlingMs;
    s_active_sample_rate_hz   = 0u;
    s_summary_frames          = 0u;
    s_summary_target_frames   = 0u;
    s_summary_rate_hz         = 0u;
}

static void on_sample_rate_change(uint32_t old_rate_hz, uint32_t new_rate_hz) {
    Serial.printf("[PCM_TLM] reset reason=sample_rate_change old=%lu new=%lu\n",
                  static_cast<unsigned long>(old_rate_hz),
                  static_cast<unsigned long>(new_rate_hz));
    ppmPcmTelemetryDiscardPending();
    reset_summary_agg();
    s_active_sample_rate_hz = new_rate_hz;
    s_summary_frames        = 0u;
    s_summary_target_frames = 0u;
    s_summary_rate_hz       = 0u;
}

static void handle_block_sample_rate(const PpmPcmTelemetryBlock& block) {
    if (block.sample_rate_hz < kMinValidSampleRateHz) return;

    if (s_active_sample_rate_hz != 0u && block.sample_rate_hz != s_active_sample_rate_hz) {
        on_sample_rate_change(s_active_sample_rate_hz, block.sample_rate_hz);
    }

    if (s_active_sample_rate_hz == 0u) {
        s_active_sample_rate_hz = block.sample_rate_hz;
    }
}

} // namespace

// ── Public API / Публичный API ─────────────────────────────────────────────

void ppmPcmTelemetryAccumulateFrame(int16_t left, int16_t right, uint32_t sample_rate_hz) {
    if (s_discard_pending.exchange(false, std::memory_order_acq_rel)) {
        reset_producer_accumulator();
    }

    if (sample_rate_hz >= kMinValidSampleRateHz) {
        s_acc.sample_rate_hz = sample_rate_hz;
    }

    const uint16_t abs_l = pcm_sample_abs(left);
    const uint16_t abs_r = pcm_sample_abs(right);
    accum_sample(abs_l, s_acc.peak_abs_l, s_acc.sum_squares_l,
                 s_acc.samples_over_minus_1_dbfs_l, s_acc.near_full_scale_samples_l);
    accum_sample(abs_r, s_acc.peak_abs_r, s_acc.sum_squares_r,
                 s_acc.samples_over_minus_1_dbfs_r, s_acc.near_full_scale_samples_r);
    ++s_acc.frame_count;

    if (s_acc.frame_count >= kPcmTelemetryFramesPerBlock) {
        flush_completed_block();
    }
}

bool ppmPcmTelemetryPop(PpmPcmTelemetryBlock& out) {
    return ring_try_pop(out);
}

uint32_t ppmPcmTelemetryDroppedBlocks() {
    return s_dropped_total.load(std::memory_order_relaxed);
}

void ppmPcmTelemetryDiscardPending() {
    ring_drain_all();
    s_discard_pending.store(true, std::memory_order_release);
}

void ppmPcmTelemetryConsumerService() {
    const uint32_t now_ms = millis();
    const uint16_t station = config.lastStation();

    if (s_station_id == 0xFFFFu) {
        s_station_id          = station;
        s_settling_until_ms   = now_ms + kSettlingMs;
        s_active_sample_rate_hz = 0u;
        s_summary_lost_base   = ppmPcmTelemetryDroppedBlocks();
    } else if (station != s_station_id) {
        on_station_change(s_station_id, station, now_ms);
    }

    PpmPcmTelemetryBlock block;
    while (ppmPcmTelemetryPop(block)) {
        if (now_ms < s_settling_until_ms) {
            continue;
        }

        handle_block_sample_rate(block);
        ensure_summary_target_rate(block.sample_rate_hz);
        merge_block_into_summary(block);
        s_summary_frames += block.frame_count;

        if (s_summary_target_frames > 0u && s_summary_frames >= s_summary_target_frames) {
            s_summary_lost = ppmPcmTelemetryDroppedBlocks() - s_summary_lost_base;
            print_thirty_second_summary(station);
        }
    }
}

#endif // YORADIO_PPM_PCM_TELEMETRY_DIAG
