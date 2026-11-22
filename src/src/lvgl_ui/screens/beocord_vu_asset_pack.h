#ifndef BEOCORD_VU_ASSET_PACK_H
#define BEOCORD_VU_ASSET_PACK_H

#include <cstdint>

#include "../../core/options.h"
#include "beocord_segment_a8.h"
#include "lvgl.h"
#include "../theme/lv_theme_yoradio.h"
#include "../profiles/resolved_ui_metrics.h"

// E1+E2+E6B: compile-time Beocord museum asset boundary — theme paths, canvas, mask, window rects.
// E1+E2+E6B: граница Beocord pack — пути тем, холст, маска, координаты окон.

namespace lvgl_ui {

struct BeocordVuAssetPack {
    uint16_t            design_width;
    uint16_t            design_height;
    const lv_img_dsc_t* segment_mask;      // E2: Flash A8 luminosity mask / E2: A8-маска яркости во Flash
    lv_area_t           overlay_rect[2][8]; // E6B: shared central-scale inclusive windows / общие окна
};

static constexpr const char kBeocordBgPathDark[] =
    "/visual/beocord9000/480x480/base_off_scale_mid_dark.bin";
static constexpr const char kBeocordBgPathLight[] =
    "/visual/beocord9000/480x480/base_off_scale_mid_light.bin";
static constexpr const char kBeocordBgPathCustom[] =
    "/visual/beocord9000/480x480/base_off_scale_mid_custom.bin";

// E6B: canonical theme → LittleFS background path; unknown → Dark.
// E6B: каноническая тема → путь фона LittleFS; неизвестная → Dark.
inline const char* beocord_square480_background_path(ThemePreset preset) {
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

// UiProfile::Square480 — 480×480 museum background + E6B central-scale segment geometry (L/R × 8).
// UiProfile::Square480 — музейный фон 480×480 + геометрия E6B central-scale (L/R × 8).
static constexpr BeocordVuAssetPack kBeocordVuAssetPackSquare480{
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

// Profiles without a museum asset: Visual falls back to the theme device_background.
// Профили без музейного ассета: Visual использует device_background темы.
static constexpr BeocordVuAssetPack kBeocordVuAssetPackNone{0u, 0u, nullptr, {}};

// Raster variant is chosen by UiProfile (Freeze §25), not by the display model.
// Растровый вариант выбирается по UiProfile (Freeze §25), а не по модели дисплея.
constexpr const BeocordVuAssetPack& beocordPackFor(yoradio::UiProfile profile) {
    switch (profile) {
        case yoradio::UiProfile::Square480: return kBeocordVuAssetPackSquare480;
        default:                            return kBeocordVuAssetPackNone;
    }
}

static constexpr const BeocordVuAssetPack& kBeocordVuAssetPack =
    beocordPackFor(kResolvedUiMetrics.profile);

// design_width 0 marks "no museum pack" for this profile.
// design_width 0 означает «нет музейного пакета» для профиля.
static_assert(kBeocordVuAssetPack.design_width == 0u ||
                  (kBeocordVuAssetPack.design_width == kResolvedUiMetrics.viewport_width &&
                   kBeocordVuAssetPack.design_height == kResolvedUiMetrics.viewport_height),
              "Beocord asset pack design size must equal the UI profile viewport.");

// E6B: canonical theme → LittleFS background path; unknown → Dark; no pack → nullptr.
// E6B: тема → путь фона LittleFS; неизвестная → Dark; нет пакета → nullptr.
inline const char* beocord_background_path_for_preset(ThemePreset preset) {
    switch (kResolvedUiMetrics.profile) {
        case yoradio::UiProfile::Square480: return beocord_square480_background_path(preset);
        default:                            return nullptr;
    }
}

} // namespace lvgl_ui

#endif
