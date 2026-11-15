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

#ifndef YORADIO_S07_STORAGE_ENABLED
#define YORADIO_S07_STORAGE_ENABLED 0
#endif
#if YORADIO_S07_STORAGE_ENABLED != 0 && YORADIO_S07_STORAGE_ENABLED != 1
#error "YORADIO_S07_STORAGE_ENABLED must be 0 or 1."
#endif
#if YORADIO_S07_STORAGE_ENABLED && YORADIO_BOARD != YORADIO_BOARD_4848S040
#error "Engineering Storage opt-in is restricted to ESP32-4848S040."
#endif

namespace yoradio {
inline constexpr bool kStorageEngineeringEnabled = YORADIO_S07_STORAGE_ENABLED != 0;

struct ResolvedStorageConfig {
    // Factory fact: slot present on the board (not "SD enabled in firmware").
    // Заводской факт: слот на плате есть (это не «SD включён в прошивке»).
    bool onboard_sd_present;

    // SD enable gate. On for the 4848S040 Owner build; off when the macro is 0.
    // Включение SD. Включено в сборке Owner 4848S040; выключено при макросе 0.
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
constexpr ResolvedStorageConfig resolveStorageConfigS07_1() {
    return ResolvedStorageConfig{
        .onboard_sd_present = kActiveBoard.storage.onboard_sd_present,
        .enabled = kStorageEngineeringEnabled,
        .transport = kStorageEngineeringEnabled ? StorageTransport::SdSpi : StorageTransport::Disabled,
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
    detail::resolveStorageConfigS07_1();

}  // namespace yoradio

#endif  // YORADIO_PLATFORM_STORAGE_RESOLVED_STORAGE_CONFIG_H
