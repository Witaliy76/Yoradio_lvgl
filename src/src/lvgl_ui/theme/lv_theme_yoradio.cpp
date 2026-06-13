/**
 * YoRadio LVGL theme — factory Dark/Light, runtime Custom palette, lv_theme_default_init.
 * Пресеты: Dark/Light const; Custom = kPaletteCustomBuiltin + /data/theme_custom.txt.
 * theme_dark metadata (not YoRadioPalette field) controls LVGL dark flag for Custom.
 * See THEME.md for theme.dat vs theme_custom.txt, Boot/Wi-Fi boundaries.
 */

#include "lv_theme_yoradio.h"


#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cctype>

#include <FS.h>
#include <LittleFS.h>
#include "lvgl.h"
#include "../../core/config.h"

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
    // S6V11-theme-A: calm cold accent / focus (was station yellow+olive) / спокойный холодный акцент
    lv_color_hex(0x5B94C9), // accent
    lv_color_hex(0x243A4D), // accent_soft — soft primary fill on dark / мягкая заливка на тёмном
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
    // S6V11A-themeA1: boot bar keeps heritage gold / прогресс бут — отдельно от accent
    lv_color_hex(0xE7D32A), // boot_progress_fill
    // §4.7 Main chrome (6.6R-GA) — current Dark hardcoded values moved into tokens.
    lv_color_hex(0x252D38), // main_chrome_bg
    lv_color_hex(0x6B7D8F), // main_chrome_border
    lv_color_hex(0xC6EBFF), // main_chrome_glow_top
    lv_color_hex(0xA2CCE0), // main_chrome_glow_bottom
    lv_color_hex(0xFFFFFF), // main_chrome_pressed_bg
    // §4.8 Presence Rail (E22M) — icy cyan-blue in family with accent/glow, not warm copper.
    lv_color_hex(0x5AA7E8), // rail_accent — cold cosmic rail / холодный «космический» цвет линий
    100,                    // rail_opa_scale — dark bg tolerates full opacity / тёмный фон терпит 100%
};

// Light preset — same field order as kPaletteDark / YoRadioPalette.
// Stage 6.6R-GB: Cloud Ivory / Warm Cloudscape — warm ivory/beige + graphite text + restrained amber.
// Пресет Light — Cloud Ivory: тёплый ivory/беж, графитовый текст, сдержанный амбер (6.6R-GB).
static const YoRadioPalette kPaletteLight = {
    // §4.1 Foundation
    lv_color_hex(0xF8EFE3), // device_background
    lv_color_hex(0xF9EFE2), // panel_background
    lv_color_hex(0xD3C4B3), // panel_border
    lv_color_hex(0x2F2926), // text_primary
    lv_color_hex(0x6F6459), // text_secondary
    lv_color_hex(0x8A7D70), // text_meta
    lv_color_hex(0xC8942E), // accent
    lv_color_hex(0xE0CBA0), // accent_soft — softened gold; raw amber too active for LVGL soft/checked states
    lv_color_hex(0xD8CABB), // divider
    lv_color_hex(0xEDE1D2), // overlay_scrim
    // §4.2 Main / player
    lv_color_hex(0x3A2C24), // status_line_text
    lv_color_hex(0x6F6459), // status_line_meta
    lv_color_hex(0xC8942E), // status_weather_icon
    lv_color_hex(0x2F2926), // status_weather_temp
    lv_color_hex(0xF4E8D9), // status_line_bg
    lv_color_hex(0x2F2926), // clock_text
    lv_color_hex(0xB65F3A), // live_indicator_text
    lv_color_hex(0x2F2926), // station_name_text
    lv_color_hex(0x6F6459), // artist_text
    lv_color_hex(0x3A2C24), // track_text
    lv_color_hex(0x7A6E62), // meta_row_text
    lv_color_hex(0xDCCBB7), // volume_bar_track
    lv_color_hex(0xE7AF58), // volume_bar_fill
    lv_color_hex(0x8A7D70), // buffer_meter_fill — quiet taupe; amber would over-emphasize technical buffer line
    lv_color_hex(0x6F6459), // bottom_weather_text
    lv_color_hex(0x6F6459), // bottom_ai_text
    // §4.3 Lists
    lv_color_hex(0x2F2926), // list_row_text
    lv_color_hex(0xEFE1CE), // list_row_selected_bg
    lv_color_hex(0x2F2926), // list_row_selected_text
    lv_color_hex(0xD8CABB), // list_row_separator
    // §4.4 Overlay
    lv_color_hex(0xF9EFE2), // overlay_card_bg
    lv_color_hex(0x2F2926), // overlay_title_text
    lv_color_hex(0x6F6459), // overlay_body_text
    // §4.5 Screensaver — intentionally dark on Light (night/device behavior); unchanged in GB.
    lv_color_hex(0x101010), // screensaver_background
    lv_color_hex(0xF5F5F5), // screensaver_clock_text
    // §4.6 Boot — parser/fallback only; runtime Boot screen is fixed dark (scr_boot.cpp).
    lv_color_hex(0xF8EFE3), // boot_background
    lv_color_hex(0x6F6459), // boot_status_text
    lv_color_hex(0xDCCBB7), // boot_progress_track
    lv_color_hex(0xC8942E), // boot_progress_fill
    // §4.7 Main chrome (6.6R-GB) — Cloud Ivory shelf/glow/pressed: warm ivory + cream glow.
    lv_color_hex(0xF9EFE2), // main_chrome_bg
    lv_color_hex(0xD3C4B3), // main_chrome_border
    lv_color_hex(0xFEF6E6), // main_chrome_glow_top
    lv_color_hex(0xF4E3CC), // main_chrome_glow_bottom
    lv_color_hex(0xD8C3A2), // main_chrome_pressed_bg — 6.6R-GB1: darker warm beige; #EFE1CE too close to shelf to read
    // §4.8 Presence Rail (E22M) — ink/slate-blue, not black: line reads as colored ink on ivory.
    // E22M1: softer cloud blue-gray — E22M #4C6476 read too dark / almost black on ivory.
    lv_color_hex(0x7A8F9A), // rail_accent — мягкий cloud blue-gray, не чёрный/грязный
    60,                     // rail_opa_scale — ниже 70, меньше «царапины» на светлом фоне
};

