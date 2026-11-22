// Storage transport and evidence enums for the compile-time Storage Contract (S07-1).
// Типы транспорта и уровня доказательств для compile-time контракта Storage (S07-1).
// Data only — no drivers, no mount, no GPIO ownership transfer.
// Только данные — без драйверов, mount и передачи владения GPIO.
#ifndef YORADIO_PLATFORM_STORAGE_STORAGE_TYPES_H
#define YORADIO_PLATFORM_STORAGE_STORAGE_TYPES_H

#include <stdint.h>

#include "../board/board_contract.h"

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

// Pin-map evidence is a board fact (BoardProfile.storage.sd_pin_evidence, S07_bis-3c).
// SD GPIO live in the profile together with that level; only a hardware gate may
// raise it to DeviceVerified. The storage name stays as an alias.
// Уровень доказанности — факт платы (S07_bis-3c). SD GPIO хранятся в профиле вместе
// с этим уровнем; повысить до DeviceVerified может только аппаратная проверка.
using StoragePinEvidence = PinEvidence;

}  // namespace yoradio

#endif  // YORADIO_PLATFORM_STORAGE_STORAGE_TYPES_H
