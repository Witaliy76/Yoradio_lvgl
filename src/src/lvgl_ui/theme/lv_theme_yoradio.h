/**
 * YoRadio LVGL theme — presets, semantic palette (Theme Bible v1.2).
 * Тема LVGL YoRadio — пресеты и семантическая палитра (Theme Bible v1.2).
 *
 * Source of truth for LVGL colors lives here, not in config.theme / mytheme.h.
 * Источник цветов LVGL здесь, не в legacy theme.
 *
 * Stage 6.6A: first slice — full struct compatible with Bible token names; screens
 * use a subset. Base LVGL theme is applied once after display registration.
 */

#ifndef LV_THEME_YORADIO_H
#define LV_THEME_YORADIO_H

#include "lvgl.h"

namespace lvgl_ui {

/// Named presets / Именованные пресеты (Custom = slot for future persistence).
enum class ThemePreset : uint8_t {
    Dark = 0,
    Light = 1,
    Custom = 2,
};

/**
 * Semantic colors aligned with docs/YoRadio_LVGL_Theme_Bible.txt §4.1–§4.6.
 * Values are filled per preset in lv_theme_yoradio.cpp (Dark / Light / Custom).
 */
struct YoRadioPalette {
    // §4.1 Foundation
    lv_color_t device_background;
    lv_color_t panel_background;
    lv_color_t panel_border;
    lv_color_t text_primary;
    lv_color_t text_secondary;
    lv_color_t text_meta;
    lv_color_t accent;
    lv_color_t accent_soft;
    lv_color_t divider;
    lv_color_t overlay_scrim;

    // §4.2 Main / player pages
    lv_color_t status_line_text;
    lv_color_t status_line_meta;
    lv_color_t status_line_bg;
    lv_color_t clock_text;
    lv_color_t live_indicator_text;
    lv_color_t station_name_text;
    lv_color_t artist_text;
    lv_color_t track_text;
    lv_color_t meta_row_text;
    lv_color_t volume_bar_track;
    lv_color_t volume_bar_fill;
    lv_color_t bottom_weather_text;
    lv_color_t bottom_ai_text;

    // §4.3 Lists
    lv_color_t list_row_text;
    lv_color_t list_row_selected_bg;
    lv_color_t list_row_selected_text;
    lv_color_t list_row_separator;

    // §4.4 Overlay
    lv_color_t overlay_card_bg;
    lv_color_t overlay_title_text;
    lv_color_t overlay_body_text;

    // §4.5 Screensaver
    lv_color_t screensaver_background;
    lv_color_t screensaver_clock_text;

    // §4.6 Boot (minimal integration in later sub-stages)
    lv_color_t boot_background;
    lv_color_t boot_status_text;
    lv_color_t boot_progress_track;
    lv_color_t boot_progress_fill;
};

ThemePreset yoradio_theme_active_preset();

/// Switch active preset (Custom may mirror Dark until storage exists). / Смена пресета.
void yoradio_theme_set_preset(ThemePreset p);

/// Active palette for the current preset. / Активная палитра для текущего пресета.
const YoRadioPalette& yoradio_palette();

/**
 * Initialize default LVGL theme + YoRadio palette binding. Idempotent.
 * Call only after successful lv_disp_drv_register (valid default display).
 * Инициализация базовой темы LVGL; только после успешной регистрации дисплея.
 */
void yoradio_theme_init(lv_disp_t* disp);

} // namespace lvgl_ui

#endif
