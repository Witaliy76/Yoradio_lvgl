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
    lv_color_hex(0xFFFFFF), // status_line_text
    lv_color_hex(0xAAAAAA), // status_line_meta
    lv_color_hex(0x101010), // status_line_bg
    lv_color_hex(0xFFFFFF), // clock_text
    lv_color_hex(0xFF5555), // live_indicator_text
    lv_color_hex(0xFFFFFF), // station_name_text
    lv_color_hex(0xCCCCCC), // artist_text
    lv_color_hex(0xCCCCCC), // track_text
    lv_color_hex(0xAAAAAA), // meta_row_text
    lv_color_hex(0x282828), // volume_bar_track
    lv_color_hex(0xE7D32A), // volume_bar_fill
    lv_color_hex(0xCCCCCC), // bottom_weather_text
    lv_color_hex(0xCCCCCC), // bottom_ai_text
    lv_color_hex(0xEEEEEE), // list_row_text
    lv_color_hex(0x3A3A3A), // list_row_selected_bg
    lv_color_hex(0xFFFFFF), // list_row_selected_text
    lv_color_hex(0x444444), // list_row_separator
    lv_color_hex(0x2C2C2C), // overlay_card_bg
    lv_color_hex(0xFFFFFF), // overlay_title_text
    lv_color_hex(0xCCCCCC), // overlay_body_text
    lv_color_hex(0x000000), // screensaver_background
    lv_color_hex(0xFFFFFF), // screensaver_clock_text
    lv_color_hex(0x000000), // boot_background
    lv_color_hex(0xCCCCCC), // boot_status_text
    lv_color_hex(0x333333), // boot_progress_track
    lv_color_hex(0xE7D32A), // boot_progress_fill
};

static const YoRadioPalette kPaletteLight = {
    lv_color_hex(0xF0F0F0),
    lv_color_hex(0xFFFFFF),
    lv_color_hex(0xCCCCCC),
    lv_color_hex(0x1A1A1A),
    lv_color_hex(0x666666),
    lv_color_hex(0x777777),
    lv_color_hex(0xB8860B),
    lv_color_hex(0xD4A84B),
    lv_color_hex(0xCCCCCC),
    lv_color_hex(0xC0C0C0),
    lv_color_hex(0x333333),
    lv_color_hex(0x555555),
    lv_color_hex(0xE8E8E8),
    lv_color_hex(0x222222),
    lv_color_hex(0xCC0000),
    lv_color_hex(0x202020),
    lv_color_hex(0x444444),
    lv_color_hex(0x444444),
    lv_color_hex(0x555555),
    lv_color_hex(0xD8D8D8),
    lv_color_hex(0xB8860B),
    lv_color_hex(0x444444),
    lv_color_hex(0x444444),
    lv_color_hex(0x222222),
    lv_color_hex(0xD0D0D0),
    lv_color_hex(0x000000),
    lv_color_hex(0xCCCCCC),
    lv_color_hex(0xFFFFFF),
    lv_color_hex(0x1A1A1A),
    lv_color_hex(0x444444),
    lv_color_hex(0x101010),
    lv_color_hex(0xF5F5F5),
    lv_color_hex(0xF5F5F5),
    lv_color_hex(0x444444),
    lv_color_hex(0xCCCCCC),
    lv_color_hex(0xB8860B),
};

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
