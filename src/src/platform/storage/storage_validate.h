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

// Final permission is one formula. Card readiness is not part of it.
// Итоговое разрешение — одна формула. Готовность карты в неё не входит.
static_assert(kResolvedStorageConfig.enabled ==
                  (kStorageEngineeringEnabled && kUserSdEnable &&
                   kActiveBoard.storage.onboard_sd_present),
              "SD permission must be Owner build + YORADIO_SD_ENABLE + board slot.");
static_assert(kResolvedStorageConfig.transport ==
                  (kResolvedStorageConfig.enabled ? StorageTransport::SdSpi
                                                  : StorageTransport::Disabled),
              "Permitted SD uses SDSPI. Otherwise transport stays Disabled.");
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

// The resolver rejects a second claim on a pin an active device already owns.
// Резолвер отвергает повторное требование пина, который уже занят активным устройством.
static_assert(wiring_check::hasDuplicateGpio(wiring_check::withPin(
                  wiring_check::kClaims, kWiring.display.pclk_gpio)),
              "GPIO resolver must reject a pin already claimed by the active display.");
static_assert(storagePinHasHardConflict(kWiring.display.cmd_cs_gpio, true),
              "ST7701 command CS stays exclusive. SD must not take it.");

// A moved command SDA is not the TF map. The literal is a resolver check,
// not a change to the board wiring.
// Сдвинутый командный SDA — не карта TF. Литерал проверяет резолвер,
// а не меняет разводку платы.
inline constexpr bool sdCommandPinsMatchDisplay(uint8_t sda, uint8_t sck, uint8_t lcd_cs) {
    return sda == storage_schematic_4848s040::kTfCmdGpio &&
           sck == storage_schematic_4848s040::kTfClkGpio &&
           lcd_cs == storage_schematic_4848s040::kLcdCmdCsGpio;
}
static_assert(!sdCommandPinsMatchDisplay(1, 48, 39),
              "Custom DisplayCommand SDA is not a usable TF map while SD is enabled.");

// One GPIO that the base claim list does not own can be added once.
// Один GPIO, которого нет в базовом списке, можно занять один раз.
inline constexpr uint8_t firstUnclaimedGpio() {
    for (uint8_t pin = 0; pin <= 48; ++pin) {
        if (!isUsableGpio(pin)) {
            continue;
        }
        if (wiring_check::containsPin(wiring_check::kClaims, pin)) {
            continue;
        }
        return pin;
    }
    return kPinNotConnected;
}
static_assert(firstUnclaimedGpio() != kPinNotConnected,
              "GPIO resolver found no free pin to prove reassignment.");
static_assert(!wiring_check::hasDuplicateGpio(wiring_check::withPin(
                  wiring_check::kClaims, firstUnclaimedGpio())),
              "GPIO resolver must accept one claim on a pin no active device owns.");

#if YORADIO_SD_ENABLE && YORADIO_S07_STORAGE_ENABLED
// SD on: DAT0/CS join the real claim list. 47/48 stay display command pins.
// SD включена: DAT0/CS входят в реальный список. 47/48 остаются командными пинами дисплея.
static_assert(kResolvedStorageConfig.enabled,
              "YORADIO_SD_ENABLE=1 on the Owner build must permit SD.");
static_assert(sdCommandPinsMatchDisplay(kWiring.display.cmd_sda_gpio,
                                        kWiring.display.cmd_sck_gpio,
                                        kWiring.display.cmd_cs_gpio),
              "YORADIO_SD_ENABLE=1 needs DISP_SDA=47, DISP_SCK=48 and DISP_CS=39. "
              "Set YORADIO_SD_ENABLE to 0 or restore those DisplayCommand lines.");
inline constexpr wiring_check::ClaimList kSdClaims = wiring_check::withPin(
    wiring_check::withPin(wiring_check::kClaims, storage_schematic_4848s040::kTfDat0Gpio),
    storage_schematic_4848s040::kTfDat3CsGpio);
static_assert(wiring_check::containsPin(kSdClaims, storage_schematic_4848s040::kTfDat0Gpio) &&
                  wiring_check::containsPin(kSdClaims, storage_schematic_4848s040::kTfDat3CsGpio),
              "SD on must reserve GPIO 41 and GPIO 42.");
static_assert(!wiring_check::hasDuplicateGpio(kSdClaims),
              "YORADIO_SD_ENABLE=1: GPIO 41 or 42 is already used by display, touch, or I2S. "
              "Move that signal or set YORADIO_SD_ENABLE to 0.");
static_assert(!storagePinHasHardConflict(storage_schematic_4848s040::kTfCmdGpio, true),
              "YORADIO_SD_ENABLE=1: TF CMD GPIO47 conflicts with an active device.");
static_assert(!storagePinHasHardConflict(storage_schematic_4848s040::kTfClkGpio, true),
              "YORADIO_SD_ENABLE=1: TF CLK GPIO48 conflicts with an active device.");
static_assert(!storagePinHasHardConflict(storage_schematic_4848s040::kTfDat0Gpio, true),
              "YORADIO_SD_ENABLE=1: TF DAT0 GPIO41 conflicts with display, touch, I2S, IR, MUTE, or WAKE.");
static_assert(!storagePinHasHardConflict(storage_schematic_4848s040::kTfDat3CsGpio, true),
              "YORADIO_SD_ENABLE=1: TF CS GPIO42 conflicts with display, touch, I2S, IR, MUTE, or WAKE.");
static_assert(storagePinHasHardConflict(storage_schematic_4848s040::kTfCmdGpio, false),
              "TF CMD must still conflict with DisplayCommand SDA when the share is disallowed.");
static_assert(storagePinHasHardConflict(storage_schematic_4848s040::kTfClkGpio, false),
              "TF CLK must still conflict with DisplayCommand SCK when the share is disallowed.");
#else
// SD off: do not reserve DAT0/CS. Display, touch and I2S claims stay in force.
// SD выключена: DAT0/CS не резервируются. Требования дисплея, тача и I2S остаются.
static_assert(!kResolvedStorageConfig.enabled,
              "YORADIO_SD_ENABLE=0 must leave SD disabled.");
#endif

static_assert(!storage_schematic_4848s040::kSupportsSdmmc4Bit,
              "YoRadio Storage: 4848 schematic has no MCU DAT1/DAT2 — 4-bit SDMMC is unavailable.");
static_assert(!storage_schematic_4848s040::kCardDetectGpioPresent,
              "YoRadio Storage: 4848 schematic ties CD to GND — no CD GPIO.");

#endif  // YORADIO_BOARD == YORADIO_BOARD_4848S040

}  // namespace storage_validate
}  // namespace yoradio

#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_STORAGE_STORAGE_VALIDATE_H
