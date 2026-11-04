// Factory Square480 UI metrics, kept independent from panel and board descriptors.
// Заводские Square480 UI-метрики, независимые от дескрипторов панели и платы.
#ifndef YORADIO_LVGL_UI_PROFILE_SQUARE480_H
#define YORADIO_LVGL_UI_PROFILE_SQUARE480_H

#include "ui_profile_types.h"

namespace lvgl_ui {

// Factory UI defaults. Screen-local composition, hit geometry and VisualPack
// coordinates intentionally stay local.
// Заводские UI-значения. Локальная компоновка, hit-геометрия и координаты
// VisualPack намеренно остаются в экранах/pack.
inline constexpr UiFactoryMetrics kUiFactoryMetricsSquare480{
    .frame_padding = 8u,
    .font_small_px = 12u,
    .font_normal_px = 16u,
    .font_large_px = 22u,
    .font_header_px = 14u,
    .main_station_font_px = 32u,
    .main_track_font_px = 22u,
    .main_artist_font_px = 18u,
    .main_station_art_slot_width = 120u,
    .main_station_art_slot_height = 120u,
    .main_station_art_gap = 12u,
};

}  // namespace lvgl_ui

#endif  // YORADIO_LVGL_UI_PROFILE_SQUARE480_H
