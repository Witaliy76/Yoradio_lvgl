// Stable compile-time board identifiers for the YoRadio Board Contract.
// Стабильные compile-time идентификаторы плат для Board Contract YoRadio.
// Ownership: platform/board. Bottom of the include graph: includes nothing from the project,
// so it can be parsed before myoptions.h without any cycle.
// Владелец: platform/board. Низ include-графа: ничего из проекта не включает,
// поэтому парсится до myoptions.h без циклов.
// Values are permanent: never reuse or reassign an id.
// Значения постоянны: id нельзя переиспользовать или переназначать.
#ifndef YORADIO_PLATFORM_BOARD_ID_H
#define YORADIO_PLATFORM_BOARD_ID_H

// 0 is reserved and is never a board. An identifier the preprocessor does not know
// evaluates to 0 inside #if, so reserving 0 turns a misspelled board name into a
// clear error instead of a silent match against a real board.
// 0 зарезервирован и платой не является. Неизвестный препроцессору идентификатор
// в #if равен 0, поэтому резерв 0 превращает опечатку в явную ошибку.
#define YORADIO_BOARD_NONE       0
#define YORADIO_BOARD_4848S040   1

#ifdef __cplusplus
#include <stdint.h>

namespace yoradio {

enum class BoardId : uint16_t {
    None             = YORADIO_BOARD_NONE,
    Esp32S3_4848S040 = YORADIO_BOARD_4848S040,
};

}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_BOARD_ID_H
