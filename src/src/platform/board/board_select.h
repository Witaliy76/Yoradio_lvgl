// Single board-selection and descriptor-resolution point of the Board Contract.
// Единственная точка выбора платы и подключения её дескриптора в Board Contract.
// Ownership: platform/board.
// Included exactly once, from src/src/core/options.h, AFTER myoptions.h has had the
// chance to set YORADIO_BOARD. It never includes options.h or myoptions.h itself —
// that back-include is what created the include cycle in earlier plan revisions.
// Включается ровно один раз из src/src/core/options.h, ПОСЛЕ myoptions.h. Сам он
// никогда не включает options.h/myoptions.h — именно такой обратный include давал цикл.
#ifndef YORADIO_PLATFORM_BOARD_SELECT_H
#define YORADIO_PLATFORM_BOARD_SELECT_H

#include "board_id.h"
#include "board_contract.h"

#if !defined(YORADIO_BOARD)
#  error "YoRadio Board Contract: YORADIO_BOARD is not set. Add '#define YORADIO_BOARD YORADIO_BOARD_4848S040' to src/myoptions.h. Known ids are listed in src/src/platform/board/board_id.h."
#endif

// A board name the preprocessor does not know evaluates to 0, which is
// YORADIO_BOARD_NONE. Catch that here so a typo reports itself as a typo instead of
// falling through to the generic 'unknown board' branch below.
// Неизвестное препроцессору имя равно 0 (YORADIO_BOARD_NONE). Ловим это отдельно,
// чтобы опечатка сообщала именно об опечатке.
#if YORADIO_BOARD == YORADIO_BOARD_NONE
#  error "YoRadio Board Contract: YORADIO_BOARD does not name a known board. An unspelled or misspelled id evaluates to 0. Check it against src/src/platform/board/board_id.h."
#endif

#if YORADIO_BOARD == YORADIO_BOARD_4848S040
#  include "profiles/esp32s3_4848s040/board_profile.h"
#  include "profiles/esp32s3_4848s040/board_defaults.h"
#  ifdef __cplusplus
namespace yoradio {
// Stable aliases every later consumer and the validator read, so that no file outside
// this header has to know which concrete profile was selected.
// kActiveBoard is immutable physical truth; kActiveBoardDefaults is overridable policy.
// Стабильные алиасы для валидатора и будущих потребителей: вне этого файла никто
// не должен знать, какой конкретный профиль выбран.
// kActiveBoard — неизменяемая физическая правда; kActiveBoardDefaults — политика.
inline constexpr const BoardProfile&  kActiveBoard         = kBoardProfile_Esp32S3_4848S040;
inline constexpr const BoardDefaults& kActiveBoardDefaults = kBoardDefaults_Esp32S3_4848S040;
}  // namespace yoradio
#  endif  // __cplusplus
#else
#  error "YoRadio Board Contract: unknown YORADIO_BOARD. Known ids are listed in src/src/platform/board/board_id.h."
#endif

// After kActiveBoard exists: build effective kWiring from the factory profile plus
// any DISP_*/TOUCH_* overrides named in myoptions.h (S02-6 Custom Wiring).
// После появления kActiveBoard: собрать итоговый kWiring из завода и опциональных
// DISP_*/TOUCH_* из myoptions.h (Custom Wiring S02-6).
#include "board_wiring.h"

#ifdef __cplusplus
namespace yoradio {
// Final configuration consumed by drivers. kActiveBoard stays the immutable factory
// profile; only this value may differ when YORADIO_CUSTOM_WIRING is 1.
// Итоговая конфигурация для драйверов. kActiveBoard остаётся неизменным заводом;
// отличаться при YORADIO_CUSTOM_WIRING=1 может только это значение.
inline constexpr BoardProfile kWiring = detail::makeWiring(kActiveBoard);
}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_BOARD_SELECT_H
