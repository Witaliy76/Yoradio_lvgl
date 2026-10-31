// Compile-time resolver/validator of the selected board against the build family.
// Compile-time проверка выбранной платы на совместимость с build family.
// Ownership: platform/board.
// Included from src/src/core/options.h at the END of that file, because the legacy
// display-selector check below needs DSP_MODEL and DSP_ST7701, and options.h defines
// them itself after myoptions.h. Checks that need user/default values must not run
// before those values exist.
// Включается из src/src/core/options.h в КОНЦЕ файла: проверке legacy-селектора
// нужны DSP_MODEL и DSP_ST7701, которые options.h определяет сам после myoptions.h.
//
// sdkconfig.h is the authoritative build-family description. It is included here
// explicitly and not assumed: options.h is sometimes parsed before Arduino.h, so the
// CONFIG_* symbols are not guaranteed to be present yet.
// sdkconfig.h — авторитетное описание build family. Включается явно, а не
// предполагается: options.h иногда парсится раньше Arduino.h.
#ifndef YORADIO_PLATFORM_BOARD_VALIDATE_H
#define YORADIO_PLATFORM_BOARD_VALIDATE_H

#include "board_select.h"
#include "sdkconfig.h"

// ---------------------------------------------------------------------------
// Legacy display-selector consistency (preprocessor level)
// Согласованность legacy-селектора дисплея (уровень препроцессора)
// ---------------------------------------------------------------------------
#if !defined(DSP_MODEL)
#  error "YoRadio Board Contract: DSP_MODEL is not defined. board_validate.h must be included from src/src/core/options.h after the legacy display defaults."
#endif
#if !defined(DSP_ST7701)
#  error "YoRadio Board Contract: DSP_ST7701 is not defined. board_validate.h must be included from src/src/core/options.h after the legacy display defaults."
#endif
#if (YORADIO_BOARD == YORADIO_BOARD_4848S040) && (DSP_MODEL != DSP_ST7701)
#  error "YoRadio Board Contract: board/display selector mismatch. YORADIO_BOARD selects the 4848S040, whose panel is ST7701 RGB, but DSP_MODEL is not DSP_ST7701. Fix DSP_MODEL in src/myoptions.h or select the matching board."
#endif

#ifdef __cplusplus
#include <stdint.h>

namespace yoradio {

// Build-family facts, read from ESP-IDF's own configuration. No parallel YoRadio SoC
// or flash macro is introduced: sdkconfig stays the single source of build truth.
namespace build_family {

#if defined(CONFIG_IDF_TARGET_ESP32S3)
inline constexpr SocTarget kSoc = SocTarget::Esp32S3;
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
inline constexpr SocTarget kSoc = SocTarget::Esp32P4;
#else
inline constexpr SocTarget kSoc = SocTarget::Unknown;
#endif

#if defined(CONFIG_ESPTOOLPY_FLASHSIZE_32MB)
inline constexpr uint32_t kFlashBytes = 32u * 1024u * 1024u;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_16MB)
inline constexpr uint32_t kFlashBytes = 16u * 1024u * 1024u;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_8MB)
inline constexpr uint32_t kFlashBytes = 8u * 1024u * 1024u;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_4MB)
inline constexpr uint32_t kFlashBytes = 4u * 1024u * 1024u;
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_2MB)
inline constexpr uint32_t kFlashBytes = 2u * 1024u * 1024u;
#else
inline constexpr uint32_t kFlashBytes = 0u;  // unknown, never silently accepted
#endif

#if defined(CONFIG_SPIRAM)
inline constexpr bool      kHasPsram = true;
#  if defined(CONFIG_SPIRAM_MODE_OCT)
inline constexpr PsramMode kPsramMode = PsramMode::Octal;
#  else
inline constexpr PsramMode kPsramMode = PsramMode::Quad;
#  endif
#else
inline constexpr bool      kHasPsram  = false;
inline constexpr PsramMode kPsramMode = PsramMode::None;
#endif

}  // namespace build_family

static_assert(build_family::kSoc != SocTarget::Unknown,
    "YoRadio Board Contract: this build's SoC target is not recognised. Expected CONFIG_IDF_TARGET_ESP32S3 (or a target the Board Contract knows) from sdkconfig.");

static_assert(kActiveBoard.identity.build.soc == build_family::kSoc,
    "YoRadio Board Contract: the board selected by YORADIO_BOARD needs a different SoC than this PlatformIO environment builds for. Select a matching environment or a matching board.");

