#ifndef PPM_PCM_TELEMETRY_H
#define PPM_PCM_TELEMETRY_H

#include <cstdint>

#ifndef YORADIO_PPM_PCM_TELEMETRY_DIAG
#define YORADIO_PPM_PCM_TELEMETRY_DIAG 0
#endif

#if YORADIO_PPM_PCM_TELEMETRY_DIAG

// E5A: pre-Gain decoded stereo PCM telemetry block (~4096 frames, ~86–93 ms).
// E5A: блок телеметрии decoded stereo PCM до Gain (~4096 кадров).
struct PpmPcmTelemetryBlock {
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

// Producer (Audio / Core 1): one call per decoded stereo frame, pre-Gain.
// sample_rate_hz: decoded PCM rate from Audio::getSampleRate() (not station metadata).
// Producer (Audio / Core 1): один вызов на decoded stereo frame, до Gain.
// sample_rate_hz: decoded PCM rate из Audio::getSampleRate() (не metadata станции).
void ppmPcmTelemetryAccumulateFrame(int16_t left, int16_t right, uint32_t sample_rate_hz);

// Consumer (Display loop): drain one completed block from SPSC ring.
// Consumer (Display loop): извлечь один готовый блок из SPSC ring.
bool ppmPcmTelemetryPop(PpmPcmTelemetryBlock& out);

// Cumulative dropped blocks when ring was full (never blocks Audio).
// Суммарно отброшенных блоков при переполнении ring (Audio не блокируется).
uint32_t ppmPcmTelemetryDroppedBlocks();

// Discard in-flight producer accumulator and drain pending ring blocks.
// Сбросить накопитель producer и очистить ring (смена станции / settling).
void ppmPcmTelemetryDiscardPending();

// Consumer service: drain ring, 1 Hz lines, 30 s summary (reads station id internally).
// Consumer service: drain ring, строки 1 Гц, summary 30 с (station id внутри).
void ppmPcmTelemetryConsumerService();

#else

struct PpmPcmTelemetryBlock {};

inline void ppmPcmTelemetryAccumulateFrame(int16_t, int16_t, uint32_t) {}

inline bool ppmPcmTelemetryPop(PpmPcmTelemetryBlock&) { return false; }

inline uint32_t ppmPcmTelemetryDroppedBlocks() { return 0u; }

inline void ppmPcmTelemetryDiscardPending() {}

inline void ppmPcmTelemetryConsumerService() {}

#endif

#endif
