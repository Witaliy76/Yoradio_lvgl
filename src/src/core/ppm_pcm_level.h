#ifndef PPM_PCM_LEVEL_H
#define PPM_PCM_LEVEL_H

#include <cstdint>

// Pre-Gain PCM level source for Visual PPM (hybrid RMS body + transient peak).
// Pre-Gain PCM level source для Visual PPM (RMS body + transient peak).
// Author: Witaliy76 - https://github.com/Witaliy76

struct PpmPcmLevelSnapshot {
    uint32_t block_id;
    uint32_t sample_rate_hz;
    uint32_t rms_frame_count;
    uint16_t short_peak_left;
    uint16_t short_peak_right;
    uint64_t rms_sum_squares_left;
    uint64_t rms_sum_squares_right;
    uint64_t fast_rms_sum_squares_left;
    uint64_t fast_rms_sum_squares_right;
    uint32_t fast_rms_frame_count;
    bool     valid;
};

void ppmPcmLevelSetEnabled(bool enabled);
void ppmPcmLevelRequestReset();

void ppmPcmLevelAccumulateFrame(int16_t left, int16_t right, uint32_t sample_rate_hz);

bool ppmPcmLevelReadSnapshot(PpmPcmLevelSnapshot& out);

#endif
