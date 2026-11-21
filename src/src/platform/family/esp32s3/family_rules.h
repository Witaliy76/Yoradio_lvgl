// ESP32-S3 build-family rules (build facts only, no settings, no board knowledge).
// Правила семейства ESP32-S3 (только факты сборки, без настроек и знания о платах).
//
// Purpose / Назначение:
//   1. isUsableGpio: which GPIO numbers a board may use on the ESP32-S3 N16R8
//      module (octal PSRAM). Moved verbatim from board_validate.h / storage_validate.h
//      (S07_bis-1).
//   2. supportsTransport: display transports this family can drive.
//   3. kRgbRestartInVsyncEnabled: the sdkconfig fact CONFIG_LCD_RGB_RESTART_IN_VSYNC.
//      The RULE that combines it with a board's display transport lives in the common
//      board_validate.h, not here: the family never reads kActiveBoard (S07_bis-3a).
//   1. isUsableGpio: какие GPIO допустимы на модуле ESP32-S3 N16R8 (восьмилинейная
//      PSRAM). Перенесено без изменений из board_validate.h / storage_validate.h
//      (S07_bis-1).
//   2. supportsTransport: транспорты дисплея, которые умеет это семейство.
//   3. kRgbRestartInVsyncEnabled: факт sdkconfig CONFIG_LCD_RGB_RESTART_IN_VSYNC.
//      ПРАВИЛО, сопоставляющее его с транспортом платы, находится в общем
//      board_validate.h, а не здесь: семейство не читает kActiveBoard (S07_bis-3a).
//
// Contract every family_rules.h must expose: isUsableGpio, supportsTransport,
// kRgbRestartInVsyncEnabled (the P4 stub must provide the same names).
// Контракт любого family_rules.h: isUsableGpio, supportsTransport,
// kRgbRestartInVsyncEnabled (заготовка P4 должна дать те же имена).
//
// Ownership / lifecycle: compile-time only. Included through family_select.h.
// Depends only on sdkconfig.h and board_contract.h (types such as DisplayTransport).
// Владение: только время компиляции. Подключается через family_select.h.
// Зависит только от sdkconfig.h и board_contract.h (типы, например DisplayTransport).
#ifndef YORADIO_PLATFORM_FAMILY_ESP32S3_FAMILY_RULES_H
#define YORADIO_PLATFORM_FAMILY_ESP32S3_FAMILY_RULES_H

#include "sdkconfig.h"

// The usable-GPIO rule below is written for octal PSRAM only. With quad PSRAM the
// reserved range differs (GPIO 26-32 flash/PSRAM, 33-37 free), so the rule must be
// revisited rather than silently reused.
// Правило допустимых GPIO ниже рассчитано только на octal PSRAM. При quad PSRAM занятый
// диапазон другой (GPIO 26-32 flash/PSRAM, 33-37 свободны): правило нужно пересмотреть,
// а не использовать молча.
#if defined(CONFIG_SPIRAM) && !defined(CONFIG_SPIRAM_MODE_OCT)
#  error "ESP32-S3 family rules: isUsableGpio assumes octal PSRAM (CONFIG_SPIRAM_MODE_OCT, N16R8 module). This build enables PSRAM in another mode; revisit the GPIO rule before building."
#endif

#ifdef __cplusplus
#include <stdint.h>

#include "../../board/board_contract.h"

namespace yoradio {
namespace family {

// ESP32-S3 N16R8 (octal PSRAM only, see the #error above): GPIO 22-25 do not exist;
// 26-37 are excluded. On this module 26-32 serve flash/PSRAM and 33-37 serve the octal
// PSRAM bus. 255 means not connected.
// ESP32-S3 N16R8 (только octal PSRAM, см. #error выше): GPIO 22-25 нет; 26-37 исключены.
// На этом модуле 26-32 заняты flash/PSRAM, а 33-37 — шиной octal PSRAM. 255 означает,
// что сигнал не подключён.
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

}  // namespace family
}  // namespace yoradio
#endif  // __cplusplus

#endif  // YORADIO_PLATFORM_FAMILY_ESP32S3_FAMILY_RULES_H