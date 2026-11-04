// Compile-time UI profile identity selected by BoardDefaults.
// Идентификатор UI-профиля времени компиляции, выбираемый BoardDefaults.
#ifndef YORADIO_UI_PROFILE_ID_H
#define YORADIO_UI_PROFILE_ID_H

#include <stdint.h>

namespace yoradio {

// This is application policy, not BoardProfile physical truth. A profile is
// always selected explicitly; panel resolution never chooses it implicitly.
// Это policy приложения, а не физический факт BoardProfile. Профиль выбирается
// явно; разрешение панели никогда не выбирает его автоматически.
enum class UiProfile : uint8_t {
    Square480 = 0,
};

}  // namespace yoradio

#endif  // YORADIO_UI_PROFILE_ID_H
