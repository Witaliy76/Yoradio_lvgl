// One panel recipe: the ST7701 RGB565 480×480 path this firmware already runs (S04-1).
// Один рецепт панели: путь ST7701 RGB565 480×480, который прошивка уже исполняет (S04-1).
// Ownership: platform/display. Data only. The backend still owns init and scanout.
// Владелец: platform/display. Только данные. Init и scanout по-прежнему у backend.
//
// This header does not include st7701_type9_ops.h. That array is static constexpr,
// so every includer would get its own copy. The only production includer remains
// esp_lcd_st7701_backend.cpp, which replays the bytes. S04-3 connects the two by
// comparing InitRecipeId counts with kType9GfxEncodedBytes / kType9GfxHighLevelOps
// inside that backend TU. The existing replay remains the only interpreter.
// Этот заголовок не включает st7701_type9_ops.h. Массив там static constexpr,
// и каждый includer получил бы свою копию. Единственный production-includer —
// esp_lcd_st7701_backend.cpp. Счётчики InitRecipeId сверяются с константами
// массива внутри backend TU; существующий replay остаётся единственным интерпретатором.
//
// LVGL may read native_width, native_height, and pixel_format from this descriptor.
// It must not include the Type9 header or any esp_lcd header to do that.
// LVGL может читать native_width, native_height и pixel_format отсюда,
// не включая заголовок Type9 и заголовки esp_lcd.
#ifndef YORADIO_PLATFORM_DISPLAY_PANEL_ST7701_RGB565_480_H
#define YORADIO_PLATFORM_DISPLAY_PANEL_ST7701_RGB565_480_H

#include "panel_mode.h"

namespace yoradio {
namespace panel {

inline constexpr PanelMode kSt7701Rgb565_480 = {
    .controller        = Controller::St7701,
    .interface_kind    = Interface::Rgb565Parallel16,
    .native_width      = 480,
    .native_height     = 480,
    .pixel_format      = PixelFormat::Rgb565,
    .rgb_bus_width_bits = 16,
    .timing = {
        .hsync_pulse_width = 8,
        .hsync_back_porch  = 50,
        .hsync_front_porch = 10,
        .vsync_pulse_width = 8,
        .vsync_back_porch  = 20,
        .vsync_front_porch = 10,
        // Source polarity 1 is active-high. esp_lcd idle_low is 0. See
        // espLcdIdleLowFromSourcePolarity() and createRgbPanel().
        // Исходная полярность 1 — активный высокий. У esp_lcd idle_low равен 0.
        .hsync = {
            .source_polarity  = 1,
            .esp_lcd_idle_low = espLcdIdleLowFromSourcePolarity(1),
        },
        .vsync = {
            .source_polarity  = 1,
            .esp_lcd_idle_low = espLcdIdleLowFromSourcePolarity(1),
        },
        .de_idle_high     = 0,
        .pclk_active_neg  = 0,
        .pclk_idle_high   = 0,
    },
    .init = {
        .name                 = "st7701-type9-rgb565-480",
        .provenance           = "Arduino_GFX 00dcd684 st7701_type9_init_operations",
        .encoded_bytes        = 271,
        .high_level_ops       = 55,
        .in_stream_delay_ms   = 120,
        .soft_reset_command   = 0x01,
        .soft_reset_delay_ms  = 120,
    },
    .quirks = {
        .orientation_commands_only_before_rgb_scanout = true,
    },
};

static_assert(kSt7701Rgb565_480.native_width == 480, "ST7701 native width");
static_assert(kSt7701Rgb565_480.native_height == 480, "ST7701 native height");
static_assert(kSt7701Rgb565_480.rgb_bus_width_bits == 16, "ST7701 RGB bus width");
static_assert(kSt7701Rgb565_480.pixel_format == PixelFormat::Rgb565, "ST7701 pixel format");
static_assert(kSt7701Rgb565_480.timing.hsync_pulse_width == 8, "HSYNC pulse");
static_assert(kSt7701Rgb565_480.timing.hsync_back_porch == 50, "HSYNC back porch");
static_assert(kSt7701Rgb565_480.timing.hsync_front_porch == 10, "HSYNC front porch");
static_assert(kSt7701Rgb565_480.timing.vsync_pulse_width == 8, "VSYNC pulse");
static_assert(kSt7701Rgb565_480.timing.vsync_back_porch == 20, "VSYNC back porch");
static_assert(kSt7701Rgb565_480.timing.vsync_front_porch == 10, "VSYNC front porch");
static_assert(kSt7701Rgb565_480.timing.hsync.source_polarity == 1, "HSYNC source polarity");
static_assert(kSt7701Rgb565_480.timing.vsync.source_polarity == 1, "VSYNC source polarity");
static_assert(kSt7701Rgb565_480.timing.hsync.esp_lcd_idle_low == 0, "HSYNC idle_low");
static_assert(kSt7701Rgb565_480.timing.vsync.esp_lcd_idle_low == 0, "VSYNC idle_low");
static_assert(kSt7701Rgb565_480.timing.hsync.esp_lcd_idle_low ==
                  espLcdIdleLowFromSourcePolarity(kSt7701Rgb565_480.timing.hsync.source_polarity),
              "HSYNC polarity map");
static_assert(kSt7701Rgb565_480.timing.vsync.esp_lcd_idle_low ==
                  espLcdIdleLowFromSourcePolarity(kSt7701Rgb565_480.timing.vsync.source_polarity),
              "VSYNC polarity map");
static_assert(kSt7701Rgb565_480.timing.de_idle_high == 0, "DE idle");
static_assert(kSt7701Rgb565_480.timing.pclk_active_neg == 0, "PCLK active edge");
static_assert(kSt7701Rgb565_480.timing.pclk_idle_high == 0, "PCLK idle");
static_assert(kSt7701Rgb565_480.init.encoded_bytes == 271, "Type9 encoded size");
static_assert(kSt7701Rgb565_480.init.high_level_ops == 55, "Type9 op count");
static_assert(kSt7701Rgb565_480.init.in_stream_delay_ms == 120, "Type9 in-stream delay");
static_assert(kSt7701Rgb565_480.init.soft_reset_command == 0x01, "soft reset command");
static_assert(kSt7701Rgb565_480.init.soft_reset_delay_ms == 120, "soft reset delay");
static_assert(kSt7701Rgb565_480.quirks.orientation_commands_only_before_rgb_scanout,
              "orientation quirk");

}  // namespace panel
}  // namespace yoradio

#endif  // YORADIO_PLATFORM_DISPLAY_PANEL_ST7701_RGB565_480_H
