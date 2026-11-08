// ResolvedDisplayConfig — one compile-time view of the selected display (S04-3A).
// ResolvedDisplayConfig — итоговая конфигурация выбранного дисплея при сборке (S04-3A).
//
// Four sources keep four different meanings:
//   panel       — physical ST7701 recipe: geometry, RGB565 timing and init identity;
//   wiring      — effective board pins, including Custom Wiring from src/myoptions.h;
//   recommended — device-tested host policy from BoardDefaults.display;
//   user fields — build choices the user may override in src/myoptions.h.
// Четыре источника сохраняют разные роли: физика панели, итоговая разводка платы,
// проверенная рекомендация хоста и разрешённые пользовательские настройки.
//
// This is data composition, not a driver. esp_lcd, LVGL, Arduino, FreeRTOS and the
// Type9 byte table stay out of this generic header. The real GPIO come through
// kWiring.display; frequencies come from kActiveBoardDefaults.display. Type9 stays
// in the backend because that translation unit owns the byte stream and its replay.
// Это композиция данных, а не драйвер. Реальные GPIO приходят из kWiring.display,
// частоты — из kActiveBoardDefaults.display. Команды Type9 остаются в драйвере.
#ifndef YORADIO_PLATFORM_DISPLAY_RESOLVED_DISPLAY_CONFIG_H
#define YORADIO_PLATFORM_DISPLAY_RESOLVED_DISPLAY_CONFIG_H

#include <stdint.h>

#include "../board/board_select.h"
#include "panel_st7701_rgb565_480.h"

namespace yoradio {

// Slot names document the load-bearing esp_lcd order without copying one GPIO.
// Имена слотов фиксируют важный порядок esp_lcd без второй таблицы GPIO.
enum class Rgb565DataSlot : uint8_t {
    B0 = 0, B1 = 1, B2 = 2, B3 = 3, B4 = 4,
    G0 = 5, G1 = 6, G2 = 7, G3 = 8, G4 = 9, G5 = 10,
    R0 = 11, R1 = 12, R2 = 13, R3 = 14, R4 = 15,
};

constexpr uint8_t rgb565DataSlotIndex(Rgb565DataSlot slot) {
    return static_cast<uint8_t>(slot);
}

// Compare meanings explicitly. PanelMode and BoardProfile deliberately use
// different enum types; equal underlying numbers are not part of the contract.
// Сравниваем смысл явно: числовое совпадение разных enum не является контрактом.
constexpr bool panelControllerMatches(panel::Controller panel_controller,
                                      PanelController board_controller) {
    switch (panel_controller) {
        case panel::Controller::Unknown:
            return board_controller == PanelController::Unknown;
        case panel::Controller::St7701:
            return board_controller == PanelController::St7701;
    }
    return false;
}

constexpr bool panelInterfaceMatches(panel::Interface panel_interface,
                                     DisplayTransport board_transport) {
    switch (panel_interface) {
        case panel::Interface::Unknown:
            return board_transport == DisplayTransport::Unknown;
        case panel::Interface::Rgb565Parallel16:
            return board_transport == DisplayTransport::Rgb565Parallel16;
    }
    return false;
}

constexpr uint32_t effectiveBounceBufferPixels(bool bounce10_enabled,
                                               const panel::PanelMode& panel_mode) {
    return bounce10_enabled ? static_cast<uint32_t>(panel_mode.native_width) * 10u : 0u;
}

namespace detail {

// These fallbacks preserve the existing build-choice defaults; they do not
// define macros and therefore cannot leak a second settings source.
// Эти запасные значения сохраняют действующие умолчания, не определяя новых макросов.
constexpr bool userBounce10Enabled() {
#if defined(YORADIO_RGB_BOUNCE10)
    return YORADIO_RGB_BOUNCE10 != 0;
#else
    return false;
#endif
}

constexpr bool userRgbFlashGuardEnabled() {
#if defined(YORADIO_RGB_FLASH_GUARD)
    return YORADIO_RGB_FLASH_GUARD != 0;
#else
    return true;
#endif
}

constexpr bool userBootOrientation180Enabled() {
#if defined(ST7701_BOOT_ORIENTATION_180)
    return ST7701_BOOT_ORIENTATION_180 != 0;
#else
    return false;
#endif
}

}  // namespace detail

struct ResolvedDisplayConfig {
    // References keep one authoritative object for recipe, pins and recommendation.
    // Ссылки сохраняют по одному авторитетному объекту рецепта, пинов и рекомендации.
    const panel::PanelMode& panel;
    const DisplayWiring& wiring;
    const DisplayDefaults& recommended;

