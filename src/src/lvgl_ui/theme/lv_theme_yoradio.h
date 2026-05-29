/**
 * YoRadio LVGL theme — presets, semantic palette (Theme Bible v1.2).
 * Тема LVGL YoRadio — пресеты и семантическая палитра (Theme Bible v1.2).
 *
 * Source of truth for LVGL colors lives here, not in config.theme / mytheme.h.
 * Источник цветов LVGL здесь, не в legacy theme.
 *
 * Stage 6.6R: Dark/Light = factory const; Custom = runtime file-backed (theme_custom.txt).
 * theme_dark is metadata in that file, NOT a struct field. Boot/Wi-Fi use fixed/service palettes.
 * Этап 6.6R: Custom — runtime из файла; theme_dark — metadata, не поле палитры.
 */

#ifndef LV_THEME_YORADIO_H
#define LV_THEME_YORADIO_H

#include <stdint.h>
#include "lvgl.h"

namespace lvgl_ui {

/// Named presets / Именованные пресеты (Custom may load /data/theme_custom.txt at runtime).
enum class ThemePreset : uint8_t {
    Dark = 0,
    Light = 1,
    Custom = 2,
};

/**
 * Semantic colors aligned with docs/YoRadio_LVGL_Theme_Bible.txt §4.1–§4.6 (+ §4.7 Main chrome, 6.6R-GA).
 * Dark/Light: const factory tables (Light = Cloud Ivory / Warm Cloudscape, 6.6R-GB).
 * Custom: kPaletteCustomBuiltin + optional /data/theme_custom.txt overrides.
 * boot_* fields exist for parser/fallback; Boot screen uses fixed dark (scr_boot.cpp), not pal.
 * theme_dark is metadata (NOT a palette field). Main-chrome geometry/opacity stay local in scr_main.cpp.
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
    lv_color_t status_weather_icon;  // status-line weather glyph (glance), not bottom bar
    lv_color_t status_weather_temp;  // status-line weather °C (glance), not bottom bar
    lv_color_t status_line_bg;
    lv_color_t clock_text;
    lv_color_t live_indicator_text;
    lv_color_t station_name_text;
    lv_color_t artist_text;
    lv_color_t track_text;
    lv_color_t meta_row_text;
    lv_color_t volume_bar_track;
    lv_color_t volume_bar_fill;
    lv_color_t buffer_meter_fill;  // lower divider buffer meter fill (not accent_soft / not volume fill)
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

    // §4.6 Boot (parsed in custom file; runtime Boot screen is fixed dark — see scr_boot.cpp)
    lv_color_t boot_background;
    lv_color_t boot_status_text;
    lv_color_t boot_progress_track;
    lv_color_t boot_progress_fill;

    // §4.7 Main chrome (Stage 6.6R-GA) — scr_main.cpp transport shelf / rim glow / pressed state.
    // Colors only; geometry/opacity/radius stay local in scr_main.cpp (not theme tokens).
    // glow_edge reuses main_chrome_bg; art frame reuses main_chrome_border.
    // §4.7 Main chrome — локальный хром главного экрана: полка/рамка/блик/pressed. Только цвета.
    lv_color_t main_chrome_bg;          // control band body + rim glow edge / тело полки + край glow
    lv_color_t main_chrome_border;      // control band border + art slot frame / рамка полки и арта
    lv_color_t main_chrome_glow_top;    // rim glow top peak / верхний пик блика
    lv_color_t main_chrome_glow_bottom; // rim glow bottom peak / нижний пик блика
    lv_color_t main_chrome_pressed_bg;  // control icon pressed state bg / фон pressed кнопок
};

// Stage 6.6R-F1: last parse of /data/theme_custom.txt (for WebUI /bg_status).
// Этап 6.6R-F1: статистика последнего разбора custom theme file.
struct ThemeCustomParseStats {
    uint16_t applied_keys   = 0;
    uint16_t invalid_lines  = 0;
    uint16_t unknown_keys   = 0;
    bool     file_exists    = false;
    uint32_t file_size      = 0;
};

ThemePreset yoradio_theme_active_preset();

/// Switch active preset (persisted in /data/theme.dat from WebUI). / Смена пресета.
void yoradio_theme_set_preset(ThemePreset p);

/// Active palette for the current preset. / Активная палитра для текущего пресета.
const YoRadioPalette& yoradio_palette();

/**
 * LVGL default-theme dark flag per preset (lv_theme_default_init).
 * Custom: from theme_dark metadata in theme_custom.txt (default true).
 * Тёмный режим LVGL: Custom — metadata theme_dark (по умолчанию true).
 */
bool yoradio_theme_is_dark(ThemePreset preset);

/** Service/recovery UI palette — factory Dark, not user Custom. / Палитра сервисных экранов. */
const YoRadioPalette& yoradio_palette_service();

/**
 * Initialize default LVGL theme + YoRadio palette binding. Idempotent.
 * Call only after successful lv_disp_drv_register (valid default display).
 * Инициализация базовой темы LVGL; только после успешной регистрации дисплея.
 */
void yoradio_theme_init(lv_disp_t* disp);

/**
 * Re-apply LVGL default theme with the current active preset's accent/dark flag.
 * Safe to call repeatedly — vendored lv_theme_default_init() reuses existing allocation,
 * resets all styles, and auto-propagates via lv_obj_report_style_change(NULL).
 * Call only from DspTask; does not touch s_theme_inited guard.
 *
 * Повторная привязка базовой темы LVGL к текущему пресету — safe, без утечек.
 * Только из DspTask. Не трогает guard первой инициализации.
 */
void yoradio_theme_reinit(lv_disp_t* disp);

// Stage 6.6R-E: LittleFS preset persistence (/data/theme.dat) — not config_t / NVS.
// Этап 6.6R-E: сохранение пресета в LittleFS, не config_t / NVS.
bool yoradio_theme_load_persisted_preset(ThemePreset* out);
bool yoradio_theme_save_persisted_preset(ThemePreset preset);

// Stage 6.6R-F1: /data/theme_custom.txt — Custom preset color overrides (not theme.dat).
// Этап 6.6R-F1: переопределения цветов Custom; выбор пресета — отдельно в theme.dat.
void yoradio_theme_reset_custom_palette();
bool yoradio_theme_load_custom_palette_file(ThemeCustomParseStats* out);
bool yoradio_theme_custom_file_exists();
const ThemeCustomParseStats& yoradio_theme_custom_parse_stats();

} // namespace lvgl_ui

#endif