static_assert(build_family::kFlashBytes != 0u,
    "YoRadio Board Contract: this build's flash size is not recognised. Expected one of the CONFIG_ESPTOOLPY_FLASHSIZE_* symbols from sdkconfig.");

static_assert(build_family::kFlashBytes >= kActiveBoard.identity.build.min_flash_bytes,
    "YoRadio Board Contract: the board selected by YORADIO_BOARD needs more flash than this PlatformIO environment provides.");

static_assert(!kActiveBoard.identity.build.requires_psram || build_family::kHasPsram,
    "YoRadio Board Contract: the board selected by YORADIO_BOARD requires PSRAM, but PSRAM is not enabled in this build (CONFIG_SPIRAM).");

static_assert(kActiveBoard.identity.build.psram_mode == PsramMode::Any
                  || kActiveBoard.identity.build.psram_mode == build_family::kPsramMode,
    "YoRadio Board Contract: the board selected by YORADIO_BOARD requires a different PSRAM mode than this build configures (see CONFIG_SPIRAM_MODE_OCT and board_build.arduino.memory_type).");

// ---------------------------------------------------------------------------
// Effective wiring checks (S02-5 / S02-6)
// Проверки итоговой разводки (S02-5 / S02-6)
// ---------------------------------------------------------------------------
#if YORADIO_BOARD == YORADIO_BOARD_4848S040

// Factory profile numbers stay the accepted S02-4 / S02-5 baseline always.
// Заводской профиль всегда хранит принятый baseline S02-4 / S02-5.
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

// Temporary aliases always follow kWiring (factory or custom).
// Временные алиасы всегда следуют за kWiring (завод или custom).
static_assert(ST7701_B0 == kWiring.display.data_gpio[0],  "ST7701_B0 must follow kWiring");
static_assert(ST7701_B1 == kWiring.display.data_gpio[1],  "ST7701_B1 must follow kWiring");
static_assert(ST7701_B2 == kWiring.display.data_gpio[2],  "ST7701_B2 must follow kWiring");
static_assert(ST7701_B3 == kWiring.display.data_gpio[3],  "ST7701_B3 must follow kWiring");
static_assert(ST7701_B4 == kWiring.display.data_gpio[4],  "ST7701_B4 must follow kWiring");
static_assert(ST7701_G0 == kWiring.display.data_gpio[5],  "ST7701_G0 must follow kWiring");
static_assert(ST7701_G1 == kWiring.display.data_gpio[6],  "ST7701_G1 must follow kWiring");
static_assert(ST7701_G2 == kWiring.display.data_gpio[7],  "ST7701_G2 must follow kWiring");
static_assert(ST7701_G3 == kWiring.display.data_gpio[8],  "ST7701_G3 must follow kWiring");
static_assert(ST7701_G4 == kWiring.display.data_gpio[9],  "ST7701_G4 must follow kWiring");
static_assert(ST7701_G5 == kWiring.display.data_gpio[10], "ST7701_G5 must follow kWiring");
static_assert(ST7701_R0 == kWiring.display.data_gpio[11], "ST7701_R0 must follow kWiring");
static_assert(ST7701_R1 == kWiring.display.data_gpio[12], "ST7701_R1 must follow kWiring");
static_assert(ST7701_R2 == kWiring.display.data_gpio[13], "ST7701_R2 must follow kWiring");
static_assert(ST7701_R3 == kWiring.display.data_gpio[14], "ST7701_R3 must follow kWiring");
static_assert(ST7701_R4 == kWiring.display.data_gpio[15], "ST7701_R4 must follow kWiring");
static_assert(ST7701_HSYNC == kWiring.display.hsync_gpio,     "ST7701_HSYNC must follow kWiring");
static_assert(ST7701_VSYNC == kWiring.display.vsync_gpio,     "ST7701_VSYNC must follow kWiring");
static_assert(ST7701_DE    == kWiring.display.de_gpio,        "ST7701_DE must follow kWiring");
static_assert(ST7701_PCLK  == kWiring.display.pclk_gpio,      "ST7701_PCLK must follow kWiring");
static_assert(ST7701_CS    == kWiring.display.cmd_cs_gpio,    "ST7701_CS must follow kWiring");
static_assert(ST7701_SCK   == kWiring.display.cmd_sck_gpio,   "ST7701_SCK must follow kWiring");
static_assert(ST7701_SDA   == kWiring.display.cmd_sda_gpio,   "ST7701_SDA must follow kWiring");
static_assert(ST7701_BL    == kWiring.display.backlight_gpio, "ST7701_BL must follow kWiring");
static_assert(TS_SDA == kWiring.touch.i2c_sda_gpio, "TS_SDA must follow kWiring");
static_assert(TS_SCL == kWiring.touch.i2c_scl_gpio, "TS_SCL must follow kWiring");
static_assert(TS_INT == kWiring.touch.int_gpio,     "TS_INT must follow kWiring");
static_assert(TS_RST == kWiring.touch.reset_gpio,   "TS_RST must follow kWiring");
static_assert(TS_SDA != 33 && TS_SCL != 32 && TS_INT != 21 && TS_RST != 25,
              "TS_* fallback from options.h leaked");

