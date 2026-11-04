// Factory UI metrics: geometry and typography only, no panel/touch/buffer facts.
// Заводские UI-метрики: только геометрия и типографика, без фактов панели/тача/буфера.
#ifndef YORADIO_LVGL_UI_PROFILE_TYPES_H
#define YORADIO_LVGL_UI_PROFILE_TYPES_H

#include <stdint.h>

namespace lvgl_ui {

struct UiFactoryMetrics {
    uint16_t frame_padding;
    uint16_t font_small_px;
    uint16_t font_normal_px;
    uint16_t font_large_px;
    uint16_t font_header_px;
    uint16_t main_station_font_px;
    uint16_t main_track_font_px;
    uint16_t main_artist_font_px;
    uint16_t main_station_art_slot_width;
    uint16_t main_station_art_slot_height;
    uint16_t main_station_art_gap;
};

}  // namespace lvgl_ui

#endif  // YORADIO_LVGL_UI_PROFILE_TYPES_H
