# compat — migration guards и временный touch bridge

Author: Witaliy76 - https://github.com/Witaliy76

Правила выбора платы и Custom Wiring — в [Board Contract](../README.md).

На штатной 4848S040 ручные `DISP_*` определения не нужны. Их задают только при физическом переназначении линий.

`legacy_macros.h` подключается из `src/src/core/options.h` после `board_select.h`. Дисплей читает типизированный `kResolvedDisplayConfig`; этот файл проверяет старые пользовательские GPIO имена и временно предоставляет touch aliases.

## Какие имена есть

Дисплейные aliases удалены в S04-4. Эти 24 имени теперь допускаются только в сообщениях об ошибке миграции:

`ST7701_B0`…`ST7701_B4`, `ST7701_G0`…`ST7701_G5`, `ST7701_R0`…`ST7701_R4`, `ST7701_HSYNC`, `ST7701_VSYNC`, `ST7701_DE`, `ST7701_PCLK`, `ST7701_CS`, `ST7701_SCK`, `ST7701_SDA`, `ST7701_BL`.

Тач, 4 временных имени до S05, читают `yoradio::kWiring.touch`:

`TS_SDA`, `TS_SCL`, `TS_INT`, `TS_RST`.

`ST7701_BOOT_ORIENTATION_180` здесь нет. Это политика ориентации в `src/myoptions.h`, не пины.

Если в `myoptions.h` осталось старое `#define ST7701_*` или `TS_*`, заголовок останавливает сборку и просит имя `DISP_*` или `TOUCH_*`.

## Почему TS_* нельзя читать в `#if`

Каждый touch alias раскрывается в поле `yoradio::kWiring.touch`. Так Custom Wiring доходит до драйвера без второй копии таблицы.

В `#if` такое C++ выражение не является числом. Для отсутствия INT/RST драйвер сравнивает 255 в обычном коде. `I2S_*` и `WAKE_PIN` остаются обычными макросами в `myoptions.h`.

## Когда мост убрать

S04-4: display aliases удалены, migration guards сохранены.
S05: драйвер тача перейдёт на `kWiring.touch`, затем `TS_*` aliases можно удалить.

До S05 touch bridge остаётся обязательным. Новый код не должен заводить ещё один слой синонимов.