#if !YORADIO_CUSTOM_WIRING
// Factory mode: effective wiring is identical to the accepted baseline.
// Штатный режим: итоговая разводка совпадает с принятым baseline.
static_assert(kWiring.display.data_gpio[0]  == 4  && ST7701_B0 == 4,  "S02-5 B0 is GPIO4");
static_assert(kWiring.display.data_gpio[1]  == 5  && ST7701_B1 == 5,  "S02-5 B1 is GPIO5");
static_assert(kWiring.display.data_gpio[2]  == 6  && ST7701_B2 == 6,  "S02-5 B2 is GPIO6");
static_assert(kWiring.display.data_gpio[3]  == 7  && ST7701_B3 == 7,  "S02-5 B3 is GPIO7");
static_assert(kWiring.display.data_gpio[4]  == 15 && ST7701_B4 == 15, "S02-5 B4 is GPIO15");
static_assert(kWiring.display.data_gpio[5]  == 8  && ST7701_G0 == 8,  "S02-5 G0 is GPIO8");
static_assert(kWiring.display.data_gpio[6]  == 20 && ST7701_G1 == 20, "S02-5 G1 is GPIO20");
static_assert(kWiring.display.data_gpio[7]  == 3  && ST7701_G2 == 3,  "S02-5 G2 is GPIO3");
static_assert(kWiring.display.data_gpio[8]  == 46 && ST7701_G3 == 46, "S02-5 G3 is GPIO46");
static_assert(kWiring.display.data_gpio[9]  == 9  && ST7701_G4 == 9,  "S02-5 G4 is GPIO9");
static_assert(kWiring.display.data_gpio[10] == 10 && ST7701_G5 == 10, "S02-5 G5 is GPIO10");
static_assert(kWiring.display.data_gpio[11] == 11 && ST7701_R0 == 11, "S02-5 R0 is GPIO11");
static_assert(kWiring.display.data_gpio[12] == 12 && ST7701_R1 == 12, "S02-5 R1 is GPIO12");
static_assert(kWiring.display.data_gpio[13] == 13 && ST7701_R2 == 13, "S02-5 R2 is GPIO13");
static_assert(kWiring.display.data_gpio[14] == 14 && ST7701_R3 == 14, "S02-5 R3 is GPIO14");
static_assert(kWiring.display.data_gpio[15] == 0  && ST7701_R4 == 0,  "S02-5 R4 is GPIO0");
static_assert(kWiring.display.hsync_gpio     == 16 && ST7701_HSYNC == 16, "S02-5 HSYNC is GPIO16");
static_assert(kWiring.display.vsync_gpio     == 17 && ST7701_VSYNC == 17, "S02-5 VSYNC is GPIO17");
static_assert(kWiring.display.de_gpio        == 18 && ST7701_DE    == 18, "S02-5 DE is GPIO18");
static_assert(kWiring.display.pclk_gpio      == 21 && ST7701_PCLK  == 21, "S02-5 PCLK is GPIO21");
static_assert(kWiring.display.cmd_cs_gpio    == 39 && ST7701_CS    == 39, "S02-5 command CS is GPIO39");
static_assert(kWiring.display.cmd_sck_gpio   == 48 && ST7701_SCK   == 48, "S02-5 command SCK is GPIO48");
static_assert(kWiring.display.cmd_sda_gpio   == 47 && ST7701_SDA   == 47, "S02-5 command SDA is GPIO47");
static_assert(kWiring.display.backlight_gpio == 38 && ST7701_BL    == 38, "S02-5 backlight is GPIO38");
static_assert(kWiring.touch.i2c_sda_gpio == 19 && TS_SDA == 19, "S02-5 touch SDA is GPIO19");
static_assert(kWiring.touch.i2c_scl_gpio == 45 && TS_SCL == 45, "S02-5 touch SCL is GPIO45");
static_assert(kWiring.touch.int_gpio   == kPinNotConnected && TS_INT == kPinNotConnected, "S02-5 touch INT is not connected");
static_assert(kWiring.touch.reset_gpio == kPinNotConnected && TS_RST == kPinNotConnected, "S02-5 touch RST is not connected");
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

