// ESP32-S3 build-family rules (build facts only, no settings).
// Правила семейства ESP32-S3 (только факты сборки, без настроек).
//
// Purpose / Назначение:
//   1. isUsableGpio: which GPIO numbers a board may use on the ESP32-S3 N16R8
//      module. Moved verbatim from board_validate.h / storage_validate.h (S07_bis-1).
//   2. supportsTransport: display transports this family can drive.
//   3. RGB transport rule: CONFIG_LCD_RGB_RESTART_IN_VSYNC must stay disabled when
//      the active board uses the RGB565 parallel transport. Keyed on the transport,
//      not on a board id.
//   1. isUsableGpio: какие GPIO допустимы на модуле ESP32-S3 N16R8. Перенесено без
//      изменений из board_validate.h / storage_validate.h (S07_bis-1).
//   2. supportsTransport: транспорты дисплея, которые умеет это семейство.
//   3. Правило RGB-транспорта: CONFIG_LCD_RGB_RESTART_IN_VSYNC должен быть выключен,
//      если активная плата использует параллельный RGB565. Привязано к транспорту,
//      а не к id платы.
//
// Ownership / lifecycle: compile-time only. Included through family_select.h.
// Владение: только время компиляции. Подключается через family_select.h.
#ifndef YORADIO_PLATFORM_FAMILY_ESP32S3_FAMILY_RULES_H
#define YORADIO_PLATFORM_FAMILY_ESP32S3_FAMILY_RULES_H

#include "sdkconfig.h"

#ifdef __cplusplus
#include <stdint.h>

#include "../../board/board_select.h"

namespace yoradio {
namespace family {

// ESP32-S3 N16R8 with octal PSRAM: GPIO 22-25 do not exist; 26-37 serve the
// flash/PSRAM bus on the N16R8 module. 255 means not connected.
// ESP32-S3 N16R8 с восьмилинейной PSRAM: GPIO 22-25 нет; 26-37 заняты flash/PSRAM.
// 255 означает, что сигнал не подключён.
inline constexpr bool isUsableGpio(uint8_t pin) {
    if (pin == kPinNotConnected) {
        return true;
    }
    if (pin > 48u) {
        return false;
    }
    if (pin >= 22u && pin <= 37u) {
        return false;
    }
    return true;
}

// ESP32-S3 drives displays only through the RGB565 parallel peripheral here.
// ESP32-S3 в этом проекте ведёт дисплей только через параллельный RGB565.
inline constexpr bool supportsTransport(DisplayTransport transport) {
    return transport == DisplayTransport::Rgb565Parallel16;
}

#if defined(CONFIG_LCD_RGB_RESTART_IN_VSYNC) && CONFIG_LCD_RGB_RESTART_IN_VSYNC
inline constexpr bool kRgbRestartInVsyncEnabled = true;
#else
inline constexpr bool kRgbRestartInVsyncEnabled = false;
#endif

// Automatic RGB restart/resync recovery is removed (accepted at beta.3.03).
// Автоматическое восстановление RGB (restart/resync) удалено (принято в beta.3.03).
static_assert(!(kRgbRestartInVsyncEnabled &&
                kActiveBoard.display.transport == DisplayTransport::Rgb565Parallel16),
    "ESP32-S3 RGB transport rule: CONFIG_LCD_RGB_RESTART_IN_VSYNC must remain disabled "
    "for a board with the RGB565 parallel display transport "
    "(check the env sdkconfig and sdkconfig.defaults).");

}  // namespace family
}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_FAMILY_ESP32S3_FAMILY_RULES_H
