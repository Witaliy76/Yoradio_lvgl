// Compile-time storage settings. Ordinary 4848S040 enables the Owner.
// Other boards stay off. Legacy USE_SD / SDManager stay off.
// Настройки накопителя. Штатный 4848S040 включает Owner.
// Другие платы остаются выключенными. Legacy USE_SD / SDManager выключены.
//
// Data composition only. No SD begin/mount, no GPIO reconfiguration, no handoff.
// Pins are NOT duplicated from BoardProfile: storage GPIO are still absent there
// until product acceptance. Engineering reuses the existing candidate map under
// the accepted limited functional bring-up decision; this is not DeviceVerified.
// Только композиция данных. Без SD begin/mount и переназначения GPIO. Пины не
// дублируют BoardProfile. Инженерная сборка использует принятый функциональный
// кандидат без объявления DeviceVerified или измеренных электрических параметров.
#ifndef YORADIO_PLATFORM_STORAGE_RESOLVED_STORAGE_CONFIG_H
#define YORADIO_PLATFORM_STORAGE_RESOLVED_STORAGE_CONFIG_H

#include <stdint.h>

#include "../board/board_select.h"
#include "schematic_candidate_4848s040.h"
#include "storage_types.h"

#ifndef YORADIO_STORAGE_OWNER_ENABLED
#define YORADIO_STORAGE_OWNER_ENABLED 0
#endif
#if YORADIO_STORAGE_OWNER_ENABLED != 0 && YORADIO_STORAGE_OWNER_ENABLED != 1
#error "YORADIO_STORAGE_OWNER_ENABLED must be 0 or 1."
#endif
#if YORADIO_STORAGE_OWNER_ENABLED && YORADIO_BOARD != YORADIO_BOARD_4848S040
#error "Storage Owner is restricted to ESP32-4848S040."
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

    // Temporary DisplayCommand↔TF share is allowed as a *lifecycle* concept on
    // 4848 when transport is enabled; this field records the intended sharing.
    // Временное совмещение DisplayCommand↔TF допустимо как lifecycle на 4848
    // после разрешённого включения; поле фиксирует предполагаемое совмещение.
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

// The 4848S040 Owner build selects SDSPI. Macro 0 stays Disabled.
// Legacy SDC_CS!=255 remains refused.
// Сборка Owner 4848S040 выбирает SDSPI. При макросе 0 транспорт Disabled.
// Legacy SDC_CS!=255 по-прежнему запрещён.
constexpr bool storagePermitted() {
    return kStorageOwnerEnabled && kUserSdEnable &&
           kActiveBoard.storage.onboard_sd_present;
}

constexpr ResolvedStorageConfig resolveStorageOwnerConfig() {
    const bool permitted = storagePermitted();
    return ResolvedStorageConfig{
        .onboard_sd_present = kActiveBoard.storage.onboard_sd_present,
        .enabled = permitted,
        .transport = permitted ? StorageTransport::SdSpi : StorageTransport::Disabled,
        .pin_evidence =
            (YORADIO_BOARD == YORADIO_BOARD_4848S040)
                ? storage_schematic_4848s040::kPinEvidence
                : StoragePinEvidence::None,
        .display_command_lifecycle_share_expected =
            (YORADIO_BOARD == YORADIO_BOARD_4848S040),
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
