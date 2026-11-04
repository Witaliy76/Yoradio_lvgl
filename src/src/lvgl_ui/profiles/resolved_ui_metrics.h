// Resolved UI metrics: selected factory defaults plus the Display Contract viewport.
// Разрешённые UI-метрики: выбранные заводские defaults и viewport Display Contract.
#ifndef YORADIO_LVGL_RESOLVED_UI_METRICS_H
#define YORADIO_LVGL_RESOLVED_UI_METRICS_H

#include <stdint.h>

// Board selection must enter through core/options.h: it loads myoptions.h
// before board_select.h and deliberately avoids the historical back-include.
// Выбор платы идёт через core/options.h: он загружает myoptions.h до
// board_select.h и не возвращает исторический обратный include.
#include "../../core/options.h"
#include "../../platform/display/resolved_display_config.h"
#include "ui_profile_square480.h"

namespace lvgl_ui {

struct ResolvedUiMetrics {
    yoradio::UiProfile profile;
    const UiFactoryMetrics& factory;
    uint16_t viewport_width;
    uint16_t viewport_height;
};

constexpr bool uiProfileSupportsViewport(yoradio::UiProfile profile,
                                         uint16_t viewport_width,
                                         uint16_t viewport_height) {
    switch (profile) {
        case yoradio::UiProfile::Square480:
            return viewport_width == 480u && viewport_height == 480u;
    }
    return false;
}

constexpr const UiFactoryMetrics& factoryMetricsFor(yoradio::UiProfile profile) {
    switch (profile) {
        case yoradio::UiProfile::Square480:
            return kUiFactoryMetricsSquare480;
    }
    return kUiFactoryMetricsSquare480;
}

constexpr ResolvedUiMetrics resolveUiMetrics(yoradio::UiProfile profile,
                                             uint16_t viewport_width,
                                             uint16_t viewport_height) {
    return ResolvedUiMetrics{
        .profile = profile,
        .factory = factoryMetricsFor(profile),
        .viewport_width = viewport_width,
        .viewport_height = viewport_height,
    };
}

// The viewport is sourced from the active Display Contract. Factory metrics do
// not store a panel size and no resolution-based profile selection occurs here.
// Viewport берётся из активного Display Contract. Заводские метрики не хранят
// размер панели и выбор профиля по разрешению здесь отсутствует.
inline constexpr ResolvedUiMetrics kResolvedUiMetrics = resolveUiMetrics(
    yoradio::kActiveBoardDefaults.ui.profile,
    yoradio::kResolvedDisplayConfig.panel.native_width,
    yoradio::kResolvedDisplayConfig.panel.native_height);

static_assert(kResolvedUiMetrics.profile == yoradio::UiProfile::Square480,
              "ResolvedUiMetrics: 4848S040 must explicitly select UiProfile::Square480.");
static_assert(uiProfileSupportsViewport(kResolvedUiMetrics.profile,
                                        kResolvedUiMetrics.viewport_width,
                                        kResolvedUiMetrics.viewport_height),
              "ResolvedUiMetrics: selected UI profile is incompatible with the Display Contract viewport.");
static_assert(kResolvedUiMetrics.factory.font_small_px > 0u &&
                  kResolvedUiMetrics.factory.font_normal_px > 0u &&
                  kResolvedUiMetrics.factory.font_large_px > 0u &&
                  kResolvedUiMetrics.factory.font_header_px > 0u &&
                  kResolvedUiMetrics.factory.main_station_font_px > 0u &&
                  kResolvedUiMetrics.factory.main_track_font_px > 0u &&
                  kResolvedUiMetrics.factory.main_artist_font_px > 0u,
              "ResolvedUiMetrics: requested font sizes must be positive.");
static_assert(2u * kResolvedUiMetrics.factory.frame_padding < kResolvedUiMetrics.viewport_width &&
                  2u * kResolvedUiMetrics.factory.frame_padding < kResolvedUiMetrics.viewport_height,
              "ResolvedUiMetrics: frame padding must leave a nonempty viewport.");
// Main currently reserves a 2 px art border on every side.
static_assert(kResolvedUiMetrics.factory.main_station_art_slot_width > 4u &&
                  kResolvedUiMetrics.factory.main_station_art_slot_height > 4u,
              "ResolvedUiMetrics: the Main art slot must leave room inside its border.");

}  // namespace lvgl_ui

#endif  // YORADIO_LVGL_RESOLVED_UI_METRICS_H
