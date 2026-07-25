/*
 * Rolling pre-Gain PCM level source for Visual PPM (Audio producer, Visual consumer).
 * Rolling PCM level для Visual PPM (producer Audio, consumer Visual).
 *
 * Integer-only hot path; seqlock snapshot; no Config/station/ovol access.
 *
 * Author: Witaliy76 - https://github.com/Witaliy76
 */

#include "ppm_pcm_level.h"

#include <atomic>
#include <cstdint>

namespace {

static constexpr uint32_t kPpmLevelBlockFrames   = 512u;
static constexpr uint32_t kPpmRmsWindowMs        = 400u;
static constexpr uint32_t kPpmFastRmsWindowMs    = 100u;
static constexpr uint32_t kPpmPeakWindowMs         = 25u;
static constexpr uint32_t kPpmHistoryCapacity      = 96u;
static constexpr uint32_t kMinValidSampleRateHz    = 8000u;
static constexpr uint32_t kMsPerBlockDenominator = kPpmLevelBlockFrames * 1000u;

struct BlockHistorySlot {
    uint16_t peak_l;
    uint16_t peak_r;
    uint64_t sum_sq_l;
    uint64_t sum_sq_r;
};

struct ProducerBlock {
    uint32_t frame_count;
    uint16_t peak_l;
    uint16_t peak_r;
    uint64_t sum_sq_l;
    uint64_t sum_sq_r;
};

static std::atomic<bool>     s_enabled{false};
static std::atomic<bool>     s_reset_pending{false};
static std::atomic<uint32_t>   s_seq{0};

static PpmPcmLevelSnapshot   s_snapshot = {};
static BlockHistorySlot      s_history[kPpmHistoryCapacity] = {};
static uint32_t              s_hist_write = 0u;
static uint32_t              s_block_id = 0u;
static uint32_t              s_active_rate_hz = 0u;
static ProducerBlock         s_partial = {};

static uint16_t pcm_sample_abs(int16_t sample) {
    const int32_t v = static_cast<int32_t>(sample);
    if (v == -32768) return 32768u;
    return static_cast<uint16_t>(v < 0 ? -v : v);
}

static void accum_sample(uint16_t abs_mag,
                         uint16_t& peak_abs,
                         uint64_t& sum_squares) {
    if (abs_mag > peak_abs) peak_abs = abs_mag;
    const int32_t mag = static_cast<int32_t>(abs_mag);
    sum_squares += static_cast<uint64_t>(mag) * static_cast<uint64_t>(mag);
}

static uint32_t window_blocks_for_ms(uint32_t sample_rate_hz, uint32_t window_ms) {
    if (sample_rate_hz < kMinValidSampleRateHz) return 1u;
    const uint64_t numerator =
        static_cast<uint64_t>(window_ms) * static_cast<uint64_t>(sample_rate_hz)
        + static_cast<uint64_t>(kMsPerBlockDenominator / 2u);
    uint32_t blocks = static_cast<uint32_t>(numerator / static_cast<uint64_t>(kMsPerBlockDenominator));
    if (blocks < 1u) blocks = 1u;
    return blocks;
}

static void reset_producer_state() {
    s_partial = {};
    s_hist_write = 0u;
    s_block_id = 0u;
    s_active_rate_hz = 0u;

    uint32_t seq = s_seq.load(std::memory_order_relaxed);
    s_seq.store(seq + 1u, std::memory_order_release);
    s_snapshot = {};
    s_snapshot.valid = false;
    s_seq.store(seq + 2u, std::memory_order_release);
}

static uint32_t clamp_window_blocks(uint32_t blocks) {
    if (blocks < 1u) return 1u;
    if (blocks > kPpmHistoryCapacity) return kPpmHistoryCapacity;
    return blocks;
}

static void sum_squares_over_blocks(uint32_t block_count,
                                    uint64_t& sum_l,
                                    uint64_t& sum_r) {
    sum_l = 0u;
    sum_r = 0u;
    for (uint32_t i = 0u; i < block_count; ++i) {
        const uint32_t idx = (s_hist_write - 1u - i) % kPpmHistoryCapacity;
        const BlockHistorySlot& slot = s_history[idx];
        sum_l += slot.sum_sq_l;
        sum_r += slot.sum_sq_r;
    }
}

static void max_peaks_over_blocks(uint32_t block_count,
                                  uint16_t& peak_l,
                                  uint16_t& peak_r) {
    peak_l = 0u;
    peak_r = 0u;
    for (uint32_t i = 0u; i < block_count; ++i) {
        const uint32_t idx = (s_hist_write - 1u - i) % kPpmHistoryCapacity;
        const BlockHistorySlot& slot = s_history[idx];
        if (slot.peak_l > peak_l) peak_l = slot.peak_l;
        if (slot.peak_r > peak_r) peak_r = slot.peak_r;
    }
}

static void publish_snapshot(uint32_t sample_rate_hz,
                             uint32_t rms_blocks,
                             uint32_t fast_rms_blocks,
                             uint32_t peak_blocks) {
    const uint32_t available = (s_hist_write < kPpmHistoryCapacity)
        ? s_hist_write
        : kPpmHistoryCapacity;
    if (available == 0u) return;

    const uint32_t rms_count = clamp_window_blocks(
        (rms_blocks < available) ? rms_blocks : available);
    const uint32_t fast_count = clamp_window_blocks(
        (fast_rms_blocks < available) ? fast_rms_blocks : available);
    const uint32_t peak_count = clamp_window_blocks(
        (peak_blocks < available) ? peak_blocks : available);

    uint64_t rms_sum_l = 0u;
    uint64_t rms_sum_r = 0u;
    uint64_t fast_sum_l = 0u;
    uint64_t fast_sum_r = 0u;
    uint16_t peak_l = 0u;
    uint16_t peak_r = 0u;

    sum_squares_over_blocks(rms_count, rms_sum_l, rms_sum_r);
    sum_squares_over_blocks(fast_count, fast_sum_l, fast_sum_r);
    max_peaks_over_blocks(peak_count, peak_l, peak_r);

    PpmPcmLevelSnapshot snap = {};
    snap.block_id = s_block_id;
    snap.sample_rate_hz = sample_rate_hz;
    snap.rms_frame_count = rms_count * kPpmLevelBlockFrames;
    snap.fast_rms_frame_count = fast_count * kPpmLevelBlockFrames;
    snap.short_peak_left = peak_l;
    snap.short_peak_right = peak_r;
    snap.rms_sum_squares_left = rms_sum_l;
    snap.rms_sum_squares_right = rms_sum_r;
    snap.fast_rms_sum_squares_left = fast_sum_l;
    snap.fast_rms_sum_squares_right = fast_sum_r;
    snap.valid = (snap.rms_frame_count > 0u);

    uint32_t seq = s_seq.load(std::memory_order_relaxed);
    s_seq.store(seq + 1u, std::memory_order_release);
    s_snapshot = snap;
    s_seq.store(seq + 2u, std::memory_order_release);
}

static void commit_completed_block() {
    const uint32_t idx = s_hist_write % kPpmHistoryCapacity;
    s_history[idx].peak_l = s_partial.peak_l;
    s_history[idx].peak_r = s_partial.peak_r;
    s_history[idx].sum_sq_l = s_partial.sum_sq_l;
    s_history[idx].sum_sq_r = s_partial.sum_sq_r;
    ++s_hist_write;
    ++s_block_id;

    const uint32_t rms_blocks = window_blocks_for_ms(s_active_rate_hz, kPpmRmsWindowMs);
    const uint32_t fast_rms_blocks = window_blocks_for_ms(s_active_rate_hz, kPpmFastRmsWindowMs);
    const uint32_t peak_blocks = window_blocks_for_ms(s_active_rate_hz, kPpmPeakWindowMs);
    publish_snapshot(s_active_rate_hz, rms_blocks, fast_rms_blocks, peak_blocks);

    s_partial.frame_count = 0u;
    s_partial.peak_l = 0u;
    s_partial.peak_r = 0u;
    s_partial.sum_sq_l = 0u;
    s_partial.sum_sq_r = 0u;
}

static void handle_rate_or_reset(uint32_t sample_rate_hz) {
    if (s_reset_pending.exchange(false, std::memory_order_acq_rel)) {
        reset_producer_state();
    }

    if (sample_rate_hz < kMinValidSampleRateHz) return;

    if (s_active_rate_hz != 0u && sample_rate_hz != s_active_rate_hz) {
        s_partial = {};
        s_hist_write = 0u;
        s_block_id = 0u;

        uint32_t seq = s_seq.load(std::memory_order_relaxed);
        s_seq.store(seq + 1u, std::memory_order_release);
        s_snapshot = {};
        s_snapshot.valid = false;
        s_seq.store(seq + 2u, std::memory_order_release);
    }

    s_active_rate_hz = sample_rate_hz;
}

} // namespace

