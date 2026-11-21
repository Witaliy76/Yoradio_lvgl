// Build-family selector: picks the SoC-family rule set from sdkconfig.
// Выбор набора правил семейства SoC по sdkconfig.
//
// Purpose / Назначение:
//   The family layer holds build facts only (usable-GPIO rule, supported display
//   transports, SoC-level sdkconfig requirements). It carries no settings and no
//   board values. Board profiles stay in platform/board/.
//   Слой семейства хранит только факты сборки (правило допустимых GPIO, доступные
//   транспорты дисплея, требования sdkconfig уровня SoC). Настроек и значений плат
//   в нём нет: профили плат остаются в platform/board/.
//
// Ownership / lifecycle: compile-time only, no runtime state. Included from
// board_validate.h and storage_validate.h after sdkconfig.h.
// Владение: только время компиляции, состояния нет. Подключается из
// board_validate.h и storage_validate.h после sdkconfig.h.
//
// sdkconfig.h is included explicitly: options.h can be parsed before Arduino.h,
// when CONFIG_* symbols are not guaranteed to exist (see board_validate.h).
// sdkconfig.h подключается явно: options.h иногда обрабатывается раньше Arduino.h.
#ifndef YORADIO_PLATFORM_FAMILY_FAMILY_SELECT_H
#define YORADIO_PLATFORM_FAMILY_FAMILY_SELECT_H

#include "sdkconfig.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#  include "esp32s3/family_rules.h"
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
#  include "esp32p4/family_rules.h"
#else
#  error "YoRadio family layer: unsupported SoC. Expected CONFIG_IDF_TARGET_ESP32S3 or CONFIG_IDF_TARGET_ESP32P4 from sdkconfig. Check the PlatformIO environment."
#endif

#endif  // YORADIO_PLATFORM_FAMILY_FAMILY_SELECT_H
