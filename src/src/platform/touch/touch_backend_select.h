// The touch backend type chosen by platform/composition.h (S07_bis-8).
// Тип backend тача, выбранный platform/composition.h (S07_bis-8).
// Ownership: platform/touch. composition.h stays data-only; this header maps its
// TouchBackend choice to a class. Both backend classes have the same interface:
// begin / end / setOrientation / read / available.
// Владелец: platform/touch. composition.h остаётся только данными; здесь выбор
// TouchBackend сопоставляется с классом. Интерфейс у обоих классов одинаковый.
#ifndef YORADIO_PLATFORM_TOUCH_BACKEND_SELECT_H
#define YORADIO_PLATFORM_TOUCH_BACKEND_SELECT_H

#include <type_traits>

#include "../composition.h"
#include "gt911_backend.h"
#include "ft5x06_backend.h"

namespace yoradio {

template <composition::TouchBackend B> struct TouchBackendClass;
template <> struct TouchBackendClass<composition::TouchBackend::Gt911>  { using type = Gt911Backend; };
template <> struct TouchBackendClass<composition::TouchBackend::Ft5x06> { using type = Ft5x06Backend; };

using ActiveTouchBackend = typename TouchBackendClass<composition::kTouchBackend>::type;

}  // namespace yoradio

#endif  // YORADIO_PLATFORM_TOUCH_BACKEND_SELECT_H
