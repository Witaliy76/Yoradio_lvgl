#ifndef optionschecker_h
#define optionschecker_h

#if REAL_LEDBUILTIN==TFT_RST
#  error LED_BUILTIN IS THE SAME AS TFT_RST. Check it in myoptions.h
#endif

#if REAL_LEDBUILTIN==VS1053_RST
#  error LED_BUILTIN IS THE SAME AS VS1053_RST. Check it in myoptions.h
#endif

#if (I2S_DOUT!=255) && (VS1053_CS!=255)
#  error YOU MUST CHOOSE BETWEEN I2S DAC AND VS1053 BY DISABLING THE SECOND MODULE IN THE myoptions.h
#endif

#if !(defined(ARDUINO_ESP32_DEV) || defined(ARDUINO_ESP32S3_DEV) || defined(ARDUINO_ESP32C3_DEV))
#  error ONLY MODULES "ESP32 Dev Module", "ESP32 Wrover Module" AND "ESP32 S3 Dev Module" ARE SUPPORTED. PLEASE SELECT ONE OF THEM IN THE MENU >> TOOLS >> BOARD
#endif

// 4848S040 only. LED_BUILTIN may be a C++ constant, so this is static_assert, not #if.
// 255 means the indicator is off and no pin is compared. Included from main.cpp after options.h.
// Только 4848S040. LED_BUILTIN может быть C++-константой, поэтому static_assert, не #if.
// 255 — индикатор выключен, пересечения не проверяются. Подключается из main.cpp после options.h.
#ifdef __cplusplus
#if YORADIO_BOARD == YORADIO_BOARD_4848S040
#include "soc/soc_caps.h"
#include "../platform/storage/schematic_candidate_4848s040.h"

namespace yoradio {
namespace builtin_led_check {
constexpr int physicalGpio() {
    constexpr int raw = static_cast<int>(REAL_LEDBUILTIN);
    if (raw == 255) return -1;
#if defined(RGB_BUILTIN)
    if (raw >= static_cast<int>(SOC_GPIO_PIN_COUNT)) return raw - static_cast<int>(SOC_GPIO_PIN_COUNT);
#endif
    return raw;
}
constexpr bool hits(int gpio) {
    constexpr int led = physicalGpio();
    return led >= 0 && led == gpio;
}
}
}

static_assert(!yoradio::builtin_led_check::hits(static_cast<int>(yoradio::kWiring.display.cmd_sck_gpio)),
              "YoRadio 4848S040: builtin LED physical GPIO overlaps DisplayCommand SCK.");
static_assert(!yoradio::builtin_led_check::hits(static_cast<int>(yoradio::kWiring.display.cmd_sda_gpio)),
              "YoRadio 4848S040: builtin LED physical GPIO overlaps DisplayCommand SDA.");
static_assert(!yoradio::builtin_led_check::hits(static_cast<int>(yoradio::kWiring.display.cmd_cs_gpio)),
              "YoRadio 4848S040: builtin LED physical GPIO overlaps DisplayCommand CS.");
static_assert(!yoradio::builtin_led_check::hits(static_cast<int>(yoradio::storage_schematic_4848s040::kTfClkGpio)),
              "YoRadio 4848S040: builtin LED physical GPIO overlaps SD SCK.");
static_assert(!yoradio::builtin_led_check::hits(static_cast<int>(yoradio::storage_schematic_4848s040::kTfCmdGpio)),
              "YoRadio 4848S040: builtin LED physical GPIO overlaps SD MOSI.");
#ifdef SD_SCK
static_assert(!yoradio::builtin_led_check::hits(static_cast<int>(SD_SCK)),
              "YoRadio 4848S040: builtin LED physical GPIO overlaps SD_SCK.");
#endif
#ifdef SD_MOSI
static_assert(!yoradio::builtin_led_check::hits(static_cast<int>(SD_MOSI)),
              "YoRadio 4848S040: builtin LED physical GPIO overlaps SD_MOSI.");
#endif
#ifdef DISP_SCK
static_assert(!yoradio::builtin_led_check::hits(static_cast<int>(DISP_SCK)),
              "YoRadio 4848S040: builtin LED physical GPIO overlaps DISP_SCK.");
#endif
#ifdef DISP_SDA
static_assert(!yoradio::builtin_led_check::hits(static_cast<int>(DISP_SDA)),
              "YoRadio 4848S040: builtin LED physical GPIO overlaps DISP_SDA.");
#endif
#endif  // YORADIO_BOARD_4848S040
#endif  // __cplusplus

#endif


