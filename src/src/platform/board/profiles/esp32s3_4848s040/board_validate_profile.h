// Profile-specific wiring/lifecycle assertions of the ESP32-S3 4848S040 board.
// Профильные проверки разводки и жизненного цикла платы ESP32-S3 4848S040.
//
// Purpose / Назначение:
//   Everything board_validate.h must NOT know about a concrete board lives here: the
//   factory GPIO numbers, "effective wiring equals factory when Custom Wiring is off",
//   the 480x480 panel / GT911 address / RESX, and the lifecycle-shared pins (BOOT = R4,
//   native USB = touch SDA / RGB G1, onboard audio needs hardware rework). Generic
//   checks (required lines, usable GPIO, duplicates, I2S, build family) stay in the
//   common board_validate.h and run for every board.
//   Здесь всё, что board_validate.h не должен знать о конкретной плате: заводские
//   номера GPIO, "итоговая разводка равна заводской при выключенном Custom Wiring",
//   панель 480x480 / адрес GT911 / RESX и общие по жизненному циклу пины (BOOT = R4,
//   native USB = SDA тача / G1 RGB, штатный аудиотракт требует доработки платы).
//   Общие проверки остаются в board_validate.h и выполняются для любой платы.
//
// Ownership / lifecycle: platform/board, compile-time only. Not included directly:
// board_select.h names this file in YORADIO_BOARD_PROFILE_VALIDATE_HEADER in the same
// YORADIO_BOARD branch that selects the profile (the only selector), and
// board_validate.h includes it by that name. The assertions were moved verbatim from
// board_validate.h (S07_bis-3a); only the redundant YORADIO_BOARD gate was dropped,
// because inclusion itself is the gate.
// Владение: platform/board, только время компиляции. Напрямую не подключается:
// board_select.h задаёт имя файла в YORADIO_BOARD_PROFILE_VALIDATE_HEADER в той же
// ветке YORADIO_BOARD, что выбирает профиль (единственный селектор), а board_validate.h
// включает файл по этому имени. Проверки перенесены дословно из board_validate.h
// (S07_bis-3a); убран только лишний гейт YORADIO_BOARD: гейтом служит само включение.
#ifndef YORADIO_PLATFORM_BOARD_PROFILES_ESP32S3_4848S040_BOARD_VALIDATE_PROFILE_H
#define YORADIO_PLATFORM_BOARD_PROFILES_ESP32S3_4848S040_BOARD_VALIDATE_PROFILE_H

#include "../../board_select.h"

// Legacy display selector must agree with this board's ST7701 panel (preprocessor level;
// DSP_MODEL / DSP_ST7701 existence is checked by board_validate.h before inclusion).
// Старый селектор дисплея должен соответствовать ST7701 этой платы (уровень препроцессора;
// наличие DSP_MODEL / DSP_ST7701 проверяет board_validate.h до включения).
#if (DSP_MODEL != DSP_ST7701)
#  error "YoRadio Board Contract: board/display selector mismatch. YORADIO_BOARD selects the 4848S040, whose panel is ST7701 RGB, but DSP_MODEL is not DSP_ST7701. Fix DSP_MODEL in src/myoptions.h or select the matching board."
#endif

#ifdef __cplusplus
#include <stdint.h>