// Built-in Custom fallback — Amber Hi-Fi / Tube Amp (matches theme_custom.example.txt + data seed).
// When theme_custom.txt is missing/removed/partial: baseline before file overrides (not Dark/Light).
// Встроенный fallback Custom = Amber Hi-Fi; при файле парсер перезаписывает поля поверх этого baseline.
static const YoRadioPalette kPaletteCustomBuiltin = {
    // §4.1 Foundation — Amber Hi-Fi / Tube Amp
    lv_color_hex(0x0D0806), // device_background
    lv_color_hex(0x1A120D), // panel_background
    lv_color_hex(0x5A422B), // panel_border
    lv_color_hex(0xF4E7D2), // text_primary
    lv_color_hex(0xB8A48C), // text_secondary
    lv_color_hex(0x8C7A66), // text_meta
    lv_color_hex(0xD89A2B), // accent
    lv_color_hex(0x4A2F17), // accent_soft
    lv_color_hex(0x3A2A1F), // divider
    lv_color_hex(0x130F0B), // overlay_scrim
    // §4.2 Main / player
    lv_color_hex(0xE8DCC7), // status_line_text
    lv_color_hex(0xA9937E), // status_line_meta
    lv_color_hex(0xD89A2B), // status_weather_icon
    lv_color_hex(0xF4E7D2), // status_weather_temp
    lv_color_hex(0x17100C), // status_line_bg
    lv_color_hex(0xF4E7D2), // clock_text
    lv_color_hex(0xD66A3A), // live_indicator_text
    lv_color_hex(0xF4E7D2), // station_name_text
    lv_color_hex(0xB8A48C), // artist_text
    lv_color_hex(0xF0D1A2), // track_text
    lv_color_hex(0xB47A3A), // meta_row_text
    lv_color_hex(0x3A2A1F), // volume_bar_track
    lv_color_hex(0xD89A2B), // volume_bar_fill
    lv_color_hex(0xB86E1E), // buffer_meter_fill
    lv_color_hex(0xCBB89F), // bottom_weather_text
    lv_color_hex(0xCBB89F), // bottom_ai_text
    // §4.3 Lists
    lv_color_hex(0xF0E4D0), // list_row_text
    lv_color_hex(0x4A2F17), // list_row_selected_bg
    lv_color_hex(0xF6EAD5), // list_row_selected_text
    lv_color_hex(0x3A2A1F), // list_row_separator
    // §4.4 Overlay
    lv_color_hex(0x20160F), // overlay_card_bg
    lv_color_hex(0xF4E7D2), // overlay_title_text
    lv_color_hex(0xB8A48C), // overlay_body_text
    // §4.5 Screensaver
    lv_color_hex(0x080705), // screensaver_background
    lv_color_hex(0xD89A2B), // screensaver_clock_text
    // §4.6 Boot — parser/fallback only; runtime Boot screen is fixed dark (scr_boot.cpp).
    lv_color_hex(0x0D0806), // boot_background
    lv_color_hex(0xCBB89F), // boot_status_text
    lv_color_hex(0x3A2A1F), // boot_progress_track
    lv_color_hex(0xD89A2B), // boot_progress_fill
    // §4.7 Main chrome — tube-amp shelf glow / pressed (pairs with main_custom.bin bg).
    lv_color_hex(0x1A120D), // main_chrome_bg
    lv_color_hex(0x8A5A24), // main_chrome_border
    lv_color_hex(0xF0C06A), // main_chrome_glow_top
    lv_color_hex(0xB86E1E), // main_chrome_glow_bottom
    lv_color_hex(0x3A2A1F), // main_chrome_pressed_bg
    // §4.8 Presence Rail (E22M) — burnt copper, matches the liked pre-E22M mix result.
    lv_color_hex(0xB06A24), // rail_accent — жжёная медь (бывший mix buffer_meter×chrome_border)
    90,                     // rail_opa_scale — slightly restrained on amber bg / чуть сдержаннее
};