    // User choices are effective build values, not claims of device validation.
    // Это действующие значения сборки, а не утверждение об аппаратной проверке.
    bool bounce10_enabled;
    uint32_t effective_bounce_buffer_px;
    bool rgb_flash_guard_enabled;
    bool boot_orientation_180;
};

inline constexpr ResolvedDisplayConfig kResolvedDisplayConfig = {
    .panel = panel::kSt7701Rgb565_480,
    .wiring = kWiring.display,
    .recommended = kActiveBoardDefaults.display,
    .bounce10_enabled = detail::userBounce10Enabled(),
    .effective_bounce_buffer_px =
        effectiveBounceBufferPixels(detail::userBounce10Enabled(), panel::kSt7701Rgb565_480),
    .rgb_flash_guard_enabled = detail::userRgbFlashGuardEnabled(),
    .boot_orientation_180 = detail::userBootOrientation180Enabled(),
};

// Mandatory compatibility checks. Failures name the owner a new user should inspect.
// Обязательные проверки совместимости; сообщение называет файл-владелец настройки.
static_assert(kResolvedDisplayConfig.panel.controller == panel::Controller::St7701 &&
                  kResolvedDisplayConfig.panel.interface_kind ==
                      panel::Interface::Rgb565Parallel16,
              "ResolvedDisplayConfig: supported 4848S040 panel is ST7701 RGB565 parallel 16-bit; check panel_st7701_rgb565_480.h.");
static_assert(panelControllerMatches(kResolvedDisplayConfig.panel.controller,
                                    kResolvedDisplayConfig.wiring.controller),
              "ResolvedDisplayConfig: PanelMode controller does not match kWiring.display; check platform/display/panel_st7701_rgb565_480.h and src/myoptions.h Custom Wiring.");
static_assert(panelInterfaceMatches(kResolvedDisplayConfig.panel.interface_kind,
                                   kResolvedDisplayConfig.wiring.transport),
              "ResolvedDisplayConfig: PanelMode interface does not match kWiring.display; check the selected board profile and panel descriptor.");
static_assert(kResolvedDisplayConfig.panel.pixel_format == panel::PixelFormat::Rgb565 &&
                  kResolvedDisplayConfig.panel.rgb_bus_width_bits == 16,
              "ResolvedDisplayConfig: this panel contract requires RGB565 on a 16-bit parallel bus; check platform/display/panel_st7701_rgb565_480.h.");
static_assert(kResolvedDisplayConfig.panel.native_width ==
                      kResolvedDisplayConfig.wiring.native_width &&
                  kResolvedDisplayConfig.panel.native_height ==
                      kResolvedDisplayConfig.wiring.native_height,
              "ResolvedDisplayConfig: PanelMode native geometry does not match BoardProfile; check panel_st7701_rgb565_480.h and the selected board profile.");

static_assert(rgb565DataSlotIndex(Rgb565DataSlot::B0) == 0 &&
                  rgb565DataSlotIndex(Rgb565DataSlot::B4) == 4 &&
                  rgb565DataSlotIndex(Rgb565DataSlot::G0) == 5 &&
                  rgb565DataSlotIndex(Rgb565DataSlot::G5) == 10 &&
                  rgb565DataSlotIndex(Rgb565DataSlot::R0) == 11 &&
                  rgb565DataSlotIndex(Rgb565DataSlot::R4) == 15,
              "ResolvedDisplayConfig: RGB565 slots must remain B0..B4, G0..G5, R0..R4; check board_contract.h DisplayWiring.");
static_assert(&kResolvedDisplayConfig.wiring == &kWiring.display,
              "ResolvedDisplayConfig: wiring must reference effective kWiring.display so Custom Wiring is preserved.");

static_assert(kResolvedDisplayConfig.panel.timing.hsync.source_polarity == 1 &&
                  kResolvedDisplayConfig.panel.timing.hsync.esp_lcd_idle_low == 0 &&
                  kResolvedDisplayConfig.panel.timing.vsync.source_polarity == 1 &&
                  kResolvedDisplayConfig.panel.timing.vsync.esp_lcd_idle_low == 0,
              "ResolvedDisplayConfig: source sync polarity 1 must map to esp_lcd idle_low 0; check panel_mode.h polarity mapping.");

// Recommendation checks describe TEST1's proven topology. They intentionally do
// not compare user choices against the recommendation or reject supported overrides.
// Проверки рекомендаций описывают проверенную топологию TEST1 и не запрещают
// поддерживаемые пользовательские настройки.
static_assert(kResolvedDisplayConfig.recommended.render_mode == DisplayRenderMode::Partial &&
                  kResolvedDisplayConfig.recommended.partial_draw_lines == 160,
              "ResolvedDisplayConfig: tested board default is PARTIAL rendering at 160 lines; check board_defaults.h.");
static_assert(kResolvedDisplayConfig.recommended.draw_buffer_count == 1 &&
                  kResolvedDisplayConfig.recommended.draw_buffer_memory ==
                      DisplayBufferMemory::Psram,
              "ResolvedDisplayConfig: tested board default is one PSRAM draw buffer; check board_defaults.h.");
static_assert(kResolvedDisplayConfig.recommended.rgb_framebuffer_count == 1 &&
                  kResolvedDisplayConfig.recommended.rgb_framebuffer_memory ==
                      DisplayBufferMemory::Psram &&
                  kResolvedDisplayConfig.recommended.rgb_framebuffer_owner ==
                      RgbFramebufferOwner::PanelDriver &&
                  !kResolvedDisplayConfig.recommended.double_framebuffer,
              "ResolvedDisplayConfig: tested board default is one panel-driver PSRAM framebuffer, not double framebuffer; check board_defaults.h.");
static_assert(kResolvedDisplayConfig.recommended.startup_pclk_hz == 6000000u &&
                  kResolvedDisplayConfig.recommended.runtime_pclk_hz == 10000000u &&
                  kResolvedDisplayConfig.recommended.flash_guard_pclk_hz == 6000000u,
              "ResolvedDisplayConfig: tested PCLK policy is startup 6 MHz, runtime 10 MHz and flash-guard 6 MHz; check board_defaults.h.");
static_assert(kResolvedDisplayConfig.recommended.startup_pclk_hz ==
                  kResolvedDisplayConfig.recommended.flash_guard_pclk_hz &&
                  kResolvedDisplayConfig.recommended.promote_pclk_once_on_boot_to_main,
              "ResolvedDisplayConfig: startup and flash-guard PCLK must match and the tested Boot-to-Main promotion must remain enabled; check board_defaults.h.");
static_assert(kResolvedDisplayConfig.recommended.bounce_buffer_px == 0,
              "ResolvedDisplayConfig: tested board recommendation is bounce 0 px; this does not ban YORADIO_RGB_BOUNCE10=1.");
static_assert(effectiveBounceBufferPixels(false, panel::kSt7701Rgb565_480) == 0u &&
                  effectiveBounceBufferPixels(true, panel::kSt7701Rgb565_480) == 4800u,
              "ResolvedDisplayConfig: B10 means native panel width times 10 (4800 px), not the flag value 1; check YORADIO_RGB_BOUNCE10 in src/myoptions.h.");
static_assert(kResolvedDisplayConfig.effective_bounce_buffer_px ==
                  effectiveBounceBufferPixels(kResolvedDisplayConfig.bounce10_enabled,
                                              kResolvedDisplayConfig.panel),
              "ResolvedDisplayConfig: effective bounce must be derived from YORADIO_RGB_BOUNCE10 and panel native width.");

static_assert(kResolvedDisplayConfig.panel.init.encoded_bytes == 271u &&
                  kResolvedDisplayConfig.panel.init.high_level_ops == 55u,
              "ResolvedDisplayConfig: PanelMode Type9 metadata must stay 271 bytes / 55 operations; check panel_st7701_rgb565_480.h.");

}  // namespace yoradio

#endif  // YORADIO_PLATFORM_DISPLAY_RESOLVED_DISPLAY_CONFIG_H
