# compat — migration guards

Author: Witaliy76 - https://github.com/Witaliy76

Правила выбора платы и Custom Wiring — в [Board Contract](../README.md).

На штатной 4848S040 ручные `DISP_*` определения не нужны. Их задают только при физическом переназначении линий.

`legacy_macros.h` подключается из `src/src/core/options.h` после `board_select.h`. Display и touch production читают typed resolved configs; этот файл только проверяет старые пользовательские GPIO имена и выдаёт направленную ошибку миграции.

## Какие имена есть

Дисплейные aliases удалены в S04-4. Эти 24 имени теперь допускаются только в сообщениях об ошибке миграции:

`ST7701_B0`…`ST7701_B4`, `ST7701_G0`…`ST7701_G5`, `ST7701_R0`…`ST7701_R4`, `ST7701_HSYNC`, `ST7701_VSYNC`, `ST7701_DE`, `ST7701_PCLK`, `ST7701_CS`, `ST7701_SCK`, `ST7701_SDA`, `ST7701_BL`.

Touch aliases удалены в S05-4. Четыре имени теперь также допускаются только в сообщениях об ошибке миграции:

`TS_SDA`, `TS_SCL`, `TS_INT`, `TS_RST`.

`ST7701_BOOT_ORIENTATION_180` здесь нет. Это политика ориентации в `src/myoptions.h`, не пины.

Если в `myoptions.h` осталось старое `#define ST7701_*` или `TS_SDA` / `TS_SCL` / `TS_INT` / `TS_RST`, заголовок останавливает сборку и просит имя `DISP_*` или `TOUCH_*`.

## Где теперь брать touch wiring

Production читает SDA/SCL/INT/RST и I2C address из `yoradio::kWiring.touch` через `ResolvedTouchConfig`. Для обычного переназначения GPIO используются `TOUCH_*` в секции Custom Wiring; `TS_*` больше не являются конфигурационным интерфейсом.

Для отсутствия INT/RST backend сравнивает typed sentinel 255 в обычном коде и преобразует его в `GPIO_NUM_NC`. `I2S_*`, `WAKE_PIN` и фактически используемый общей SPI-конфигурацией `TS_HSPI` остаются обычными макросами.

## Когда мост убрать

S04-4: display aliases удалены, migration guards сохранены.
S05-1: production touch перешёл на `kWiring.touch` через `ResolvedTouchConfig`; aliases пока не удаляются.
S05-4: production consumers отсутствуют; touch aliases, generic pin fallbacks и alias-only assertions удалены. Migration guards сохранены, чтобы старые определения не игнорировались молча.

Новый код не должен возвращать этот мост или заводить ещё один слой синонимов.
