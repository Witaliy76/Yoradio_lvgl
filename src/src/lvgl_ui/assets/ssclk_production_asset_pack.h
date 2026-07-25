/*
 * SSCLK-A3B screensaver clock asset pack.
 * Theme → LittleFS background path + approved Flash hand/cap descriptors.
 */
// Author: Witaliy76 - https://github.com/Witaliy76
#pragma once

#include <cstdint>

#include "lvgl.h"
#include "ssclk_production_assets.h"
#include "../theme/lv_theme_yoradio.h"

namespace lvgl_ui {

struct ScreensaverClockThemeAssets {
    const char* background_path;
    const lv_img_dsc_t* hour;
    int16_t hour_pivot_x;
    int16_t hour_pivot_y;
    const lv_img_dsc_t* minute;
    int16_t minute_pivot_x;
    int16_t minute_pivot_y;
    const lv_img_dsc_t* second;
    int16_t second_pivot_x;
    int16_t second_pivot_y;
    const lv_img_dsc_t* cap;
    uint16_t hour_front_length;
    uint16_t minute_front_length;
    uint16_t second_front_length;
    uint8_t hour_tail;
    uint8_t minute_tail;
    uint8_t second_tail;
};

static constexpr const char kSsclkBgPathDark[] =
    "/screensaver_clock/ssclk_dark_bg_480.bin";
static constexpr const char kSsclkBgPathLight[] =
    "/screensaver_clock/ssclk_light_bg_480.bin";
static constexpr const char kSsclkBgPathCustom[] =
    "/screensaver_clock/ssclk_custom_bg_480.bin";

inline const char* ssclk_background_path_for_preset(ThemePreset preset) {
    switch (preset) {
        case ThemePreset::Light:
            return kSsclkBgPathLight;
        case ThemePreset::Custom:
            return kSsclkBgPathCustom;
        case ThemePreset::Dark:
        default:
            return kSsclkBgPathDark;
    }
}

static constexpr ScreensaverClockThemeAssets kSsclkClockAssetsDark{
    kSsclkBgPathDark,
    &ssclk_dark_c_hour,
    static_cast<int16_t>(SSCLK_DARK_C_HOUR_PIVOT_X),
    static_cast<int16_t>(SSCLK_DARK_C_HOUR_PIVOT_Y),
    &ssclk_dark_c_minute,
    static_cast<int16_t>(SSCLK_DARK_C_MINUTE_PIVOT_X),
    static_cast<int16_t>(SSCLK_DARK_C_MINUTE_PIVOT_Y),
    &ssclk_dark_c_second,
    static_cast<int16_t>(SSCLK_DARK_C_SECOND_PIVOT_X),
    static_cast<int16_t>(SSCLK_DARK_C_SECOND_PIVOT_Y),
    &ssclk_dark_c_cap,
    118u,
    162u,
    184u,
    8u,
    10u,
    18u,
};

static constexpr ScreensaverClockThemeAssets kSsclkClockAssetsLight{
    kSsclkBgPathLight,
    &ssclk_light_a_hour,
    static_cast<int16_t>(SSCLK_LIGHT_A_HOUR_PIVOT_X),
    static_cast<int16_t>(SSCLK_LIGHT_A_HOUR_PIVOT_Y),
    &ssclk_light_a_minute,
    static_cast<int16_t>(SSCLK_LIGHT_A_MINUTE_PIVOT_X),
    static_cast<int16_t>(SSCLK_LIGHT_A_MINUTE_PIVOT_Y),
    &ssclk_light_a_second,
    static_cast<int16_t>(SSCLK_LIGHT_A_SECOND_PIVOT_X),
    static_cast<int16_t>(SSCLK_LIGHT_A_SECOND_PIVOT_Y),
    &ssclk_light_a_cap,
    118u,
    162u,
    184u,
    8u,
    10u,
    18u,
};

static constexpr ScreensaverClockThemeAssets kSsclkClockAssetsCustom{
    kSsclkBgPathCustom,
    &ssclk_custom_c_hour,
    static_cast<int16_t>(SSCLK_CUSTOM_C_HOUR_PIVOT_X),
    static_cast<int16_t>(SSCLK_CUSTOM_C_HOUR_PIVOT_Y),
    &ssclk_custom_c_minute,
    static_cast<int16_t>(SSCLK_CUSTOM_C_MINUTE_PIVOT_X),
    static_cast<int16_t>(SSCLK_CUSTOM_C_MINUTE_PIVOT_Y),
    &ssclk_custom_c_second,
    static_cast<int16_t>(SSCLK_CUSTOM_C_SECOND_PIVOT_X),
    static_cast<int16_t>(SSCLK_CUSTOM_C_SECOND_PIVOT_Y),
    &ssclk_custom_c_cap,
    105u,
    145u,
    166u,
    8u,
    10u,
    18u,
};

inline const ScreensaverClockThemeAssets* ssclk_clock_assets_for_preset(ThemePreset preset) {
    switch (preset) {
        case ThemePreset::Light:
            return &kSsclkClockAssetsLight;
        case ThemePreset::Custom:
            return &kSsclkClockAssetsCustom;
        case ThemePreset::Dark:
        default:
            return &kSsclkClockAssetsDark;
    }
}

} // namespace lvgl_ui
