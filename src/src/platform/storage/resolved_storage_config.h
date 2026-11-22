// Compile-time storage settings. The Owner build may run on any board whose
// profile declares an SD slot with its pins (BoardProfile.storage); which builds
// enable the Owner is a build decision. Legacy USE_SD / SDManager stay off.
// Настройки накопителя. Owner допустим на любой плате, профиль которой объявил
// слот SD с пинами (BoardProfile.storage); какие сборки включают Owner — решение
// сборки. Legacy USE_SD / SDManager выключены.
//
// Data composition only. No SD begin/mount, no GPIO reconfiguration, no handoff.
// Pins and their evidence level come from BoardProfile.storage (S07_bis-3c) and
// are not copied here. Evidence is not raised by this file.
// Только композиция данных. Без SD begin/mount и переназначения GPIO. Пины и их
// уровень доказанности берутся из BoardProfile.storage и здесь не копируются.
#ifndef YORADIO_PLATFORM_STORAGE_RESOLVED_STORAGE_CONFIG_H
#define YORADIO_PLATFORM_STORAGE_RESOLVED_STORAGE_CONFIG_H

#include <stdint.h>

#include "../board/board_select.h"
#include "storage_types.h"

#ifndef YORADIO_STORAGE_OWNER_ENABLED
#define YORADIO_STORAGE_OWNER_ENABLED 0
#endif
#if YORADIO_STORAGE_OWNER_ENABLED != 0 && YORADIO_STORAGE_OWNER_ENABLED != 1
#error "YORADIO_STORAGE_OWNER_ENABLED must be 0 or 1."
#endif

// User permission. The macro lives in src/myoptions.h, which options.h includes
// before this header. 0 and 1 are the only accepted values.
// Разрешение пользователя. Макрос задаётся в src/myoptions.h до этого заголовка.
#ifndef YORADIO_SD_ENABLE
#error "YoRadio: define YORADIO_SD_ENABLE as 0 or 1 in src/myoptions.h."
#endif
#if (YORADIO_SD_ENABLE) != 0 && (YORADIO_SD_ENABLE) != 1
#error "YoRadio: YORADIO_SD_ENABLE must be 0 or 1."
#endif

namespace yoradio {
inline constexpr bool kStorageOwnerEnabled = YORADIO_STORAGE_OWNER_ENABLED != 0;

// The board declares a usable SD map: a slot, all four SPI lines and an evidence level.
// Плата объявила пригодную карту SD: слот, все четыре линии SPI и уровень доказанности.
inline constexpr bool boardDeclaresSdPins() {
    return kActiveBoard.storage.onboard_sd_present &&
           kActiveBoard.storage.sd_pin_evidence != PinEvidence::None &&
           kActiveBoard.storage.sd_clk_gpio != kPinNotConnected &&
           kActiveBoard.storage.sd_cmd_gpio != kPinNotConnected &&
           kActiveBoard.storage.sd_dat0_gpio != kPinNotConnected &&
           kActiveBoard.storage.sd_cs_gpio != kPinNotConnected;
}
static_assert(!kStorageOwnerEnabled || boardDeclaresSdPins(),
              "YoRadio Storage: the Owner build needs a board whose BoardProfile.storage declares "
              "the SD slot, CLK/CMD/DAT0/CS pins and their evidence level.");
// User asked for SD. This is not card readiness and not the board's slot fact.
// Пользователь запросил SD. Это не готовность карты и не факт слота на плате.
inline constexpr bool kUserSdEnable = (YORADIO_SD_ENABLE) != 0;

struct ResolvedStorageConfig {
    // Factory fact: slot present on the board (not "SD enabled in firmware").
    // Заводской факт: слот на плате есть (это не «SD включён в прошивке»).
    bool onboard_sd_present;

    // Final SD permission: Owner build AND user flag AND a board slot.
    // Pin conflicts are rejected at compile time, not turned into a silent false.
    // Итоговое разрешение SD: сборка Owner, флаг пользователя и слот платы.
    // Конфликт пинов останавливает сборку, а не прячется в false.
    bool enabled;

    StorageTransport transport;

    // Provenance of the candidate map; not DeviceVerified.
    // Происхождение кандидатной разводки; пока не DeviceVerified.
    StoragePinEvidence pin_evidence;

    // True when the board declares an SD line as a follower of a display command
    // line in BoardProfile.shared (lifecycle share after the panel releases it).
    // True, если плата объявила линию SD последователем командной линии дисплея
    // в BoardProfile.shared (совмещение по жизненному циклу).
    bool display_command_lifecycle_share_expected;

    // Only mount_path is bound to the owner. Retry/timeout fields are reserved,
    // not consumed by the driver, and impose no wall-clock deadline. No autoformat.
    // Владелец использует mount_path. Поля retry/timeout зарезервированы,
    // драйвер их не читает; ограничения времени они не задают. Автоформата нет.
    const char* mount_path;
    uint8_t max_mount_retries;
    uint32_t mount_timeout_ms;
    bool allow_autoformat;
};

namespace detail {

// The Owner build selects SDSPI. Macro 0 stays Disabled.
// Legacy SDC_CS!=255 remains refused.
// Сборка Owner выбирает SDSPI. При макросе 0 транспорт Disabled.
// Legacy SDC_CS!=255 по-прежнему запрещён.
constexpr bool storagePermitted() {
    return kStorageOwnerEnabled && kUserSdEnable &&
           kActiveBoard.storage.onboard_sd_present;
}

constexpr bool sdLineFollowsDisplayCommand() {
    const SharedResources& s = kActiveBoard.shared;
    for (uint8_t i = 0; i < s.count && i < kMaxSharedPins; ++i) {
        const SharedPin& e = s.pins[i];
        const bool display_owner = e.owner == PinRole::DisplayCommandSda ||
                                   e.owner == PinRole::DisplayCommandSck;
        const uint32_t sd_roles = pinRoleBit(PinRole::SdClk) | pinRoleBit(PinRole::SdCmd) |
                                  pinRoleBit(PinRole::SdDat0) | pinRoleBit(PinRole::SdCs);
        if (display_owner && (e.followers & sd_roles) != 0u) {
            return true;
        }
    }
    return false;
}

constexpr ResolvedStorageConfig resolveStorageOwnerConfig() {
    const bool permitted = storagePermitted();
    return ResolvedStorageConfig{
        .onboard_sd_present = kActiveBoard.storage.onboard_sd_present,
        .enabled = permitted,
        .transport = permitted ? StorageTransport::SdSpi : StorageTransport::Disabled,
        .pin_evidence = kActiveBoard.storage.sd_pin_evidence,
        .display_command_lifecycle_share_expected = sdLineFollowsDisplayCommand(),
        .mount_path = "/sdcard",
        .max_mount_retries = 0,
        .mount_timeout_ms = 0,
        .allow_autoformat = false,
    };
}

}  // namespace detail

inline constexpr ResolvedStorageConfig kResolvedStorageConfig =
    detail::resolveStorageOwnerConfig();

}  // namespace yoradio

#endif  // YORADIO_PLATFORM_STORAGE_RESOLVED_STORAGE_CONFIG_H
