// SCHEMATIC CONFIRMED TF candidate map for ESP32-4848S040 (S07-0A / S07-1).
// Кандидатная разводка TF по схеме ESP32-4848S040 (S07-0A / S07-1).
//
// Source sheets (stronger than myoptions comments / third-party tables):
//   https://github.com/gys2005/ESP32-4848S040/blob/main/Photo_02.png
//   https://github.com/gys2005/ESP32-4848S040/blob/main/Photo_03.png
//
// This is NOT BoardProfile wiring and MUST NOT be treated as DEVICE VERIFIED.
// Stock firmware does not apply these values. The explicit engineering env
// reuses this map under accepted B003-B005 functional evidence, not electrical proof.
// Это НЕ разводка BoardProfile и НЕ DEVICE VERIFIED. Только инженерный env
// применяет эту карту по функциональным B003-B005, без подмены измерений.
#ifndef YORADIO_PLATFORM_STORAGE_SCHEMATIC_CANDIDATE_4848S040_H
#define YORADIO_PLATFORM_STORAGE_SCHEMATIC_CANDIDATE_4848S040_H

#include <stdint.h>

#include "storage_types.h"

namespace yoradio {
namespace storage_schematic_4848s040 {

inline constexpr StoragePinEvidence kPinEvidence = StoragePinEvidence::SchematicCandidate;

// TF slot → ESP32 (Photo_02). DAT1/DAT2 are pull-up only, not MCU-routed.
// Разъём TF → ESP32. DAT1/DAT2 только на подтяжках, на MCU не заведены.
inline constexpr uint8_t kTfDat3CsGpio = 42;  // DAT3 / SPI CS
inline constexpr uint8_t kTfCmdGpio    = 47;  // CMD / SPI MOSI; shares DisplayCommand SDA
inline constexpr uint8_t kTfClkGpio    = 48;  // CLK / SPI SCK; shares DisplayCommand SCK
inline constexpr uint8_t kTfDat0Gpio   = 41;  // DAT0 / SPI MISO (not "D1")

inline constexpr uint8_t kLcdCmdCsGpio = 39;  // LCD_CS — stays with display; not an SD data line

// Card Detect is hard-tied to GND on the schematic — no CD GPIO exists.
// Card Detect на схеме жёстко на GND — отдельного CD GPIO нет.
inline constexpr bool kCardDetectGpioPresent = false;

// Width: SPI or SDMMC 1-bit only (DAT1/DAT2 not on the SoC).
// Ширина: только SPI или SDMMC 1-bit (DAT1/DAT2 не на SoC).
inline constexpr bool kSupportsSdmmc4Bit = false;

}  // namespace storage_schematic_4848s040
}  // namespace yoradio

#endif  // YORADIO_PLATFORM_STORAGE_SCHEMATIC_CANDIDATE_4848S040_H
