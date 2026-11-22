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

// Builtin LED vs active GPIO claims. Common for every board: the LED is an
// Exclusive role (BoardProfile.shared never lists it), so it must not land on any
// display/touch/I2S claim of the effective wiring (kWiring, Custom Wiring included).
// LED_BUILTIN may be a C++ constant and RGB_BUILTIN comes from pins_arduino.h, so
// this is static_assert in main.cpp (after Arduino.h), not #if in options.h.
// 255 means the indicator is off and no pin is compared.
// Встроенный LED против активных GPIO. Общая проверка для любой платы: LED — роль
// Exclusive (BoardProfile.shared её не перечисляет), поэтому он не должен попасть на
// пин дисплея/тача/I2S итоговой разводки. LED_BUILTIN может быть C++-константой, а
// RGB_BUILTIN приходит из pins_arduino.h, поэтому static_assert в main.cpp.
// 255 — индикатор выключен, пересечения не проверяются.
#ifdef __cplusplus
#include "soc/soc_caps.h"

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
constexpr bool hitsAnyClaim() {
    constexpr int led = physicalGpio();
    return led >= 0 && led <= 48 &&
           wiring_check::containsPin(wiring_check::kClaims, static_cast<uint8_t>(led));
}
}  // namespace builtin_led_check
}  // namespace yoradio

static_assert(!yoradio::builtin_led_check::hitsAnyClaim(),
              "YoRadio: builtin LED physical GPIO overlaps an active display, touch or I2S pin. "
              "Set USE_BUILTIN_LED false in src/myoptions.h or move the conflicting signal.");

// SD lines are not in the claim list yet (S07_bis-3c moves them into the profile).
// Until then the 4848 TF map is checked here by name.
// Линии SD пока не в списке требований (S07_bis-3c перенесёт их в профиль).
// До этого карта TF 4848 проверяется здесь по имени.
#if YORADIO_BOARD == YORADIO_BOARD_4848S040
#include "../platform/storage/schematic_candidate_4848s040.h"
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
#endif  // YORADIO_BOARD_4848S040
#endif  // __cplusplus

#endif