namespace yoradio {
// Factory profile numbers stay the accepted S02-4 / S02-5 baseline always.
// Заводской профиль сохраняет принятую разводку S02-4 / S02-5.
static_assert(kActiveBoard.display.data_gpio[0]  == 4,  "factory B0 is GPIO4");
static_assert(kActiveBoard.display.data_gpio[1]  == 5,  "factory B1 is GPIO5");
static_assert(kActiveBoard.display.data_gpio[2]  == 6,  "factory B2 is GPIO6");
static_assert(kActiveBoard.display.data_gpio[3]  == 7,  "factory B3 is GPIO7");
static_assert(kActiveBoard.display.data_gpio[4]  == 15, "factory B4 is GPIO15");
static_assert(kActiveBoard.display.data_gpio[5]  == 8,  "factory G0 is GPIO8");
static_assert(kActiveBoard.display.data_gpio[6]  == 20, "factory G1 is GPIO20");
static_assert(kActiveBoard.display.data_gpio[7]  == 3,  "factory G2 is GPIO3");
static_assert(kActiveBoard.display.data_gpio[8]  == 46, "factory G3 is GPIO46");
static_assert(kActiveBoard.display.data_gpio[9]  == 9,  "factory G4 is GPIO9");
static_assert(kActiveBoard.display.data_gpio[10] == 10, "factory G5 is GPIO10");
static_assert(kActiveBoard.display.data_gpio[11] == 11, "factory R0 is GPIO11");
static_assert(kActiveBoard.display.data_gpio[12] == 12, "factory R1 is GPIO12");
static_assert(kActiveBoard.display.data_gpio[13] == 13, "factory R2 is GPIO13");
static_assert(kActiveBoard.display.data_gpio[14] == 14, "factory R3 is GPIO14");
static_assert(kActiveBoard.display.data_gpio[15] == 0,  "factory R4 is GPIO0");
static_assert(kActiveBoard.display.hsync_gpio     == 16, "factory HSYNC is GPIO16");
static_assert(kActiveBoard.display.vsync_gpio     == 17, "factory VSYNC is GPIO17");
static_assert(kActiveBoard.display.de_gpio        == 18, "factory DE is GPIO18");
static_assert(kActiveBoard.display.pclk_gpio      == 21, "factory PCLK is GPIO21");
static_assert(kActiveBoard.display.cmd_cs_gpio    == 39, "factory command CS is GPIO39");
static_assert(kActiveBoard.display.cmd_sck_gpio   == 48, "factory command SCK is GPIO48");
static_assert(kActiveBoard.display.cmd_sda_gpio   == 47, "factory command SDA is GPIO47");
static_assert(kActiveBoard.display.backlight_gpio == 38, "factory backlight is GPIO38");
static_assert(kActiveBoard.touch.i2c_sda_gpio == 19, "factory touch SDA is GPIO19");
static_assert(kActiveBoard.touch.i2c_scl_gpio == 45, "factory touch SCL is GPIO45");
static_assert(kActiveBoard.touch.int_gpio   == kPinNotConnected, "factory touch INT is not connected");
static_assert(kActiveBoard.touch.reset_gpio == kPinNotConnected, "factory touch RST is not connected");
static_assert(kPinNotConnected == 255, "the not-connected sentinel stays 255");

#if !YORADIO_CUSTOM_WIRING
// Factory mode: effective wiring is identical to the accepted baseline.
// Штатный режим: итоговая разводка совпадает с принятой заводской.
static_assert(kWiring.display.data_gpio[0]  == 4,  "S02-5 B0 is GPIO4");
static_assert(kWiring.display.data_gpio[1]  == 5,  "S02-5 B1 is GPIO5");
static_assert(kWiring.display.data_gpio[2]  == 6,  "S02-5 B2 is GPIO6");
static_assert(kWiring.display.data_gpio[3]  == 7,  "S02-5 B3 is GPIO7");
static_assert(kWiring.display.data_gpio[4]  == 15, "S02-5 B4 is GPIO15");
static_assert(kWiring.display.data_gpio[5]  == 8,  "S02-5 G0 is GPIO8");
static_assert(kWiring.display.data_gpio[6]  == 20, "S02-5 G1 is GPIO20");
static_assert(kWiring.display.data_gpio[7]  == 3,  "S02-5 G2 is GPIO3");
static_assert(kWiring.display.data_gpio[8]  == 46, "S02-5 G3 is GPIO46");
static_assert(kWiring.display.data_gpio[9]  == 9,  "S02-5 G4 is GPIO9");
static_assert(kWiring.display.data_gpio[10] == 10, "S02-5 G5 is GPIO10");
static_assert(kWiring.display.data_gpio[11] == 11, "S02-5 R0 is GPIO11");
static_assert(kWiring.display.data_gpio[12] == 12, "S02-5 R1 is GPIO12");
static_assert(kWiring.display.data_gpio[13] == 13, "S02-5 R2 is GPIO13");
static_assert(kWiring.display.data_gpio[14] == 14, "S02-5 R3 is GPIO14");
static_assert(kWiring.display.data_gpio[15] == 0,  "S02-5 R4 is GPIO0");
static_assert(kWiring.display.hsync_gpio     == 16, "S02-5 HSYNC is GPIO16");
static_assert(kWiring.display.vsync_gpio     == 17, "S02-5 VSYNC is GPIO17");
static_assert(kWiring.display.de_gpio        == 18, "S02-5 DE is GPIO18");
static_assert(kWiring.display.pclk_gpio      == 21, "S02-5 PCLK is GPIO21");
static_assert(kWiring.display.cmd_cs_gpio    == 39, "S02-5 command CS is GPIO39");
static_assert(kWiring.display.cmd_sck_gpio   == 48, "S02-5 command SCK is GPIO48");
static_assert(kWiring.display.cmd_sda_gpio   == 47, "S02-5 command SDA is GPIO47");
static_assert(kWiring.display.backlight_gpio == 38, "S02-5 backlight is GPIO38");
static_assert(kWiring.touch.i2c_sda_gpio == 19, "S02-5 touch SDA is GPIO19");
static_assert(kWiring.touch.i2c_scl_gpio == 45, "S02-5 touch SCL is GPIO45");
static_assert(kWiring.touch.int_gpio == kPinNotConnected,
              "S02-5 touch INT is not connected");
static_assert(kWiring.touch.reset_gpio == kPinNotConnected,
              "S02-5 touch RST is not connected");
#endif  // !YORADIO_CUSTOM_WIRING

static_assert(kActiveBoard.display.native_width  == 480, "4848S040 panel is 480 wide");
static_assert(kActiveBoard.display.native_height == 480, "4848S040 panel is 480 high");
static_assert(kActiveBoard.display.panel_reset_gpio == kPinNotConnected,
              "4848S040 panel RESX is not routed to the SoC");
static_assert(kActiveBoard.touch.i2c_address == 0x5D, "GT911 primary address");
// Factory lifecycle share: BOOT button and RGB R4 are the same physical pin.
// Custom Wiring may move R4; the button fact stays on the factory profile.
// Заводское совпадение BOOT и R4. Custom Wiring может перенести R4; кнопка
// остаётся фактом заводского профиля.
static_assert(kActiveBoard.power.boot_button_gpio == kActiveBoard.display.data_gpio[15],
              "on this board the BOOT button and RGB R4 are the same lifecycle-shared pin");
static_assert(kActiveBoard.usb.soc_native_usb_dm_gpio == kActiveBoard.touch.i2c_sda_gpio,
              "stock: native USB DM and touch SDA share GPIO19; USB stays unused");
static_assert(kActiveBoard.usb.soc_native_usb_dp_gpio == kActiveBoard.display.data_gpio[6],
              "stock: native USB DP and RGB G1 share GPIO20; USB stays unused");
static_assert(!kActiveBoard.usb.native_usb_pins_free_in_stock_wiring,
              "stock wiring uses both native-USB pins (touch SDA and RGB G1)");
static_assert(kActiveBoard.audio.onboard_output_requires_hw_rework,
              "the onboard mono path is strap-selected, so it is never software-selectable");
// S07 TF map (moved from storage/schematic_candidate_4848s040.h in S07_bis-3c).
// Карта TF S07 (перенесена из schematic_candidate_4848s040.h в S07_bis-3c).
static_assert(kActiveBoard.storage.onboard_sd_present, "4848S040 has an onboard microSD slot");
static_assert(kActiveBoard.storage.sd_clk_gpio == 48 && kActiveBoard.storage.sd_cmd_gpio == 47 &&
                  kActiveBoard.storage.sd_dat0_gpio == 41 && kActiveBoard.storage.sd_cs_gpio == 42,
              "4848S040 TF map: CLK 48, CMD 47, DAT0 41, CS 42");
static_assert(kActiveBoard.storage.sd_pin_evidence == PinEvidence::SchematicCandidate,
              "4848S040 TF map is SchematicCandidate; DeviceVerified needs the hardware gate");
static_assert(!kActiveBoard.storage.sd_card_detect_present,
              "4848S040 ties card detect to GND: no CD GPIO");
static_assert(!kActiveBoard.storage.sd_supports_sdmmc_4bit,
              "4848S040 has no MCU DAT1/DAT2: 4-bit SDMMC is unavailable");
static_assert(kActiveBoard.storage.sd_cmd_gpio == kActiveBoard.display.cmd_sda_gpio &&
                  kActiveBoard.storage.sd_clk_gpio == kActiveBoard.display.cmd_sck_gpio,
              "4848S040: TF CMD/CLK are the display command SDA/SCK lines");

// S07: TF CMD/CLK are the ST7701 command SDA/SCK lines; the profile must declare it.
// S07: TF CMD/CLK — это линии SDA/SCK команд ST7701; профиль обязан это объявить.
static_assert(kActiveBoard.shared.count == 2,
              "4848S040 declares exactly two shared pins: GPIO47 and GPIO48");
static_assert(wiring_check::sharingDeclared(kActiveBoard.shared, 47,
                                            PinRole::DisplayCommandSda, PinRole::SdCmd),
              "4848S040: GPIO47 is DisplayCommand SDA with TF CMD as lifecycle follower");
static_assert(wiring_check::sharingDeclared(kActiveBoard.shared, 48,
                                            PinRole::DisplayCommandSck, PinRole::SdClk),
              "4848S040: GPIO48 is DisplayCommand SCK with TF CLK as lifecycle follower");

}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_BOARD_PROFILES_ESP32S3_4848S040_BOARD_VALIDATE_PROFILE_H