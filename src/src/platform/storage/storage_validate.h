// Compile-time Storage Contract checks (S07-1).
// Compile-time проверки контракта Storage (S07-1).
//
// Included from options.h AFTER board_validate.h so SDC_CS / I2S / IR / MUTE /
// WAKE macros and kWiring exist. No runtime mount or GPIO handoff.
// Подключается из options.h ПОСЛЕ board_validate.h. Без runtime mount/handoff.
#ifndef YORADIO_PLATFORM_STORAGE_STORAGE_VALIDATE_H
#define YORADIO_PLATFORM_STORAGE_STORAGE_VALIDATE_H

#include "resolved_storage_config.h"

// ---------------------------------------------------------------------------
// Legacy SDFS is never an enable mechanism, including engineering builds.
// Legacy SDFS не служит способом включения, в том числе в инженерной сборке.
// ---------------------------------------------------------------------------
#if defined(SDC_CS) && (SDC_CS != 255)
#  error "YoRadio Storage: keep SDC_CS=255; legacy SDManager is forbidden. The 4848S040 Owner is not USE_SD."
#endif
// sdmanager.cpp has a legacy empty compile marker; it is not a runtime enable.
// Legacy TU задаёт пустой маркер USE_SD; он не включает runtime SD.
#if YORADIO_S07_STORAGE_ENABLED && defined(USE_SD) && (USE_SD + 0)
#error "Engineering Storage requires USE_SD=false; do not activate legacy SDManager."
#endif

#ifdef __cplusplus