void ppmPcmLevelSetEnabled(bool enabled) {
    s_enabled.store(enabled, std::memory_order_release);
    if (enabled) {
        s_reset_pending.store(true, std::memory_order_release);
    } else {
        uint32_t seq = s_seq.load(std::memory_order_relaxed);
        s_seq.store(seq + 1u, std::memory_order_release);
        s_snapshot.valid = false;
        s_seq.store(seq + 2u, std::memory_order_release);
    }
}

void ppmPcmLevelRequestReset() {
    s_reset_pending.store(true, std::memory_order_release);
}

void ppmPcmLevelAccumulateFrame(int16_t left, int16_t right, uint32_t sample_rate_hz) {
    if (!s_enabled.load(std::memory_order_acquire)) return;

    handle_rate_or_reset(sample_rate_hz);
    if (sample_rate_hz < kMinValidSampleRateHz) return;

    const uint16_t abs_l = pcm_sample_abs(left);
    const uint16_t abs_r = pcm_sample_abs(right);
    accum_sample(abs_l, s_partial.peak_l, s_partial.sum_sq_l);
    accum_sample(abs_r, s_partial.peak_r, s_partial.sum_sq_r);
    ++s_partial.frame_count;

    if (s_partial.frame_count >= kPpmLevelBlockFrames) {
        commit_completed_block();
    }
}

bool ppmPcmLevelReadSnapshot(PpmPcmLevelSnapshot& out) {
    for (uint8_t attempt = 0u; attempt < 4u; ++attempt) {
        const uint32_t seq1 = s_seq.load(std::memory_order_acquire);
        if (seq1 & 1u) continue;
        out = s_snapshot;
        const uint32_t seq2 = s_seq.load(std::memory_order_acquire);
        if (seq1 == seq2) {
            return out.valid;
        }
    }
    out = {};
    out.valid = false;
    return false;
}
