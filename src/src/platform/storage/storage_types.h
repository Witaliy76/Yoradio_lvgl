// Storage transport and evidence enums for the compile-time Storage Contract (S07-1).
// Типы транспорта и уровня доказательств для compile-time контракта Storage (S07-1).
// Data only — no drivers, no mount, no GPIO ownership transfer.
// Только данные — без драйверов, mount и передачи владения GPIO.
#ifndef YORADIO_PLATFORM_STORAGE_STORAGE_TYPES_H
#define YORADIO_PLATFORM_STORAGE_STORAGE_TYPES_H

#include <stdint.h>

namespace yoradio {

// Product transport selection. Disabled is the accepted 4848 default until a later
// S07 slice enables a proven host path. Unspecified means "enable requested but
// transport not chosen" and must fail compile-time validation if ever selected.
// Выбор транспорта. Disabled — принятый default 4848 до позднего S07. Unspecified
// означает «хотят включить, транспорт не выбран» и должен отвергаться валидатором.
enum class StorageTransport : uint8_t {
    Disabled = 0,
    Unspecified = 1,
    SdSpi = 2,
    SdMmc1Bit = 3,
};

// How strongly the pin map is known. SchematicCandidate is planning evidence only;
// DeviceVerified is required before BoardProfile may encode SD GPIO.
// Насколько надёжно известна разводка. SchematicCandidate — только планирование;
// DeviceVerified обязателен до записи SD GPIO в BoardProfile.
enum class StoragePinEvidence : uint8_t {
    None = 0,
    SchematicCandidate = 1,
    DeviceVerified = 2,
};

}  // namespace yoradio

#endif  // YORADIO_PLATFORM_STORAGE_STORAGE_TYPES_H