namespace yoradio {
namespace storage_validate {

// ESP32-S3 usable GPIO — same rule as board_validate wiring_check::isUsableGpio.
// Допустимый GPIO ESP32-S3 — то же правило, что в board_validate.
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

inline constexpr bool pinsEqual(uint8_t a, uint8_t b) {
    return a != kPinNotConnected && a == b;
}

// True when `pin` is claimed by effective display/touch wiring or live I2S macros.
// DisplayCommand SCK/SDA may be excluded for the lifecycle-share exception.
// true, если пин занят итоговой разводкой дисплея/тача или живыми I2S.
// SCK/SDA DisplayCommand можно исключить для lifecycle-share.
inline constexpr bool conflictsWithExclusiveRuntimePins(uint8_t pin,
                                                        bool allow_display_command_share) {
    if (pin == kPinNotConnected) {
        return false;
    }
    const DisplayWiring& d = kWiring.display;
    const TouchWiring& t = kWiring.touch;

    for (uint8_t i = 0; i < 16; ++i) {
        if (pinsEqual(pin, d.data_gpio[i])) {
            return true;
        }
    }
    if (pinsEqual(pin, d.hsync_gpio) || pinsEqual(pin, d.vsync_gpio) ||
        pinsEqual(pin, d.de_gpio) || pinsEqual(pin, d.pclk_gpio) ||
        pinsEqual(pin, d.cmd_cs_gpio) || pinsEqual(pin, d.backlight_gpio)) {
        return true;
    }
    if (!allow_display_command_share) {
        if (pinsEqual(pin, d.cmd_sck_gpio) || pinsEqual(pin, d.cmd_sda_gpio)) {
            return true;
        }
    } else {
        // Lifecycle share is only legal for the command clock/data pair, never CS.
        // Lifecycle-share разрешён только для командных SCK/SDA, никогда для CS.
        if (pinsEqual(pin, d.cmd_cs_gpio)) {
            return true;
        }
    }
    if (pinsEqual(pin, t.i2c_sda_gpio) || pinsEqual(pin, t.i2c_scl_gpio) ||
        pinsEqual(pin, t.int_gpio) || pinsEqual(pin, t.reset_gpio)) {
        return true;
    }
#if defined(I2S_DOUT) && (I2S_DOUT != 255)
    if (pinsEqual(pin, static_cast<uint8_t>(I2S_DOUT))) {
        return true;
    }
#endif
#if defined(I2S_BCLK) && (I2S_BCLK != 255)
    if (pinsEqual(pin, static_cast<uint8_t>(I2S_BCLK))) {
        return true;
    }
#endif
#if defined(I2S_LRC) && (I2S_LRC != 255)
    if (pinsEqual(pin, static_cast<uint8_t>(I2S_LRC))) {
        return true;
    }
#endif
    return false;
}

inline constexpr bool conflictsWithOptionalUserPins(uint8_t pin) {
    if (pin == kPinNotConnected) {
        return false;
    }
#if defined(IR_PIN) && (IR_PIN != 255)
    if (pinsEqual(pin, static_cast<uint8_t>(IR_PIN))) {
        return true;
    }
#endif
#if defined(MUTE_PIN) && (MUTE_PIN != 255)
    if (pinsEqual(pin, static_cast<uint8_t>(MUTE_PIN))) {
        return true;
    }
#endif
#if defined(BTN_MUTE) && (BTN_MUTE != 255)
    if (pinsEqual(pin, static_cast<uint8_t>(BTN_MUTE))) {
        return true;
    }
#endif
#if defined(WAKE_PIN) && (WAKE_PIN != 255)
    // WAKE may legally share BOOT/R4 lifecycle on this board; still reject if a
    // storage line claims the same GPIO as the configured wake pin when that pin
    // is not the documented R4 share (storage never uses GPIO0 on the schematic).
    // WAKE может делить BOOT/R4; отклоняем, если линия storage совпала с WAKE
    // (на схеме TF не использует GPIO0).
    if (pinsEqual(pin, static_cast<uint8_t>(WAKE_PIN))) {
        return true;
    }
#endif
    return false;
}

// Full exclusive conflict check used when a future slice supplies real pins.
// Полная проверка эксклюзивных конфликтов для будущего enable.
inline constexpr bool storagePinHasHardConflict(uint8_t pin,
                                                bool allow_display_command_share) {
    return !isUsableGpio(pin) ||
           conflictsWithExclusiveRuntimePins(pin, allow_display_command_share) ||
           conflictsWithOptionalUserPins(pin);
}

#if YORADIO_BOARD == YORADIO_BOARD_4848S040

// ---- Product ResolvedStorageConfig invariants (S07-1) ----
static_assert(kResolvedStorageConfig.enabled == kStorageEngineeringEnabled,
              "Storage IO requires the explicit engineering opt-in; stock remains disabled.");
static_assert(kResolvedStorageConfig.transport == (kStorageEngineeringEnabled ?
                  StorageTransport::SdSpi : StorageTransport::Disabled),
              "Engineering Storage uses SDSPI; stock transport stays Disabled.");
static_assert(!kResolvedStorageConfig.allow_autoformat,
              "YoRadio Storage: allow_autoformat must remain false.");
static_assert(kResolvedStorageConfig.onboard_sd_present ==
                  kActiveBoard.storage.onboard_sd_present,
              "YoRadio Storage: onboard_sd_present must follow BoardProfile.storage.");
static_assert(kResolvedStorageConfig.pin_evidence ==
                  StoragePinEvidence::SchematicCandidate,
              "YoRadio Storage (S07-1): 4848 pin evidence is SchematicCandidate only.");
static_assert(kResolvedStorageConfig.pin_evidence != StoragePinEvidence::DeviceVerified,
              "YoRadio Storage (S07-1): DeviceVerified is forbidden until the hardware gate.");
static_assert(kResolvedStorageConfig.display_command_lifecycle_share_expected,
              "YoRadio Storage: 4848 expects DisplayCommand↔TF lifecycle share on 47/48.");

// ---- Checker self-check against SCHEMATIC candidates (not BoardProfile apply) ----
// Identity: candidate CMD/CLK must match effective DisplayCommand SDA/SCK.
static_assert(storage_schematic_4848s040::kTfCmdGpio == kWiring.display.cmd_sda_gpio,
              "YoRadio Storage: schematic TF CMD must match effective DisplayCommand SDA (GPIO47).");
static_assert(storage_schematic_4848s040::kTfClkGpio == kWiring.display.cmd_sck_gpio,
              "YoRadio Storage: schematic TF CLK must match effective DisplayCommand SCK (GPIO48).");
static_assert(storage_schematic_4848s040::kLcdCmdCsGpio == kWiring.display.cmd_cs_gpio,
              "YoRadio Storage: schematic LCD_CS must match effective DisplayCommand CS (GPIO39).");

// With lifecycle share allowed, CMD/CLK must NOT hard-conflict; CS/DAT0 must be free
// of exclusive runtime claimants under stock IR/MUTE=255.
static_assert(!storagePinHasHardConflict(storage_schematic_4848s040::kTfCmdGpio, true),
              "YoRadio Storage: schematic TF CMD (47) must be legal under DisplayCommand lifecycle share.");
static_assert(!storagePinHasHardConflict(storage_schematic_4848s040::kTfClkGpio, true),
              "YoRadio Storage: schematic TF CLK (48) must be legal under DisplayCommand lifecycle share.");
static_assert(!storagePinHasHardConflict(storage_schematic_4848s040::kTfDat0Gpio, true),
              "YoRadio Storage: schematic TF DAT0 (41) conflicts with display/touch/I2S/IR/MUTE/WAKE.");
static_assert(!storagePinHasHardConflict(storage_schematic_4848s040::kTfDat3CsGpio, true),
              "YoRadio Storage: schematic TF DAT3/CS (42) conflicts with display/touch/I2S/IR/MUTE/WAKE.");

// Without the share exception, CMD/CLK correctly look like conflicts — proves the
// exception is intentional, not a missing collision check.
static_assert(storagePinHasHardConflict(storage_schematic_4848s040::kTfCmdGpio, false),
              "YoRadio Storage: TF CMD must conflict with DisplayCommand SDA when share is disallowed.");
static_assert(storagePinHasHardConflict(storage_schematic_4848s040::kTfClkGpio, false),
              "YoRadio Storage: TF CLK must conflict with DisplayCommand SCK when share is disallowed.");

// LCD_CS must never be claimed as an SD line.
static_assert(storagePinHasHardConflict(storage_schematic_4848s040::kLcdCmdCsGpio, true),
              "YoRadio Storage: LCD_CS (39) must remain exclusive to the display.");

static_assert(!storage_schematic_4848s040::kSupportsSdmmc4Bit,
              "YoRadio Storage: 4848 schematic has no MCU DAT1/DAT2 — 4-bit SDMMC is unavailable.");
static_assert(!storage_schematic_4848s040::kCardDetectGpioPresent,
              "YoRadio Storage: 4848 schematic ties CD to GND — no CD GPIO.");

// Permanent product BoardProfile/kWiring storage pins remain outside the
// engineering opt-in. Runtime handoff/mount belong to StorageOwner, not here.
// Постоянная разводка Storage в BoardProfile/kWiring не задаётся инженерным
// opt-in. Runtime handoff/mount выполняет StorageOwner, а не валидатор.

#endif  // YORADIO_BOARD == YORADIO_BOARD_4848S040

}  // namespace storage_validate
}  // namespace yoradio

#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_STORAGE_STORAGE_VALIDATE_H
