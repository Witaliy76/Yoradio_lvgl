/**
 * YoRadio LVGL theme — preset tables and lv_theme_default_init bridge.
 * Пресеты и привязка к lv_theme_default_init.
 */

#include "lv_theme_yoradio.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lvgl.h"

namespace lvgl_ui {

namespace {

static ThemePreset s_preset = ThemePreset::Dark;

// Dark preset — matches pre–6.6A scr_main / scr_stub appearance (RGB preserved semantically).
// Пресет Dark — соответствует прежним hardcoded RGB главного экрана и стабов.
static const YoRadioPalette kPaletteDark = {
    // §4.1 Foundation / YoRadioPalette order in lv_theme_yoradio.h
    lv_color_hex(0x000000), // device_background
    lv_color_hex(0x121212), // panel_background
    lv_color_hex(0x333333), // panel_border
    lv_color_hex(0xFFFFFF), // text_primary
    lv_color_hex(0x808080), // text_secondary
    lv_color_hex(0x888888), // text_meta
    lv_color_hex(0xE7D32A), // accent
    lv_color_hex(0x5C5010), // accent_soft
    lv_color_hex(0x444444), // divider
    lv_color_hex(0x202020), // overlay_scrim
    // §4.2 Main / player (volume/heap colors refined 6.1D-a2)
    lv_color_hex(0xFFFFFF), // status_line_text
    lv_color_hex(0xAAAAAA), // status_line_meta
    lv_color_hex(0xAEB4BC), // status_weather_icon
    lv_color_hex(0xC4CAD2), // status_weather_temp
    lv_color_hex(0x101010), // status_line_bg
    lv_color_hex(0xFFFFFF), // clock_text
    lv_color_hex(0xFF5555), // live_indicator_text
    lv_color_hex(0xFFFFFF), // station_name_text
    lv_color_hex(0xCCCCCC), // artist_text
    lv_color_hex(0xCCCCCC), // track_text
    lv_color_hex(0xAAAAAA), // meta_row_text
    lv_color_hex(0x151515), // volume_bar_track — recessed graphite (6.1D-a2)
    lv_color_hex(0x5A6572), // volume_bar_fill — muted slate, not station yellow (6.1D-a2)
    lv_color_hex(0xCCCCCC), // buffer_meter_fill — matches bottom_ai_text: readable on 1px vs divider (6.1D-a2)
    lv_color_hex(0xCCCCCC), // bottom_weather_text
    lv_color_hex(0xCCCCCC), // bottom_ai_text
    // §4.3 Lists
    lv_color_hex(0xEEEEEE), // list_row_text
    lv_color_hex(0x3A3A3A), // list_row_selected_bg
    lv_color_hex(0xFFFFFF), // list_row_selected_text
    lv_color_hex(0x444444), // list_row_separator
    // §4.4 Overlay
    lv_color_hex(0x2C2C2C), // overlay_card_bg
    lv_color_hex(0xFFFFFF), // overlay_title_text
    lv_color_hex(0xCCCCCC), // overlay_body_text
    // §4.5 Screensaver
    lv_color_hex(0x000000), // screensaver_background
    lv_color_hex(0xFFFFFF), // screensaver_clock_text
    // §4.6 Boot
    lv_color_hex(0x000000), // boot_background
    lv_color_hex(0xCCCCCC), // boot_status_text
    lv_color_hex(0x333333), // boot_progress_track
    lv_color_hex(0xE7D32A), // boot_progress_fill
};

// Light preset — same field order as kPaletteDark / YoRadioPalette.
// Пресет Light — тот же порядок полей, что у Dark.
static const YoRadioPalette kPaletteLight = {
    // §4.1 Foundation
    lv_color_hex(0xF0F0F0), // device_background
    lv_color_hex(0xFFFFFF), // panel_background
    lv_color_hex(0xCCCCCC), // panel_border
    lv_color_hex(0x1A1A1A), // text_primary
    lv_color_hex(0x666666), // text_secondary
    lv_color_hex(0x777777), // text_meta
    lv_color_hex(0xB8860B), // accent
    lv_color_hex(0xD4A84B), // accent_soft
    lv_color_hex(0xCCCCCC), // divider
    lv_color_hex(0xC0C0C0), // overlay_scrim
    // §4.2 Main / player
    lv_color_hex(0x333333), // status_line_text
    lv_color_hex(0x555555), // status_line_meta
    lv_color_hex(0x8F98A3), // status_weather_icon (quieter than temp on light)
    lv_color_hex(0x4E5A66), // status_weather_temp (more readable on light)
    lv_color_hex(0xE8E8E8), // status_line_bg
    lv_color_hex(0x222222), // clock_text
    lv_color_hex(0xCC0000), // live_indicator_text
    lv_color_hex(0x202020), // station_name_text
    lv_color_hex(0x444444), // artist_text
    lv_color_hex(0x444444), // track_text
    lv_color_hex(0x555555), // meta_row_text
    lv_color_hex(0xC4C4C4), // volume_bar_track — light recessed track (6.1D-a2)
    lv_color_hex(0x6B7580), // volume_bar_fill — muted slate (6.1D-a2)
    lv_color_hex(0x444444), // buffer_meter_fill — matches bottom_ai_text: readable on 1px vs divider (6.1D-a2)
    lv_color_hex(0x444444), // bottom_weather_text
    lv_color_hex(0x444444), // bottom_ai_text
    // §4.3 Lists
    lv_color_hex(0x222222), // list_row_text
    lv_color_hex(0xD0D0D0), // list_row_selected_bg
    lv_color_hex(0x000000), // list_row_selected_text
    lv_color_hex(0xCCCCCC), // list_row_separator
    // §4.4 Overlay
    lv_color_hex(0xFFFFFF), // overlay_card_bg
    lv_color_hex(0x1A1A1A), // overlay_title_text
    lv_color_hex(0x444444), // overlay_body_text
    // §4.5 Screensaver
    lv_color_hex(0x101010), // screensaver_background
    lv_color_hex(0xF5F5F5), // screensaver_clock_text
    // §4.6 Boot
    lv_color_hex(0xF5F5F5), // boot_background
    lv_color_hex(0x444444), // boot_status_text
    lv_color_hex(0xCCCCCC), // boot_progress_track
    lv_color_hex(0xB8860B), // boot_progress_fill
};

// Custom — copy of Dark until persisted user palette exists.
// Custom — копия Dark, пока нет сохранённой пользовательской палитры.
static const YoRadioPalette kPaletteCustom = kPaletteDark;

static bool s_theme_inited = false;

static const YoRadioPalette& palette_for_preset(ThemePreset p) {
    switch (p) {
        case ThemePreset::Light:
            return kPaletteLight;
        case ThemePreset::Custom:
            return kPaletteCustom;
        case ThemePreset::Dark:
        default:
            return kPaletteDark;
    }
}

static bool theme_is_dark(ThemePreset p) {
    return (p == ThemePreset::Dark || p == ThemePreset::Custom);
}

} // namespace

ThemePreset yoradio_theme_active_preset() {
    return s_preset;
}

void yoradio_theme_set_preset(ThemePreset p) {
    s_preset = p;
}

const YoRadioPalette& yoradio_palette() {
    return palette_for_preset(s_preset);
}

void yoradio_theme_init(lv_disp_t* disp) {
    if (s_theme_inited || !disp) {
        return;
    }

    const YoRadioPalette& p = yoradio_palette();

    lv_theme_t* th =
        lv_theme_default_init(disp, p.accent, p.accent_soft, theme_is_dark(s_preset), LV_FONT_DEFAULT);

    if (th) {
        lv_disp_set_theme(disp, th);
    }

    s_theme_inited = true;
}

} // namespace lvgl_ui

#endif // YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)