// ---- Required lines may not be disconnected; INT/RST may stay at 255 ----
// ---- Обязательные линии нельзя отключать; INT/RST могут остаться 255 ----
static_assert(kWiring.display.data_gpio[0]  != kPinNotConnected, "DISP_B0 cannot be disconnected");
static_assert(kWiring.display.data_gpio[1]  != kPinNotConnected, "DISP_B1 cannot be disconnected");
static_assert(kWiring.display.data_gpio[2]  != kPinNotConnected, "DISP_B2 cannot be disconnected");
static_assert(kWiring.display.data_gpio[3]  != kPinNotConnected, "DISP_B3 cannot be disconnected");
static_assert(kWiring.display.data_gpio[4]  != kPinNotConnected, "DISP_B4 cannot be disconnected");
static_assert(kWiring.display.data_gpio[5]  != kPinNotConnected, "DISP_G0 cannot be disconnected");
static_assert(kWiring.display.data_gpio[6]  != kPinNotConnected, "DISP_G1 cannot be disconnected");
static_assert(kWiring.display.data_gpio[7]  != kPinNotConnected, "DISP_G2 cannot be disconnected");
static_assert(kWiring.display.data_gpio[8]  != kPinNotConnected, "DISP_G3 cannot be disconnected");
static_assert(kWiring.display.data_gpio[9]  != kPinNotConnected, "DISP_G4 cannot be disconnected");
static_assert(kWiring.display.data_gpio[10] != kPinNotConnected, "DISP_G5 cannot be disconnected");
static_assert(kWiring.display.data_gpio[11] != kPinNotConnected, "DISP_R0 cannot be disconnected");
static_assert(kWiring.display.data_gpio[12] != kPinNotConnected, "DISP_R1 cannot be disconnected");
static_assert(kWiring.display.data_gpio[13] != kPinNotConnected, "DISP_R2 cannot be disconnected");
static_assert(kWiring.display.data_gpio[14] != kPinNotConnected, "DISP_R3 cannot be disconnected");
static_assert(kWiring.display.data_gpio[15] != kPinNotConnected, "DISP_R4 cannot be disconnected");
static_assert(kWiring.display.hsync_gpio     != kPinNotConnected, "DISP_HSYNC cannot be disconnected");
static_assert(kWiring.display.vsync_gpio     != kPinNotConnected, "DISP_VSYNC cannot be disconnected");
static_assert(kWiring.display.de_gpio        != kPinNotConnected, "DISP_DE cannot be disconnected");
static_assert(kWiring.display.pclk_gpio      != kPinNotConnected, "DISP_PCLK cannot be disconnected");
static_assert(kWiring.display.cmd_cs_gpio    != kPinNotConnected, "DISP_CS cannot be disconnected");
static_assert(kWiring.display.cmd_sck_gpio   != kPinNotConnected, "DISP_SCK cannot be disconnected");
static_assert(kWiring.display.cmd_sda_gpio   != kPinNotConnected, "DISP_SDA cannot be disconnected");
static_assert(kWiring.display.backlight_gpio != kPinNotConnected, "DISP_BL cannot be disconnected");
static_assert(kWiring.touch.i2c_sda_gpio != kPinNotConnected, "TOUCH_SDA cannot be disconnected");
static_assert(kWiring.touch.i2c_scl_gpio != kPinNotConnected, "TOUCH_SCL cannot be disconnected");

