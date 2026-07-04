#ifndef BEOCORD_VU_ASSET_PACK_H
#define BEOCORD_VU_ASSET_PACK_H

#include <cstdint>

#include "../../core/options.h"
#include "beocord_segment_a8.h"
#include "lvgl.h"

// E1+E2: compile-time Beocord museum asset boundary — path, design canvas, segment mask, window rects.
// E1+E2: граница target-pack Beocord — путь, холст, маска сегмента, координаты окон.

namespace lvgl_ui {

struct BeocordVuAssetPack {
    const char*         background_path; // LittleFS runtime path / путь в LittleFS
    uint16_t            design_width;
    uint16_t            design_height;
    const lv_img_dsc_t* segment_mask;      // E2: Flash A8 luminosity mask / E2: A8-маска яркости во Flash
    lv_area_t           overlay_rect[2][8]; // E0B canonical inclusive window rects / канонические inclusive-окна
};

#if DSP_MODEL == DSP_ST7701
// Sunton 4848S040 — 480×480 museum background + E2 segment geometry (L/R × 8).
// Sunton 4848S040 — музейный фон 480×480 + геометрия сегментов E2 (L/R × 8).
static constexpr BeocordVuAssetPack kBeocordVuAssetPack{
    "/visual/beocord9000/480x480/base_off_scale_on.bin",
    480u,
    480u,
    &img_beocord_segment_a8,
    {
        // L row / ряд L
        {
            {44, 191, 83, 235},
            {89, 191, 128, 235},
            {134, 191, 173, 235},
            {179, 191, 218, 235},
            {224, 191, 263, 235},
            {269, 191, 308, 235},
            {314, 191, 353, 235},
            {359, 191, 398, 235},
        },
        // R row / ряд R
        {
            {44, 266, 83, 310},
            {89, 266, 128, 310},
            {134, 266, 173, 310},
            {179, 266, 218, 310},
            {224, 266, 263, 310},
            {269, 266, 308, 310},
            {314, 266, 353, 310},
            {359, 266, 398, 310},
        },
    },
};
#else
// Other boards: no museum asset in E1/E2 — Visual falls back to theme device_background.
// Другие платы: без музейного ассета в E1/E2 — fallback на device_background темы.
static constexpr BeocordVuAssetPack kBeocordVuAssetPack{
    nullptr,
    0u,
    0u,
    nullptr,
    {},
};
#endif

} // namespace lvgl_ui

#endif