static YoRadioPalette s_customPalette = kPaletteCustomBuiltin;
static ThemeCustomParseStats s_customStats{};
// Stage 6.6R-F2: Custom metadata — LVGL dark/light widget states (not a color key).
// Этап 6.6R-F2: metadata theme_dark для Custom (не поле YoRadioPalette).
static bool s_customThemeDark = true;

static bool s_theme_inited = false;

// Stage 6.6R-E: selected preset only (dark/light/custom).
// Этап 6.6R-E: только выбранный пресет.
static const char kThemePersistPath[] = "/data/theme.dat";

// Stage 6.6R-F1/F2: /data/theme_custom.txt — color key=#RRGGBB + metadata theme_dark (not in YoRadioPalette).
// Этап 6.6R: файл цветов Custom; theme_dark — metadata для lv_theme_default_init.
static const char kThemeCustomPath[] = "/data/theme_custom.txt";
static constexpr size_t kThemeCustomMaxBytes = 4096u;
static constexpr size_t kThemeCustomLineMax = 128u;

struct PaletteKeyMap {
    const char* key;
    size_t      offset;
};

#define YORA_PAL_KEY(name) { #name, offsetof(YoRadioPalette, name) }

static const PaletteKeyMap kPaletteKeyTable[] = {
    YORA_PAL_KEY(device_background),
    YORA_PAL_KEY(panel_background),
    YORA_PAL_KEY(panel_border),
    YORA_PAL_KEY(text_primary),
    YORA_PAL_KEY(text_secondary),
    YORA_PAL_KEY(text_meta),
    YORA_PAL_KEY(accent),
    YORA_PAL_KEY(accent_soft),
    YORA_PAL_KEY(divider),
    YORA_PAL_KEY(overlay_scrim),
    YORA_PAL_KEY(status_line_text),
    YORA_PAL_KEY(status_line_meta),
    YORA_PAL_KEY(status_weather_icon),
    YORA_PAL_KEY(status_weather_temp),
    YORA_PAL_KEY(status_line_bg),
    YORA_PAL_KEY(clock_text),
    YORA_PAL_KEY(live_indicator_text),
    YORA_PAL_KEY(station_name_text),
    YORA_PAL_KEY(artist_text),
    YORA_PAL_KEY(track_text),
    YORA_PAL_KEY(meta_row_text),
    YORA_PAL_KEY(volume_bar_track),
    YORA_PAL_KEY(volume_bar_fill),
    YORA_PAL_KEY(buffer_meter_fill),
    YORA_PAL_KEY(bottom_weather_text),
    YORA_PAL_KEY(bottom_ai_text),
    YORA_PAL_KEY(list_row_text),
    YORA_PAL_KEY(list_row_selected_bg),
    YORA_PAL_KEY(list_row_selected_text),
    YORA_PAL_KEY(list_row_separator),
    YORA_PAL_KEY(overlay_card_bg),
    YORA_PAL_KEY(overlay_title_text),
    YORA_PAL_KEY(overlay_body_text),
    YORA_PAL_KEY(screensaver_background),
    YORA_PAL_KEY(screensaver_clock_text),
    YORA_PAL_KEY(boot_background),
    YORA_PAL_KEY(boot_status_text),
    YORA_PAL_KEY(boot_progress_track),
    YORA_PAL_KEY(boot_progress_fill),
    // §4.7 Main chrome (6.6R-GA) — Custom files may override shelf/glow/pressed colors.
    YORA_PAL_KEY(main_chrome_bg),
    YORA_PAL_KEY(main_chrome_border),
    YORA_PAL_KEY(main_chrome_glow_top),
    YORA_PAL_KEY(main_chrome_glow_bottom),
    YORA_PAL_KEY(main_chrome_pressed_bg),
    // §4.8 Presence Rail (E22M) — rail_opa_scale is numeric, parsed separately (not in this table).
    YORA_PAL_KEY(rail_accent),
};

