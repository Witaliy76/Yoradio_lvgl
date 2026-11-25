// Profile-specific assertions of the VIEWE UEDX48480040E-WB-A.
// Профильные проверки VIEWE UEDX48480040E-WB-A.
// Included by board_validate.h through YORADIO_BOARD_PROFILE_VALIDATE_HEADER (board_select.h).
// Same role as the 4848S040 validator: factory numbers, effective wiring in factory mode,
// controller identities, SD map and the declared shared lines.
// Включается board_validate.h по имени из board_select.h. Роль та же, что у валидатора
// 4848S040: заводские номера, итоговая разводка в штатном режиме, контроллеры, карта SD
// и объявленные общие линии.
#ifndef YORADIO_PLATFORM_BOARD_PROFILES_ESP32S3_UEDX48480040_BOARD_VALIDATE_PROFILE_H
#define YORADIO_PLATFORM_BOARD_PROFILES_ESP32S3_UEDX48480040_BOARD_VALIDATE_PROFILE_H

#include "../../board_select.h"

#ifdef __cplusplus
#include <stdint.h>

namespace yoradio {
namespace uedx48480040_check {

// V1.3 schematic: slots [0..4]=B0..B4, [5..10]=G0..G5, [11..15]=R0..R4.
inline constexpr uint8_t kFactoryData[16] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

constexpr bool dataEquals(const DisplayWiring& d) {
    for (uint8_t i = 0; i < 16; ++i) {
        if (d.data_gpio[i] != kFactoryData[i]) {
            return false;
        }
    }
    return true;
}

constexpr bool controlEquals(const BoardProfile& p) {
    return p.display.hsync_gpio == 16 && p.display.vsync_gpio == 17 &&
           p.display.de_gpio == 18 && p.display.pclk_gpio == 21 &&
           p.display.cmd_cs_gpio == 39 && p.display.cmd_sck_gpio == 48 &&
           p.display.cmd_sda_gpio == 47 && p.display.backlight_gpio == 38 &&
           p.touch.i2c_sda_gpio == 40 && p.touch.i2c_scl_gpio == 41 &&
           p.touch.int_gpio == kPinNotConnected && p.touch.reset_gpio == kPinNotConnected;
}

}  // namespace uedx48480040_check

static_assert(kActiveBoard.display.controller == PanelController::Gc9503,
              "UEDX48480040 panel controller is GC9503");
static_assert(kActiveBoard.touch.controller == TouchController::Ft5x06 &&
                  kActiveBoard.touch.i2c_address == 0x38,
              "UEDX48480040 touch is FT6336U (FT5x06 map) at 0x38");
static_assert(kActiveBoard.display.native_width == 480 && kActiveBoard.display.native_height == 480,
              "UEDX48480040 panel is 480x480");
static_assert(kActiveBoard.display.panel_reset_gpio == kPinNotConnected,
              "UEDX48480040 LCM RESET is a passive RC, not routed to the SoC");
static_assert(uedx48480040_check::dataEquals(kActiveBoard.display),
              "UEDX48480040 factory RGB data map (V1.3 schematic)");
static_assert(uedx48480040_check::controlEquals(kActiveBoard),
              "UEDX48480040 factory sync/command/backlight/touch pins (V1.3 schematic)");
#if !YORADIO_CUSTOM_WIRING
static_assert(uedx48480040_check::dataEquals(kWiring.display) &&
                  uedx48480040_check::controlEquals(kWiring),
              "UEDX48480040 factory mode: effective wiring equals the factory profile");
#endif

static_assert(kActiveBoard.power.boot_button_gpio == kActiveBoard.display.data_gpio[15],
              "on this board the BOOT button and RGB R4 are the same lifecycle-shared pin");
static_assert(!kActiveBoard.audio.onboard_output_present,
              "UEDX48480040 has no onboard audio output");
static_assert(kActiveBoard.usb.native_usb_pins_free_in_stock_wiring,
              "UEDX48480040 stock wiring leaves GPIO19/20 free");

// TF map (V1.3): CS 47 (= command SDA through R17), CLK 45, CMD 42, DAT0 46.
static_assert(kActiveBoard.storage.onboard_sd_present &&
                  kActiveBoard.storage.sd_cs_gpio == 47 && kActiveBoard.storage.sd_clk_gpio == 45 &&
                  kActiveBoard.storage.sd_cmd_gpio == 42 && kActiveBoard.storage.sd_dat0_gpio == 46,
              "UEDX48480040 TF map: CS 47, CLK 45, CMD 42, DAT0 46");
static_assert(kActiveBoard.storage.sd_pin_evidence == PinEvidence::SchematicCandidate,
              "UEDX48480040 TF map is SchematicCandidate; DeviceVerified needs the hardware gate");
static_assert(!kActiveBoard.storage.sd_card_detect_present && !kActiveBoard.storage.sd_supports_sdmmc_4bit,
              "UEDX48480040: no CD GPIO, no 4-bit SDMMC");

static_assert(kActiveBoard.shared.count == 2,
              "UEDX48480040 declares exactly two shared pins: GPIO47 and GPIO48");
static_assert(wiring_check::sharingDeclared(kActiveBoard.shared, 47,
                                            PinRole::DisplayCommandSda, PinRole::SdCs),
              "UEDX48480040: GPIO47 is DisplayCommand SDA with TF CS as lifecycle follower");
static_assert(wiring_check::sharingDeclared(kActiveBoard.shared, 48,
                                            PinRole::DisplayCommandSck, PinRole::I2sWs),
              "UEDX48480040: GPIO48 is DisplayCommand SCK with I2S WS as lifecycle follower");

static_assert(kActiveBoardDefaults.audio.i2s_dout_gpio == 20 &&
                  kActiveBoardDefaults.audio.i2s_bclk_gpio == 19 &&
                  kActiveBoardDefaults.audio.i2s_lrck_gpio == 48,
              "UEDX48480040 default external DAC wiring: DOUT 20, BCLK 19, WS 48");
static_assert(kActiveBoardDefaults.display.startup_pclk_hz == 6000000u &&
                  kActiveBoardDefaults.display.runtime_pclk_hz == 12000000u &&
                  kActiveBoardDefaults.display.flash_guard_pclk_hz == 6000000u &&
                  kActiveBoardDefaults.display.promote_pclk_once_on_boot_to_main,
              "UEDX48480040 uses the D6 structure at 6 / 12 / 6 MHz (one promotion; see board_defaults.h)");
static_assert(kActiveBoardDefaults.touch.service_mode == TouchServiceMode::Polling,
              "UEDX48480040 FT6336U INT is not routed: touch service is polling");
static_assert(kActiveBoardDefaults.ui.profile == UiProfile::Square480,
              "UEDX48480040 selects the Square480 UI profile");

}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_BOARD_PROFILES_ESP32S3_UEDX48480040_BOARD_VALIDATE_PROFILE_H
