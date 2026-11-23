// PanelMode: GC9503 480x480 RGB565 parallel 16-bit with a 3-wire command bus.
// PanelMode: GC9503 480x480, RGB565 параллельно 16 бит, 3-wire шина команд.
// Ownership: platform/display. Data only: no esp_lcd, no init bytes.
// Владелец: platform/display. Только данные: без esp_lcd и байтов инициализации.
//
// First board: VIEWE UEDX48480040E-WB-A (panel UE040WV-RH40-A044C).
// Evidence (S07_bis-8): DOCUMENTED, not device-verified on our firmware yet.
//   Porches/pulses: working ESPHome config for this board (espcontrol #2140):
//     HSYNC pulse 32 / back 10 / front 16, VSYNC pulse 12 / back 15 / front 3,
//     pclk_inverted false.
//   Sync polarity: same esp_lcd idle levels as the official driver defaults
//     (flags zero: idle_low 0, pclk_active_neg 0, de_idle_high 0).
//   Init recipe: espressif/esp_lcd_gc9503 3.0.1 (gc9503_ops.h, D10 step 2), with
//     COLMOD 0x60 (18-bit panel interface) instead of 0x50: device evidence S07_bis-8b.
// PCLK is host policy (BoardDefaults: 6 MHz first scanout, 10 MHz runtime, D6), not here.
// Первая плата: VIEWE UEDX48480040E-WB-A. Доказательства: DOCUMENTED, на нашей
// прошивке ещё не проверено. Порчи — из рабочего конфига espcontrol #2140; полярности —
// значения по умолчанию официального драйвера; рецепт — esp_lcd_gc9503 3.0.1.
#ifndef YORADIO_PLATFORM_DISPLAY_PANEL_GC9503_RGB565_480_H
#define YORADIO_PLATFORM_DISPLAY_PANEL_GC9503_RGB565_480_H

#include "panel_mode.h"

namespace yoradio {
namespace panel {

inline constexpr PanelMode kGc9503Rgb565_480 = {
    .controller        = Controller::Gc9503,
    .interface_kind    = Interface::Rgb565Parallel16,
    .native_width      = 480,
    .native_height     = 480,
    .pixel_format      = PixelFormat::Rgb565,
    .rgb_bus_width_bits = 16,
    .timing = {
        .hsync_pulse_width = 32,
        .hsync_back_porch  = 10,
        .hsync_front_porch = 16,
        .vsync_pulse_width = 12,
        .vsync_back_porch  = 15,
        .vsync_front_porch = 3,
        // Source polarity 1 maps to esp_lcd idle_low 0: the official driver's default.
        // Полярность 1 соответствует idle_low 0 — умолчанию официального драйвера.
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
        .recipe               = InitRecipe::Gc9503Default,
        .name                 = "gc9503-default-rgb565-480",
        .provenance           = "espressif/esp_lcd_gc9503 3.0.1 vendor_specific_init_default (esp-bsp fb5714e); COLMOD 0x60",
        .encoded_bytes        = 721,
        .high_level_ops       = 161,
        // Sum of in-table delays: SLPOUT 120 ms + DISPON 20 ms.
        // Сумма задержек в таблице: SLPOUT 120 мс + DISPON 20 мс.
        .in_stream_delay_ms   = 140,
        .soft_reset_command   = 0x01,
        .soft_reset_delay_ms  = 120,
    },
    .quirks = {
        // Unknown for this panel; boot orientation 180 is not offered for GC9503 (Q3).
        // Для этой панели неизвестно; поворот 180 для GC9503 не предлагается (Q3).
        .orientation_commands_only_before_rgb_scanout = false,
    },
};

static_assert(kGc9503Rgb565_480.native_width == 480 && kGc9503Rgb565_480.native_height == 480,
              "GC9503 native geometry");
static_assert(kGc9503Rgb565_480.rgb_bus_width_bits == 16 &&
                  kGc9503Rgb565_480.pixel_format == PixelFormat::Rgb565,
              "GC9503 RGB565 on a 16-bit bus");
static_assert(kGc9503Rgb565_480.init.recipe == InitRecipe::Gc9503Default, "GC9503 recipe id");
static_assert(kGc9503Rgb565_480.init.encoded_bytes == 721 &&
                  kGc9503Rgb565_480.init.high_level_ops == 161,
              "GC9503 recipe size (gc9503_ops.h)");
static_assert(kGc9503Rgb565_480.timing.hsync.esp_lcd_idle_low ==
                  espLcdIdleLowFromSourcePolarity(kGc9503Rgb565_480.timing.hsync.source_polarity) &&
                  kGc9503Rgb565_480.timing.vsync.esp_lcd_idle_low ==
                  espLcdIdleLowFromSourcePolarity(kGc9503Rgb565_480.timing.vsync.source_polarity),
              "GC9503 polarity map");

}  // namespace panel
}  // namespace yoradio

#endif  // YORADIO_PLATFORM_DISPLAY_PANEL_GC9503_RGB565_480_H
