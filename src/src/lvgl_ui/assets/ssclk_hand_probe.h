/*
 * SSCLK-P0A probe hand — LV_IMG_CF_TRUE_COLOR_ALPHA (RGB565 LE + alpha), programmatic source.
 * SSCLK-P0A probe-стрелка — TRUE_COLOR_ALPHA, симметричный стержень без hub/cap в sprite.
 *
 * Source: assets/ssclk_hand_probe_source.png (programmatic, not production art).
 * Pivot: bottom-center (6, 79) — shared rotation center for all probe hands.
 * Pivot: нижний центр (6, 79) — общий центр вращения для probe-стрелок.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t ssclk_hand_probe_map[];

#ifdef __cplusplus
}
#endif

#define SSCLK_HAND_PROBE_W 13u
#define SSCLK_HAND_PROBE_H 80u
#define SSCLK_HAND_PROBE_PIVOT_X 6u
#define SSCLK_HAND_PROBE_PIVOT_Y (SSCLK_HAND_PROBE_H - 1u)
#define SSCLK_HAND_PROBE_DATA_SIZE (SSCLK_HAND_PROBE_W * SSCLK_HAND_PROBE_H * 3u)
