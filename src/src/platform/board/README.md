# Board Contract

## English

Board Contract is the compile-time source of selected-board facts and effective
display/touch wiring. The current production board is ESP32-S3 4848S040.

### Core model

| Value | Meaning |
|---|---|
| BoardProfile | Immutable factory facts: identity, build requirements, display/touch wiring and hardware presence |
| BoardDefaults | Tested recommendations and explicit UI policy: external-I2S pins, wake, display host policy, touch servicing and `ui.profile` |
| kActiveBoard | Selected factory BoardProfile |
| kWiring | Effective wiring: factory profile with only enabled DISP_* / TOUCH_* overrides applied |
| kActiveBoardDefaults | Defaults for the selected profile |

`BoardDefaults.ui.profile` is application UI policy consumed by
[ResolvedUiMetrics](../../lvgl_ui/README.md#ui-contract--ui-контракт); panel
geometry remains in the Display Contract.

BoardProfile is not changed by user defines. BoardDefaults is not a hardware
truth or a complete runtime configuration. Display uses ResolvedDisplayConfig;
touch uses ResolvedTouchConfig. External I2S and wake remain macros because
their consumers require preprocessor values.

For a one-off rewiring of the same supported ST7701/GT911 board, Custom Wiring
is sufficient for the exposed `DISP_*`/`TOUCH_*` GPIO overrides. A distinct
factory board variant that needs its own identity or defaults needs a new
BoardProfile and validation; new controllers or resolutions additionally need
their own supported contracts/backends. External I2S pins remain user macros.

### Select and configure an existing board

In src/myoptions.h:

    #define YORADIO_BOARD YORADIO_BOARD_4848S040
    #define YORADIO_CUSTOM_WIRING 0

The PlatformIO environment (for example, 4848S040) selects build-family
properties such as SoC, flash, PSRAM and partitions; it does not select a
BoardProfile. board_validate.h checks that the profile agrees with that build.

For a physically rewired display or GT911, set Custom Wiring to 1 and uncomment
only changed DISP_* or TOUCH_* entries. Unnamed lines stay factory values:

    #define YORADIO_CUSTOM_WIRING 1
    #define TOUCH_SDA 19

Original `DISP_*`/`TOUCH_*` override values and active I2S values are range-checked
before conversion to `uint8_t`. The validator rejects a GPIO used twice among display, touch and
`I2S_DOUT` / `I2S_BCLK` / `I2S_LRC`; ESP32-S3 numbers 22–37, negative and
out-of-range values on `DISP_*`, `TOUCH_*` and active I2S macros; a required
display or touch line set to 255; and a `DISP_*` or `TOUCH_*` override while
the flag is off. It does not check `MUTE_PIN`, `WAKE_PIN`,
IR or SD against the display. It cannot prove a physical wire or that a pin
is free.

### External I2S DAC

Change `I2S_DOUT`, `I2S_BCLK` and `I2S_LRC` in `src/myoptions.h`. Do not edit
BoardProfile, Custom Wiring, or `BoardDefaults.audio`: the audio and wake
fields there are recommendations and are not applied. The 4848S040 rear
connector uses DOUT 40, BCLK 1 and LRC 2. Those three signals are ESP32
outputs in I2S master mode: DOUT to the DAC DIN/DATA pin, BCLK to BCK, LRC to
LRCK/WS. No MCLK pin is driven. The validator checks the original I2S values,
reserved GPIO and collisions with effective display/touch and other I2S pins.
Each explicit 255 keeps its existing no-claim meaning. DOUT=255 disables the
external I2S path; the current player still requires an audio backend. With
active DOUT, BCLK/LRC=255 is passed to the driver as 255, not I2S_GPIO_UNUSED;
acceptance by the GPIO contract does not prove a working audio configuration.
Serial `bootInfo` prints the compiled `i2s-dout`,
`i2s-bclk` and `i2s-lrc` values. That log does not prove the I2S driver
accepted the pins. Verify the DAC, voltage levels and remaining board
functions on hardware. Rebuild and flash your own firmware; `build_bin/`
packages do not contain `myoptions.h` edits.

### GT911 and display GPIO

Display and GT911 GPIO use `DISP_*` and `TOUCH_*` under Custom Wiring. See
[Touch Contract](../touch/README.md). `TS_*` and `ST7701_*` names are
migration errors. The profile owns the GT911 address (`0x5D` on 4848S040);
Custom Wiring does not override it. Panel timing and 480×480 geometry stay in
PanelMode. There is no `DISP_*` line for panel RESX, and the backend requires
that reset to stay disconnected. `TOUCH_INT` does not select interrupt mode.
Serial shows `wiring: custom` and `backlight-gpio`, not the full RGB or
SDA/SCL map.

### Add a board

Adding a board is programming work:

1. Allocate a stable BoardId in board_id.h.
2. Add profiles/<board>/board_profile.h and board_defaults.h.
3. Add the selection branch in board_select.h.
4. Extend board_validate.h and add a real PlatformIO build environment if the
   SoC/flash/PSRAM family differs.
5. Supply/qualify matching display and touch implementations, then perform
   hardware validation.

The same ST7701 RGB565 480×480 and GT911 keep the current backends. A
BoardProfile cannot by itself add a new panel, resolution, or controller.
The current GPIO range and duplicate checks sit inside the 4848S040 branch.
See [profiles README](profiles/README.md) and [display README](../display/README.md).

### Compatibility

Old ST7701_* and TS_* wiring names are intentionally rejected by
compat/legacy_macros.h. ST7701_BOOT_ORIENTATION_180 is an allowed display
policy choice, not a GPIO alias. See [compat README](compat/README.md).

## Русский

Board Contract — источник фактов выбранной платы и итоговой разводки дисплея/тача,
проверяемых при сборке. Текущая поддерживаемая плата — ESP32-S3 4848S040.

### Основная модель

| Значение | Смысл |
|---|---|
| BoardProfile | Неизменяемые заводские факты: идентичность платы, требования к сборке, разводка дисплея/тача, наличие оборудования |
| BoardDefaults | Проверенные рекомендации и явный выбор UI: внешний I2S, пробуждение, политика дисплея, обслуживание тача и `ui.profile` |
| kActiveBoard | Выбранный заводской BoardProfile |
| kWiring | Итоговая разводка: заводской профиль с применёнными изменениями DISP_* / TOUCH_* |
| kActiveBoardDefaults | Настройки выбранного профиля |

`BoardDefaults.ui.profile` — выбор UI-профиля приложения для
[ResolvedUiMetrics](../../lvgl_ui/README.md#ui-contract--ui-контракт); геометрия
панели остаётся в Display Contract.

Пользовательские макросы не меняют BoardProfile. BoardDefaults не описывает
физические факты и не является полной конфигурацией работающего устройства.
Дисплей использует ResolvedDisplayConfig, тач — ResolvedTouchConfig. Внешний I2S
и пробуждение остаются макросами: использующий их код требует значений препроцессора.

Для разового переподключения той же поддерживаемой платы ST7701/GT911 достаточно
Custom Wiring для доступных GPIO `DISP_*`/`TOUCH_*`. Отдельному заводскому
варианту платы со своим идентификатором или настройками нужны новый BoardProfile и его
валидация; новые контроллеры или разрешения дополнительно требуют поддержанных
контрактов и драйверов. Пины внешнего I2S остаются пользовательскими макросами.

### Выбор и настройка существующей платы

В src/myoptions.h:

    #define YORADIO_BOARD YORADIO_BOARD_4848S040
    #define YORADIO_CUSTOM_WIRING 0

Окружение PlatformIO (например, 4848S040) задаёт параметры сборки: SoC, flash,
PSRAM и таблицу разделов; оно не выбирает BoardProfile. board_validate.h
проверяет, что профиль соответствует этой сборке.

Для физически переподключённого дисплея или GT911 установите Custom Wiring в 1
и раскомментируйте только изменённые DISP_* или TOUCH_* строки. Неназванные
линии сохраняют заводские значения:

    #define YORADIO_CUSTOM_WIRING 1
    #define TOUCH_SDA 19

Исходные значения `DISP_*`/`TOUCH_*` и активных I2S-макросов проверяются до
преобразования в `uint8_t`. Валидатор отвергает GPIO, занятый дважды среди дисплея, тача и
`I2S_DOUT` / `I2S_BCLK` / `I2S_LRC`; номера ESP32-S3 22–37, отрицательные и
выходящие за диапазон значения `DISP_*`, `TOUCH_*` и активных I2S-макросов;
обязательную линию дисплея или тача со значением 255; override `DISP_*` или
`TOUCH_*` при выключенном флаге. `MUTE_PIN`, `WAKE_PIN`, IR и SD с дисплеем
он не сверяет. Корректность физической разводки и свободу непроверяемых пинов он не доказывает.

### Внешний I2S ЦАП

Меняйте `I2S_DOUT`, `I2S_BCLK`, `I2S_LRC` в `src/myoptions.h`. Не правьте
BoardProfile, Custom Wiring и `BoardDefaults.audio`: поля audio и wake там —
рекомендации, и прошивка их не применяет. Задний разъём 4848S040: DOUT 40,
BCLK 1, LRC 2. Эти три сигнала — выходы ESP32 в режиме I2S master: DOUT на
DIN/DATA ЦАП, BCLK на BCK, LRC на LRCK/WS. Пин MCLK не выводится. Валидатор
проверяет исходные I2S-значения, зарезервированные GPIO и конфликты с итоговой
разводкой дисплея/тача и другими I2S-пинами. Явный 255 исключает сигнал из
списка занятых GPIO. DOUT=255 отключает внешний I2S-тракт; текущему плееру всё
ещё нужен звуковой драйвер.
При активном DOUT значение BCLK/LRC=255 передаётся драйверу как 255, а не
I2S_GPIO_UNUSED; принятие GPIO-контрактом не доказывает работоспособность звукового тракта.
Serial `bootInfo`
печатает собранные значения `i2s-dout`, `i2s-bclk`, `i2s-lrc`. Этот лог не
доказывает, что драйвер I2S принял пины. ЦАП, уровни напряжения и остальные
функции платы проверяйте на устройстве. Нужна своя сборка и прошивка; пакеты
`build_bin/` правки `myoptions.h` не содержат.

### GT911 и GPIO дисплея

GPIO дисплея и GT911 задаются `DISP_*` и `TOUCH_*` через Custom Wiring. См.
[Touch Contract](../touch/README.md). Имена `TS_*` и `ST7701_*` — ошибки
миграции. Профиль задаёт адрес GT911 (`0x5D` на 4848S040); Custom Wiring
его не меняет. Тайминги и геометрия 480×480 остаются в PanelMode. Строки
`DISP_*` для RESX нет, драйвер требует, чтобы сброс панели оставался
неподключённым. `TOUCH_INT` не выбирает режим прерывания. Serial показывает
`wiring: custom` и `backlight-gpio`, не полную карту RGB или SDA/SCL.

### Добавление платы

Добавление платы требует изменения кода:

1. Выделите стабильный BoardId в board_id.h.
2. Добавьте profiles/<board>/board_profile.h и board_defaults.h.
3. Добавьте ветку выбора в board_select.h.
4. Расширьте board_validate.h и добавьте отдельное окружение PlatformIO,
   если отличается семейство SoC/flash/PSRAM.
5. Подключите и проверьте соответствующие драйверы дисплея/тача, затем
   проведите проверку на устройстве.

Те же ST7701 RGB565 480×480 и GT911 используют текущие драйверы.
BoardProfile сам по себе не добавляет новую панель, разрешение или контроллер.
Текущие проверки диапазона GPIO и дубликатов находятся внутри ветки 4848S040.
См. [profiles README](profiles/README.md) и [display README](../display/README.md).

### Совместимость

Старые имена GPIO ST7701_* и TS_* намеренно отвергаются
compat/legacy_macros.h. ST7701_BOOT_ORIENTATION_180 — допустимая настройка
дисплея, а не псевдоним GPIO. См. [описание совместимости](compat/README.md).