namespace wiring_check {

// ESP32-S3 with octal flash/PSRAM: GPIO 22-25 do not exist; 26-37 serve the
// flash/PSRAM bus on the N16R8 module. 255 means not connected.
// ESP32-S3 с octal flash/PSRAM: GPIO 22-25 нет; 26-37 заняты шиной на N16R8.
inline constexpr bool isUsableGpio(uint8_t pin) {
    if (pin == kPinNotConnected) {
        return true;
    }
    if (pin > 48u) {
        return false;
    }
    if (pin >= 22u && pin <= 37u) {
        return false;
    }
    return true;
}

static_assert(isUsableGpio(kWiring.display.data_gpio[0]),  "DISP_B0 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[1]),  "DISP_B1 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[2]),  "DISP_B2 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[3]),  "DISP_B3 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[4]),  "DISP_B4 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[5]),  "DISP_G0 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[6]),  "DISP_G1 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[7]),  "DISP_G2 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[8]),  "DISP_G3 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[9]),  "DISP_G4 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[10]), "DISP_G5 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[11]), "DISP_R0 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[12]), "DISP_R1 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[13]), "DISP_R2 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[14]), "DISP_R3 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.data_gpio[15]), "DISP_R4 uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.hsync_gpio),     "DISP_HSYNC uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.vsync_gpio),     "DISP_VSYNC uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.de_gpio),        "DISP_DE uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.pclk_gpio),      "DISP_PCLK uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.cmd_cs_gpio),    "DISP_CS uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.cmd_sck_gpio),   "DISP_SCK uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.cmd_sda_gpio),   "DISP_SDA uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.display.backlight_gpio), "DISP_BL uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.touch.i2c_sda_gpio), "TOUCH_SDA uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.touch.i2c_scl_gpio), "TOUCH_SCL uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.touch.int_gpio),     "TOUCH_INT uses an invalid ESP32-S3 GPIO");
static_assert(isUsableGpio(kWiring.touch.reset_gpio),   "TOUCH_RST uses an invalid ESP32-S3 GPIO");

// Simultaneous claimants: display + touch (+ INT/RST if wired) + active I2S.
// Not claimed here: USB (unused in stock), BOOT/WAKE lifecycle share with R4,
// undocumented SD lines, Arduino RGB LED on GPIO48 (migration debt).
// Одновременно занятые: дисплей + тач (+ INT/RST) + активный I2S.
// Не входят: USB, BOOT/WAKE (lifecycle с R4), недокументированный SD, RGB LED.
struct ClaimList {
    uint8_t pin[32];
    uint8_t count;
};

inline constexpr ClaimList buildClaims() {
    ClaimList c{};
    auto push = [&](uint8_t pin) {
        if (pin == kPinNotConnected) {
            return;
        }
        c.pin[c.count++] = pin;
    };
    for (uint8_t i = 0; i < 16; ++i) {
        push(kWiring.display.data_gpio[i]);
    }
    push(kWiring.display.hsync_gpio);
    push(kWiring.display.vsync_gpio);
    push(kWiring.display.de_gpio);
    push(kWiring.display.pclk_gpio);
    push(kWiring.display.cmd_cs_gpio);
    push(kWiring.display.cmd_sck_gpio);
    push(kWiring.display.cmd_sda_gpio);
    push(kWiring.display.backlight_gpio);
    push(kWiring.touch.i2c_sda_gpio);
    push(kWiring.touch.i2c_scl_gpio);
    push(kWiring.touch.int_gpio);
    push(kWiring.touch.reset_gpio);
#if defined(I2S_DOUT) && (I2S_DOUT != 255)
    push(static_cast<uint8_t>(I2S_DOUT));
#endif
#if defined(I2S_BCLK) && (I2S_BCLK != 255)
    push(static_cast<uint8_t>(I2S_BCLK));
#endif
#if defined(I2S_LRC) && (I2S_LRC != 255)
    push(static_cast<uint8_t>(I2S_LRC));
#endif
    return c;
}

inline constexpr bool hasDuplicateGpio(const ClaimList& claims) {
    for (uint8_t i = 0; i < claims.count; ++i) {
        for (uint8_t j = static_cast<uint8_t>(i + 1u); j < claims.count; ++j) {
            if (claims.pin[i] == claims.pin[j]) {
                return true;
            }
        }
    }
    return false;
}

inline constexpr ClaimList kClaims = buildClaims();
static_assert(!hasDuplicateGpio(kClaims),
              "YoRadio Custom Wiring: two active signals share the same GPIO. "
              "Check DISP_*/TOUCH_* overrides against each other and against I2S_DOUT/BCLK/LRC in src/myoptions.h.");

}  // namespace wiring_check

#endif  // YORADIO_BOARD == YORADIO_BOARD_4848S040

}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_BOARD_VALIDATE_H
