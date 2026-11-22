// Compile-time Storage Contract checks (S07-1).
// Compile-time проверки контракта Storage (S07-1).
//
// Included from options.h AFTER board_validate.h so SDC_CS / I2S / IR / MUTE /
// WAKE macros and kWiring exist. No runtime mount or GPIO handoff.
// Подключается из options.h ПОСЛЕ board_validate.h. Без runtime mount/handoff.
#ifndef YORADIO_PLATFORM_STORAGE_STORAGE_VALIDATE_H
#define YORADIO_PLATFORM_STORAGE_STORAGE_VALIDATE_H

#include "resolved_storage_config.h"
#include "../family/family_select.h"

// ---------------------------------------------------------------------------
// Legacy SDFS is never an enable mechanism, including engineering builds.
// Legacy SDFS не служит способом включения, в том числе в инженерной сборке.
// ---------------------------------------------------------------------------
#if defined(SDC_CS) && (SDC_CS != 255)
#  error "YoRadio Storage: keep SDC_CS=255; legacy SDManager is forbidden. The Storage Owner is not USE_SD."
#endif
// sdmanager.cpp has a legacy empty compile marker; it is not a runtime enable.
// Legacy TU задаёт пустой маркер USE_SD; он не включает runtime SD.
#if YORADIO_STORAGE_OWNER_ENABLED && defined(USE_SD) && (USE_SD + 0)
#error "Storage Owner requires USE_SD=false; do not activate legacy SDManager."
#endif

#ifdef __cplusplus