#undef YORA_PAL_KEY

static void copy_builtin_custom_palette() {
    s_customPalette = kPaletteCustomBuiltin;
    s_customThemeDark = true;
}

static bool parse_bool_metadata(const char* raw, bool* out) {
    if (!raw || !out) {
        return false;
    }
    char norm[12] = {};
    size_t i = 0u;
    for (; raw[i] && i < sizeof(norm) - 1u; ++i) {
        norm[i] = static_cast<char>(tolower(static_cast<unsigned char>(raw[i])));
    }
    if (strcmp(norm, "true") == 0 || strcmp(norm, "1") == 0 || strcmp(norm, "yes") == 0 ||
        strcmp(norm, "on") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(norm, "false") == 0 || strcmp(norm, "0") == 0 || strcmp(norm, "no") == 0 ||
        strcmp(norm, "off") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool str_ieq(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower(static_cast<unsigned char>(*a)) != tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool parse_hex_color(const char* raw, lv_color_t* out) {
    if (!raw || !out) return false;
    while (*raw == ' ' || *raw == '\t') {
        ++raw;
    }
    if (*raw == '#') {
        ++raw;
    }
    size_t n = 0;
    while (raw[n] &&
           isxdigit(static_cast<unsigned char>(raw[n])) &&
           n < 8u) {
        ++n;
    }
    if (n != 6u) {
        return false;
    }
    char hex[7] = {};
    memcpy(hex, raw, 6u);
    char* end = nullptr;
    const unsigned long v = strtoul(hex, &end, 16);
    if (!end || *end != '\0' || v > 0xFFFFFFul) {
        return false;
    }
    *out = lv_color_hex(static_cast<uint32_t>(v));
    return true;
}

static bool apply_palette_key(const char* key, lv_color_t color) {
    for (size_t i = 0; i < sizeof(kPaletteKeyTable) / sizeof(kPaletteKeyTable[0]); ++i) {
        if (!str_ieq(key, kPaletteKeyTable[i].key)) {
            continue;
        }
        lv_color_t* dst = reinterpret_cast<lv_color_t*>(
            reinterpret_cast<uint8_t*>(&s_customPalette) + kPaletteKeyTable[i].offset);
        *dst = color;
        return true;
    }
    return false;
}

static void trim_token_inplace(char* s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0u && isspace(static_cast<unsigned char>(s[len - 1]))) {
        s[--len] = '\0';
    }
    size_t start = 0u;
    while (s[start] && isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    if (start > 0u) {
        memmove(s, s + start, strlen(s + start) + 1u);
    }
}

static void parse_custom_theme_line(char* line, ThemeCustomParseStats& st) {
    trim_token_inplace(line);
    if (line[0] == '\0' || line[0] == '#') {
        return;
    }
    char* eq = strchr(line, '=');
    if (!eq) {
        ++st.invalid_lines;
        return;
    }
    *eq = '\0';
    char* key = line;
    char* val = eq + 1;
    trim_token_inplace(key);
    trim_token_inplace(val);
    if (key[0] == '\0' || val[0] == '\0') {
        ++st.invalid_lines;
        return;
    }
    if (str_ieq(key, "theme_dark")) {
        bool dark = true;
        if (!parse_bool_metadata(val, &dark)) {
            ++st.invalid_lines;
            return;
        }
        s_customThemeDark = dark;
        return;
    }
    // §4.8 (E22M): rail_opa_scale — optional numeric percent key (0..200), not a color.
    // Missing key keeps the builtin fallback already copied into s_customPalette.
    // Опциональный числовой ключ; при отсутствии остаётся builtin fallback.
    if (str_ieq(key, "rail_opa_scale")) {
        char* end = nullptr;
        const long v = strtol(val, &end, 10);
        if (!end || *end != '\0' || v < 0 || v > 200) {
            ++st.invalid_lines;
            return;
        }
        s_customPalette.rail_opa_scale = static_cast<uint8_t>(v);
        ++st.applied_keys;
        return;
    }
    lv_color_t c{};
    if (!parse_hex_color(val, &c)) {
        ++st.invalid_lines;
        return;
    }
    if (apply_palette_key(key, c)) {
        ++st.applied_keys;
    } else {
        ++st.unknown_keys;
    }
}

static bool parse_preset_token(const char* token, ThemePreset* out) {
    if (!token || !out) return false;
    char norm[12] = {};
    size_t i = 0u;
    for (; token[i] && i < sizeof(norm) - 1u; ++i) {
        norm[i] = static_cast<char>(tolower(static_cast<unsigned char>(token[i])));
    }
    if (strcmp(norm, "dark") == 0) {
        *out = ThemePreset::Dark;
        return true;
    }
    if (strcmp(norm, "light") == 0) {
        *out = ThemePreset::Light;
        return true;
    }
    if (strcmp(norm, "custom") == 0) {
        *out = ThemePreset::Custom;
        return true;
    }
    return false;
}

static const char* preset_to_file_token(ThemePreset p) {
    switch (p) {
        case ThemePreset::Light:
            return "light";
        case ThemePreset::Custom:
            return "custom";
        case ThemePreset::Dark:
        default:
            return "dark";
    }
}

static const YoRadioPalette& palette_for_preset(ThemePreset p) {
    switch (p) {
        case ThemePreset::Light:
            return kPaletteLight;
        case ThemePreset::Custom:
            return s_customPalette;
        case ThemePreset::Dark:
        default:
            return kPaletteDark;
    }
}

static bool theme_is_dark_for_lvgl(ThemePreset p) {
    switch (p) {
        case ThemePreset::Light:
            return false;
        case ThemePreset::Custom:
            return s_customThemeDark;
        case ThemePreset::Dark:
        default:
            return true;
    }
}

} // namespace

bool yoradio_theme_is_dark(ThemePreset preset) {
    return theme_is_dark_for_lvgl(preset);
}

const YoRadioPalette& yoradio_palette_service() {
    return kPaletteDark;
}

ThemePreset yoradio_theme_active_preset() {
    return s_preset;
}

void yoradio_theme_set_preset(ThemePreset p) {
    s_preset = p;
}

const YoRadioPalette& yoradio_palette() {
    return palette_for_preset(s_preset);
}

bool yoradio_theme_load_persisted_preset(ThemePreset* out) {
    if (!out || !fsIsReady()) {
        return false;
    }
    if (!LittleFS.exists(kThemePersistPath)) {
        return false;
    }
    File f = LittleFS.open(kThemePersistPath, "r");
    if (!f) {
        return false;
    }
    char buf[16] = {};
    const size_t n = f.readBytes(buf, sizeof(buf) - 1u);
    f.close();
    if (n == 0u) {
        return false;
    }
    trim_token_inplace(buf);
    return parse_preset_token(buf, out);
}

bool yoradio_theme_save_persisted_preset(ThemePreset preset) {
    if (!fsIsReady()) {
        return false;
    }
    if (preset > ThemePreset::Custom) {
        preset = ThemePreset::Dark;
    }
    File f = LittleFS.open(kThemePersistPath, "w");
    if (!f) {
        return false;
    }
    const char* token = preset_to_file_token(preset);
    const bool ok = f.print(token) > 0;
    f.close();
    return ok;
}

void yoradio_theme_reset_custom_palette() {
    copy_builtin_custom_palette();
    s_customStats = ThemeCustomParseStats{};
    s_customStats.file_exists = false;
    s_customStats.file_size   = 0;
    s_customThemeDark         = true;
}

bool yoradio_theme_custom_file_exists() {
    return fsIsReady() && LittleFS.exists(kThemeCustomPath);
}

const ThemeCustomParseStats& yoradio_theme_custom_parse_stats() {
    return s_customStats;
}

bool yoradio_theme_load_custom_palette_file(ThemeCustomParseStats* out) {
    copy_builtin_custom_palette();
    ThemeCustomParseStats st{};
    if (!fsIsReady()) {
        s_customStats = st;
        if (out) {
            *out = st;
        }
        return false;
    }
    if (!LittleFS.exists(kThemeCustomPath)) {
        s_customStats = st;
        if (out) {
            *out = st;
        }
        return false;
    }
    File f = LittleFS.open(kThemeCustomPath, "r");
    if (!f) {
        s_customStats = st;
        if (out) {
            *out = st;
        }
        return false;
    }
    st.file_exists = true;
    st.file_size   = static_cast<uint32_t>(f.size());
    char line[kThemeCustomLineMax] = {};
    while (f.available()) {
        const int n = f.readBytesUntil('\n', line, sizeof(line) - 1u);
        if (n <= 0) {
            break;
        }
        line[n > 0 ? static_cast<size_t>(n) : 0u] = '\0';
        if (n > 0 && line[static_cast<size_t>(n) - 1u] == '\r') {
            line[static_cast<size_t>(n) - 1u] = '\0';
        }
        parse_custom_theme_line(line, st);
    }
    f.close();
    s_customStats = st;
    if (out) {
        *out = s_customStats;
    }
    return true;
}

void yoradio_theme_init(lv_disp_t* disp) {
    if (s_theme_inited || !disp) {
        return;
    }

    // Stage 6.6R-E: load before first lv_theme_default_init — LittleFS ready (Config::init earlier).
    // Этап 6.6R-E: загрузка до первой инициализации темы; LittleFS уже смонтирован.
    ThemePreset persisted = ThemePreset::Dark;
    if (yoradio_theme_load_persisted_preset(&persisted)) {
        s_preset = persisted;
    }

    // Stage 6.6R-F1: load custom palette file into runtime s_customPalette (always; used when preset Custom).
    // Этап 6.6R-F1: загрузить theme_custom.txt до первого lv_theme_default_init.
    (void)yoradio_theme_load_custom_palette_file(nullptr);

    const YoRadioPalette& p = yoradio_palette();

    lv_theme_t* th =
        lv_theme_default_init(disp, p.accent, p.accent_soft, theme_is_dark_for_lvgl(s_preset), LV_FONT_DEFAULT);

    if (th) {
        lv_disp_set_theme(disp, th);
    }

    s_theme_inited = true;
}

void yoradio_theme_reinit(lv_disp_t* disp) {
    // Stage 6.6R-B: safe repeated call — lv_theme_default_init reuses existing allocation,
    // resets all ~60 LVGL default styles, and calls lv_obj_report_style_change(NULL) automatically
    // (because the display already uses this theme object via the initial yoradio_theme_init).
    // No memory leak: style_init_reset calls lv_style_reset (frees sub-allocs) before re-init.
    //
    // Повторный вызов безопасен: LVGL переиспользует аллоцированный блок,
    // сбрасывает все стили, автопропагирует изменения на все объекты.
    if (!disp) return;
    const YoRadioPalette& p = yoradio_palette();
    lv_theme_default_init(disp, p.accent, p.accent_soft, theme_is_dark_for_lvgl(s_preset), LV_FONT_DEFAULT);
}

} // namespace lvgl_ui

