#ifndef BEOCORD_VU_ASSET_PACK_H
#define BEOCORD_VU_ASSET_PACK_H

#include <cstdint>

#include "../../core/options.h"
#include "beocord_segment_a8.h"
#include "lvgl.h"
#include "../theme/lv_theme_yoradio.h"

// E1+E2+E6B: compile-time Beocord museum asset boundary — theme paths, canvas, mask, window rects.
// E1+E2+E6B: граница Beocord pack — пути тем, холст, маска, координаты окон.

namespace lvgl_ui {

struct BeocordVuAssetPack {
    uint16_t            design_width;
    uint16_t            design_height;
    const lv_img_dsc_t* segment_mask;      // E2: Flash A8 luminosity mask / E2: A8-маска яркости во Flash
    lv_area_t           overlay_rect[2][8]; // E6B: shared central-scale inclusive windows / общие окна
};

#if DSP_MODEL == DSP_ST7701

static constexpr const char kBeocordBgPathDark[] =
    "/visual/beocord9000/480x480/base_off_scale_mid_dark.bin";
static constexpr const char kBeocordBgPathLight[] =
    "/visual/beocord9000/480x480/base_off_scale_mid_light.bin";
static constexpr const char kBeocordBgPathCustom[] =
    "/visual/beocord9000/480x480/base_off_scale_mid_custom.bin";

// E6B: canonical theme → LittleFS background path; unknown → Dark.
// E6B: каноническая тема → путь фона LittleFS; неизвестная → Dark.
inline const char* beocord_background_path_for_preset(ThemePreset preset) {
    switch (preset) {
        case ThemePreset::Light:
            return kBeocordBgPathLight;
        case ThemePreset::Custom:
            return kBeocordBgPathCustom;
        case ThemePreset::Dark:
        default:
            return kBeocordBgPathDark;
    }
}

// Sunton 4848S040 — 480×480 museum background + E6B central-scale segment geometry (L/R × 8).
// Sunton 4848S040 — музейный фон 480×480 + геометрия E6B central-scale (L/R × 8).
static constexpr BeocordVuAssetPack kBeocordVuAssetPack{
    480u,
    480u,
    &img_beocord_segment_a8,
    {
        // L row / ряд L (y = 162)
        {
            {44, 162, 83, 206},
            {89, 162, 128, 206},
            {134, 162, 173, 206},
            {179, 162, 218, 206},
            {224, 162, 263, 206},
            {269, 162, 308, 206},
            {314, 162, 353, 206},
            {359, 162, 398, 206},
        },
        // R row / ряд R (y = 270)
        {
            {44, 270, 83, 314},
            {89, 270, 128, 314},
            {134, 270, 173, 314},
            {179, 270, 218, 314},
            {224, 270, 263, 314},
            {269, 270, 308, 314},
            {314, 270, 353, 314},
            {359, 270, 398, 314},
        },
    },
};
#else
// Other boards: no museum asset in E1/E2 — Visual falls back to theme device_background.
// Другие платы: без музейного ассета в E1/E2 — fallback на device_background темы.
static constexpr BeocordVuAssetPack kBeocordVuAssetPack{
    0u,
    0u,
    nullptr,
    {},
};

inline const char* beocord_background_path_for_preset(ThemePreset) {
    return nullptr;
}

#endif

} // namespace lvgl_ui

#endif
