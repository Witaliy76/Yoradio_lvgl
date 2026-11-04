// S06-1 host proof: resolved_ui_metrics may be the first project header.
// It must enter the Board Contract through core/options.h itself.

#include "../../src/src/lvgl_ui/profiles/resolved_ui_metrics.h"

static_assert(yoradio::kActiveBoardDefaults.ui.profile == yoradio::UiProfile::Square480);
static_assert(lvgl_ui::kResolvedUiMetrics.viewport_width == 480u);
static_assert(lvgl_ui::kResolvedUiMetrics.viewport_height == 480u);
// S06 migration parity baseline: intentional factory changes require review and
// an explicit update here, not a change to the production resolver invariants.
static_assert(lvgl_ui::kResolvedUiMetrics.factory.frame_padding == 8u);
static_assert(lvgl_ui::kResolvedUiMetrics.factory.font_small_px == 12u);
static_assert(lvgl_ui::kResolvedUiMetrics.factory.font_normal_px == 16u);
static_assert(lvgl_ui::kResolvedUiMetrics.factory.font_large_px == 22u);
static_assert(lvgl_ui::kResolvedUiMetrics.factory.font_header_px == 14u);
static_assert(lvgl_ui::kResolvedUiMetrics.factory.main_station_font_px == 32u);
static_assert(lvgl_ui::kResolvedUiMetrics.factory.main_track_font_px == 22u);
static_assert(lvgl_ui::kResolvedUiMetrics.factory.main_artist_font_px == 18u);
static_assert(lvgl_ui::kResolvedUiMetrics.factory.main_station_art_slot_width == 120u);
static_assert(lvgl_ui::kResolvedUiMetrics.factory.main_station_art_slot_height == 120u);
static_assert(lvgl_ui::kResolvedUiMetrics.factory.main_station_art_gap == 12u);
static_assert(lvgl_ui::uiProfileSupportsViewport(yoradio::UiProfile::Square480, 480u, 480u));
static_assert(!lvgl_ui::uiProfileSupportsViewport(yoradio::UiProfile::Square480, 720u, 720u));
static_assert(!lvgl_ui::uiProfileSupportsViewport(yoradio::UiProfile::Square480, 800u, 480u));
static_assert(!lvgl_ui::uiProfileSupportsViewport(static_cast<yoradio::UiProfile>(255), 480u, 480u));
static_assert(lvgl_ui::kResolvedUiMetrics.viewport_width ==
              yoradio::kResolvedDisplayConfig.panel.native_width);
static_assert(lvgl_ui::kResolvedUiMetrics.viewport_height ==
              yoradio::kResolvedDisplayConfig.panel.native_height);

int main() { return 0; }