namespace yoradio {
namespace storage_validate {

// Usable-GPIO rule comes from the build family (platform/family/); no local copy.
// Правило допустимых GPIO берётся из семейства (platform/family/); локальной копии нет.
using family::isUsableGpio;

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

// ---------------------------------------------------------------------------
// Common rules for every board. Board facts (TF map, evidence level, CD, width)
// are asserted by the profile validator; this block names no board.
// Общие правила для любой платы. Факты платы (карта TF, уровень доказанности, CD,
// ширина) проверяет профильный валидатор; здесь плата не называется.
// ---------------------------------------------------------------------------
// Final permission is one formula. Card readiness is not part of it.
// Итоговое разрешение — одна формула. Готовность карты в неё не входит.
static_assert(kResolvedStorageConfig.enabled ==
                  (kStorageOwnerEnabled && kUserSdEnable &&
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
static_assert(kResolvedStorageConfig.pin_evidence == kActiveBoard.storage.sd_pin_evidence,
              "YoRadio Storage: pin evidence must follow BoardProfile.storage.");

// A slot either has a complete, usable SPI map or none at all.
// Слот либо имеет полную пригодную карту SPI, либо не имеет пинов вовсе.
static_assert(!kActiveBoard.storage.onboard_sd_present || boardDeclaresSdPins(),
              "BoardProfile.storage: a board with an SD slot must declare CLK/CMD/DAT0/CS and evidence.");
static_assert(kActiveBoard.storage.onboard_sd_present ||
                  (kActiveBoard.storage.sd_clk_gpio == kPinNotConnected && kActiveBoard.storage.sd_cmd_gpio == kPinNotConnected &&
                   kActiveBoard.storage.sd_dat0_gpio == kPinNotConnected && kActiveBoard.storage.sd_cs_gpio == kPinNotConnected),
              "BoardProfile.storage: SD pins without an SD slot.");
static_assert(!kActiveBoard.storage.onboard_sd_present ||
                  (isUsableGpio(kActiveBoard.storage.sd_clk_gpio) && isUsableGpio(kActiveBoard.storage.sd_cmd_gpio) &&
                   isUsableGpio(kActiveBoard.storage.sd_dat0_gpio) && isUsableGpio(kActiveBoard.storage.sd_cs_gpio)),
              "BoardProfile.storage: an SD line uses an unusable GPIO for this build family.");

// SD claims with roles. Shared lines pass only where BoardProfile.shared declares
// the SD role as a follower of the role that holds the pin.
// Требования SD с ролями. Общие линии проходят, только если BoardProfile.shared
// объявил роль SD последователем роли, занимающей пин.
inline constexpr wiring_check::ClaimList withSdClaims(wiring_check::ClaimList base) {
    return wiring_check::withPin(
        wiring_check::withPin(
            wiring_check::withPin(
                wiring_check::withPin(base, kActiveBoard.storage.sd_clk_gpio, PinRole::SdClk),
                kActiveBoard.storage.sd_cmd_gpio, PinRole::SdCmd),
            kActiveBoard.storage.sd_dat0_gpio, PinRole::SdDat0),
        kActiveBoard.storage.sd_cs_gpio, PinRole::SdCs);
}

// The display owner of a shared SD line must still sit on it: SD is wired to that
// trace, so a moved command line breaks the lifecycle handoff.
// Владелец общей линии SD должен оставаться на ней: SD разведена на ту же дорожку,
// и перенос командной линии ломает передачу по жизненному циклу.
inline constexpr bool sharedSdOwnersInPlace() {
    for (uint8_t i = 0; i < kWiring.shared.count && i < kMaxSharedPins; ++i) {
        const SharedPin& e = kWiring.shared.pins[i];
        const uint32_t sd_roles = pinRoleBit(PinRole::SdClk) | pinRoleBit(PinRole::SdCmd) |
                                  pinRoleBit(PinRole::SdDat0) | pinRoleBit(PinRole::SdCs);
        if ((e.followers & sd_roles) == 0u) {
            continue;
        }
        if (wiring_check::boardPinOf(kWiring, e.owner) != e.gpio) {
            return false;
        }
    }
    return true;
}

// Resolver self-tests (hold on every board).
// Самопроверки резолвера (верны для любой платы).
static_assert(wiring_check::hasDuplicateGpio(wiring_check::withPin(
                  wiring_check::kClaims, kWiring.display.pclk_gpio)),
              "GPIO resolver must reject a pin already claimed by the active display.");
static_assert(wiring_check::hasDuplicateGpio(wiring_check::withPin(
                  wiring_check::kClaims, kWiring.display.cmd_cs_gpio, PinRole::SdCs)),
              "Display command CS stays exclusive. SD must not take it.");
static_assert(storagePinHasHardConflict(kWiring.display.cmd_cs_gpio, true),
              "Display command CS stays exclusive even when the command share is allowed.");

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

#if YORADIO_SD_ENABLE && YORADIO_STORAGE_OWNER_ENABLED
// SD on: all four SD lines join the real claim list with their roles.
// SD включена: все четыре линии SD входят в реальный список со своими ролями.
static_assert(kResolvedStorageConfig.enabled,
              "YORADIO_SD_ENABLE=1 on the Owner build must permit SD (the board needs an SD slot).");
static_assert(sharedSdOwnersInPlace(),
              "YORADIO_SD_ENABLE=1: an SD line shares a display command line that Custom Wiring moved "
              "(DISP_SDA/DISP_SCK). Set YORADIO_SD_ENABLE to 0 or restore those DisplayCommand lines.");
static_assert(kWiring.display.cmd_cs_gpio == kActiveBoard.display.cmd_cs_gpio ||
                  !kResolvedStorageConfig.display_command_lifecycle_share_expected,
              "YORADIO_SD_ENABLE=1: SD shares the display command bus, so DISP_CS must stay on the factory pin.");
inline constexpr wiring_check::ClaimList kSdClaims = withSdClaims(wiring_check::kClaims);
static_assert(wiring_check::containsPin(kSdClaims, kActiveBoard.storage.sd_dat0_gpio) &&
                  wiring_check::containsPin(kSdClaims, kActiveBoard.storage.sd_cs_gpio),
              "SD on must reserve DAT0 and CS.");
static_assert(!wiring_check::hasDuplicateGpio(kSdClaims),
              "YORADIO_SD_ENABLE=1: an SD line (CLK/CMD/DAT0/CS) is already used by display, touch or I2S "
              "and the board does not declare that share. Move that signal or set YORADIO_SD_ENABLE to 0.");
static_assert(!conflictsWithOptionalUserPins(kActiveBoard.storage.sd_clk_gpio) &&
                  !conflictsWithOptionalUserPins(kActiveBoard.storage.sd_cmd_gpio) &&
                  !conflictsWithOptionalUserPins(kActiveBoard.storage.sd_dat0_gpio) &&
                  !conflictsWithOptionalUserPins(kActiveBoard.storage.sd_cs_gpio),
              "YORADIO_SD_ENABLE=1: an SD line conflicts with IR, MUTE or WAKE.");
#else
// SD off: SD lines are not claimed. Display, touch and I2S claims stay in force.
// SD выключена: линии SD не занимаются. Требования дисплея, тача и I2S остаются.
static_assert(!kResolvedStorageConfig.enabled,
              "YORADIO_SD_ENABLE=0 must leave SD disabled.");
#endif

}  // namespace storage_validate
}  // namespace yoradio

#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_STORAGE_STORAGE_VALIDATE_H
