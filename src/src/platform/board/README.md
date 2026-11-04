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

The validator rejects a GPIO used twice among display, touch and
`I2S_DOUT` / `I2S_BCLK` / `I2S_LRC`; ESP32-S3 numbers 22–37 and numbers above
48 on `DISP_*` and `TOUCH_*` only; a required display or touch line set to
255; and a `DISP_*` or `TOUCH_*` override while the flag is off. It does not
range-check the three I2S macros. It does not check `MUTE_PIN`, `WAKE_PIN`,
IR or SD against the display. It cannot prove a physical wire or that a pin
is free.

### External I2S DAC

Change `I2S_DOUT`, `I2S_BCLK` and `I2S_LRC` in `src/myoptions.h`. Do not edit
BoardProfile, Custom Wiring, or `BoardDefaults.audio`: the audio and wake
fields there are recommendations and are not applied. The 4848S040 rear
connector uses DOUT 40, BCLK 1 and LRC 2. Those three signals are ESP32
outputs in I2S master mode: DOUT to the DAC DIN/DATA pin, BCLK to BCK, LRC to
LRCK/WS. No MCLK pin is driven. The validator checks collisions with effective
display/touch pins only. Serial `bootInfo` prints the compiled `i2s-dout`,
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

Board Contract — compile-time источник фактов выбранной платы и effective
разводки display/touch. Текущая production-плата — ESP32-S3 4848S040.

### Основная модель

| Значение | Смысл |
|---|---|
| BoardProfile | Неизменяемые заводские факты: identity, build requirements, display/touch wiring, наличие hardware |
| BoardDefaults | Проверенные рекомендации и явная UI-policy: external-I2S pins, wake, display host policy, touch servicing и `ui.profile` |
| kActiveBoard | Выбранный заводской BoardProfile |
| kWiring | Effective wiring: factory profile с наложенными только включёнными DISP_* / TOUCH_* override |
| kActiveBoardDefaults | Defaults выбранного profile |

`BoardDefaults.ui.profile` — UI-policy приложения для
[ResolvedUiMetrics](../../lvgl_ui/README.md#ui-contract--ui-контракт); геометрия
панели остаётся в Display Contract.

BoardProfile не меняется user defines. BoardDefaults не является hardware truth
или полной runtime configuration. Display использует ResolvedDisplayConfig;
touch — ResolvedTouchConfig. External I2S и wake остаются macros, потому что их
consumers требуют preprocessor values.

Для разового переподключения той же поддерживаемой платы ST7701/GT911 достаточно
Custom Wiring для доступных GPIO `DISP_*`/`TOUCH_*`. Отдельному заводскому
варианту платы со своей identity или defaults нужны новый BoardProfile и его
валидация; новые контроллеры или разрешения дополнительно требуют поддержанных
контрактов/backend. Пины внешнего I2S остаются пользовательскими macros.

### Выбор и настройка существующей платы

В src/myoptions.h:

    #define YORADIO_BOARD YORADIO_BOARD_4848S040
    #define YORADIO_CUSTOM_WIRING 0

PlatformIO environment (например, 4848S040) выбирает build-family properties:
SoC, flash, PSRAM, partitions; он не выбирает BoardProfile. board_validate.h
проверяет, что profile соответствует этой сборке.

Для физически переподключённого display или GT911 установите Custom Wiring в 1
и раскомментируйте только изменённые DISP_* или TOUCH_* строки. Неназванные
линии сохраняют заводские значения:

    #define YORADIO_CUSTOM_WIRING 1
    #define TOUCH_SDA 19

Validator отвергает GPIO, занятый дважды среди дисплея, тача и
`I2S_DOUT` / `I2S_BCLK` / `I2S_LRC`; номера ESP32-S3 22–37 и номера больше 48
только у `DISP_*` и `TOUCH_*`; обязательную линию дисплея или тача со
значением 255; override `DISP_*` или `TOUCH_*` при выключенном flag. Диапазон
трёх макросов I2S он не проверяет. `MUTE_PIN`, `WAKE_PIN`, IR и SD с дисплеем
он не сверяет. Физический провод и свободу пина он не доказывает.

### Внешний I2S ЦАП

Меняйте `I2S_DOUT`, `I2S_BCLK`, `I2S_LRC` в `src/myoptions.h`. Не правьте
BoardProfile, Custom Wiring и `BoardDefaults.audio`: поля audio и wake там —
рекомендации, и прошивка их не применяет. Задний разъём 4848S040: DOUT 40,
BCLK 1, LRC 2. Эти три сигнала — выходы ESP32 в режиме I2S master: DOUT на
DIN/DATA ЦАП, BCLK на BCK, LRC на LRCK/WS. Пин MCLK не выводится. Validator
проверяет только пересечение с effective display/touch pins. Serial `bootInfo`
печатает собранные значения `i2s-dout`, `i2s-bclk`, `i2s-lrc`. Этот лог не
доказывает, что драйвер I2S принял пины. ЦАП, уровни напряжения и остальные
функции платы проверяйте на hardware. Нужна своя сборка и прошивка; пакеты
`build_bin/` правки `myoptions.h` не содержат.

### GT911 и GPIO дисплея

GPIO дисплея и GT911 задаются `DISP_*` и `TOUCH_*` через Custom Wiring. См.
[Touch Contract](../touch/README.md). Имена `TS_*` и `ST7701_*` — ошибки
миграции. Profile владеет адресом GT911 (`0x5D` на 4848S040); Custom Wiring
его не меняет. Timing и геометрия 480×480 остаются в PanelMode. Строки
`DISP_*` для RESX нет, backend требует, чтобы сброс панели оставался
неподключённым. `TOUCH_INT` не выбирает режим прерывания. Serial показывает
`wiring: custom` и `backlight-gpio`, не полную карту RGB или SDA/SCL.

### Добавление платы

Добавление платы — programming work:

1. Выделите стабильный BoardId в board_id.h.
2. Добавьте profiles/<board>/board_profile.h и board_defaults.h.
3. Добавьте ветку выбора в board_select.h.
4. Расширьте board_validate.h и добавьте реальный PlatformIO build environment,
   если отличается семейство SoC/flash/PSRAM.
5. Добавьте/квалифицируйте matching display/touch implementations, затем
   проведите hardware validation.

Те же ST7701 RGB565 480×480 и GT911 остаются на текущих backend.
BoardProfile сам по себе не добавляет новую panel, resolution или controller.
Текущие проверки диапазона GPIO и дубликатов находятся внутри ветки 4848S040.
См. [profiles README](profiles/README.md) и [display README](../display/README.md).

### Совместимость

Старые wiring names ST7701_* и TS_* намеренно отвергаются
compat/legacy_macros.h. ST7701_BOOT_ORIENTATION_180 — допустимый display policy
choice, не GPIO alias. См. [compat README](compat/README.md).
