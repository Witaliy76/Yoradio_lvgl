// ResolvedDisplayConfig — one compile-time view of the selected display (S04-3A).
// ResolvedDisplayConfig — итоговая конфигурация выбранного дисплея при сборке (S04-3A).
//
// Four sources keep four different meanings:
//   panel       — PanelMode recipe chosen by platform/composition.h: geometry, timing, init id;
//   wiring      — effective board pins, including Custom Wiring from src/myoptions.h;
//   recommended — device-tested host policy from BoardDefaults.display;
//   user fields — build choices the user may override in src/myoptions.h.
// Четыре источника сохраняют разные роли: физика панели, итоговая разводка платы,
// проверенная рекомендация хоста и разрешённые пользовательские настройки.
//
// This is data composition, not a driver. esp_lcd, LVGL, Arduino, FreeRTOS and the
// init byte tables stay out of this generic header. The real GPIO come through
// kWiring.display; frequencies come from kActiveBoardDefaults.display. Init bytes stay
// in the display host because that translation unit owns the byte stream and its replay.
// Это композиция данных, а не драйвер. Реальные GPIO приходят из kWiring.display,
// частоты — из kActiveBoardDefaults.display. Команды Type9 остаются в драйвере.
#ifndef YORADIO_PLATFORM_DISPLAY_RESOLVED_DISPLAY_CONFIG_H
#define YORADIO_PLATFORM_DISPLAY_RESOLVED_DISPLAY_CONFIG_H

#include <stdint.h>

#include "../board/board_select.h"
#include "../composition.h"

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

// The single source of these flags is core/options.h: the board profile default
// (YORADIO_BD_RGB_*) unless myoptions.h overrides it. This header has no fallback of
// its own, so a TU that includes it before options.h fails here instead of silently
// reading a different value.
// Единственный источник флагов — core/options.h: умолчание профиля платы
// (YORADIO_BD_RGB_*), если myoptions.h не переопределил. Собственных запасных значений
// у заголовка нет: TU, подключивший его раньше options.h, падает здесь, а не читает
// тихо другое значение.
#if !defined(YORADIO_RGB_BOUNCE10) || !defined(YORADIO_RGB_FLASH_GUARD)
#  error "resolved_display_config.h: include core/options.h before this header; YORADIO_RGB_BOUNCE10 / YORADIO_RGB_FLASH_GUARD come from options.h (board profile default, myoptions.h override)."
#endif

constexpr bool userBounce10Enabled() {
    return YORADIO_RGB_BOUNCE10 != 0;
}

constexpr bool userRgbFlashGuardEnabled() {
    return YORADIO_RGB_FLASH_GUARD != 0;
}

constexpr bool userBootOrientation180Enabled() {
#if !defined(DISPLAY_BOOT_ORIENTATION_180)
#  error "resolved_display_config.h: DISPLAY_BOOT_ORIENTATION_180 must come from core/options.h."
#endif
    return DISPLAY_BOOT_ORIENTATION_180 != 0;
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
    .panel = composition::kPanelMode,
    .wiring = kWiring.display,
    .recommended = kActiveBoardDefaults.display,
    .bounce10_enabled = detail::userBounce10Enabled(),
    .effective_bounce_buffer_px =
        effectiveBounceBufferPixels(detail::userBounce10Enabled(), composition::kPanelMode),
    .rgb_flash_guard_enabled = detail::userRgbFlashGuardEnabled(),
    .boot_orientation_180 = detail::userBootOrientation180Enabled(),
};

// Mandatory compatibility checks. Failures name the owner a new user should inspect.
// Обязательные проверки совместимости; сообщение называет файл-владелец настройки.
static_assert(composition::kDisplayHost == composition::DisplayHost::Rgb3Wire &&
                  kResolvedDisplayConfig.panel.interface_kind ==
                      panel::Interface::Rgb565Parallel16,
              "ResolvedDisplayConfig: the only display host is RGB565 parallel 16-bit + 3-wire command bus; check platform/composition.h.");
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

static_assert(kResolvedDisplayConfig.panel.timing.hsync.esp_lcd_idle_low ==
                  panel::espLcdIdleLowFromSourcePolarity(
                      kResolvedDisplayConfig.panel.timing.hsync.source_polarity) &&
                  kResolvedDisplayConfig.panel.timing.vsync.esp_lcd_idle_low ==
                  panel::espLcdIdleLowFromSourcePolarity(
                      kResolvedDisplayConfig.panel.timing.vsync.source_polarity),
              "ResolvedDisplayConfig: esp_lcd idle_low must be the complement of the source sync polarity; check panel_mode.h polarity mapping.");

// The tested host recommendation of a board (render mode, PCLK 6/10/6, bounce 0,
// flash guard) is a board fact: asserted in profiles/<board>/board_validate_profile.h.
// Проверенная рекомендация хоста платы — факт платы: проверяется в профильном валидаторе.
static_assert(effectiveBounceBufferPixels(false, kResolvedDisplayConfig.panel) == 0u &&
                  effectiveBounceBufferPixels(true, kResolvedDisplayConfig.panel) ==
                      kResolvedDisplayConfig.panel.native_width * 10u,
              "ResolvedDisplayConfig: B10 means native panel width times 10, not the flag value 1; check YORADIO_RGB_BOUNCE10 in src/myoptions.h.");
static_assert(kResolvedDisplayConfig.effective_bounce_buffer_px ==
                  effectiveBounceBufferPixels(kResolvedDisplayConfig.bounce10_enabled,
                                              kResolvedDisplayConfig.panel),
              "ResolvedDisplayConfig: effective bounce must be derived from YORADIO_RGB_BOUNCE10 and panel native width.");

static_assert(kResolvedDisplayConfig.panel.init.recipe != panel::InitRecipe::None,
              "ResolvedDisplayConfig: the selected PanelMode names no init recipe.");

}  // namespace yoradio

#endif  // YORADIO_PLATFORM_DISPLAY_RESOLVED_DISPLAY_CONFIG_H
