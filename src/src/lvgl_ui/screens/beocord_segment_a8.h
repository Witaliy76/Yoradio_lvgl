#ifndef BEOCORD_SEGMENT_A8_H
#define BEOCORD_SEGMENT_A8_H

#include "lvgl.h"

// E2: reusable 40×45 alpha mask for museum PPM segments (Flash, recolor at runtime).
// E2: переиспользуемая A8-маска 40×45 для музейных сегментов PPM (Flash, цвет через recolor).

#define BEOCORD_SEGMENT_A8_W 40u
#define BEOCORD_SEGMENT_A8_H 45u
#define BEOCORD_SEGMENT_A8_DATA_SIZE (BEOCORD_SEGMENT_A8_W * BEOCORD_SEGMENT_A8_H)

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_img_dsc_t img_beocord_segment_a8;

#ifdef __cplusplus
}
#endif

#endif
