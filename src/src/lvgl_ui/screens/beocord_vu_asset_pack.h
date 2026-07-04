#ifndef BEOCORD_VU_ASSET_PACK_H
#define BEOCORD_VU_ASSET_PACK_H

#include <cstdint>

#include "../../core/options.h"

// E1: compile-time Beocord museum asset boundary — path + design canvas only (no segment rects in E1).
// E1: граница target-pack Beocord — только путь и размер дизайна (без координат сегментов в E1).

namespace lvgl_ui {

struct BeocordVuAssetPack {
    const char* background_path; // LittleFS runtime path / путь в LittleFS
    uint16_t    design_width;
    uint16_t    design_height;
};

#if DSP_MODEL == DSP_ST7701
// Sunton 4848S040 — 480×480 museum background (E1 static proof).
// Sunton 4848S040 — статичный музейный фон 480×480 (доказательство E1).
static constexpr BeocordVuAssetPack kBeocordVuAssetPack{
    "/visual/beocord9000/480x480/base_off_scale_on.bin",
    480u,
    480u,
};
#else
// Other boards: no museum asset in E1 — Visual falls back to theme device_background.
// Другие платы: без музейного ассета в E1 — fallback на device_background темы.
static constexpr BeocordVuAssetPack kBeocordVuAssetPack{
    nullptr,
    0u,
    0u,
};
#endif

} // namespace lvgl_ui

#endif
