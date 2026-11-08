# YoRadio platform architecture

Author: Witaliy76 - https://github.com/Witaliy76

## English

`src/src/platform/` is the compile-time hardware boundary. It turns a selected
board profile and optional wiring overrides into typed values consumed by the
display and touch implementations. It is not a UI, theme, persistence, or
generic hardware framework.

### Map

```text
src/myoptions.h
  -> platform/board: BoardProfile + BoardDefaults + kWiring
  -> platform/display: PanelMode + ResolvedDisplayConfig
  -> DisplayPort -> direct ST7701 / esp_lcd backend
  -> platform/touch: ResolvedTouchConfig -> Gt911Backend -> TouchPort
  -> lvgl_ui/lv_touch_indev.cpp -> LVGL pointer input
```

| Area | Start here | Responsibility |
|---|---|---|
| Board selection and GPIO overrides | [board/README.md](board/README.md) | `YORADIO_BOARD`, `kActiveBoard`, `kWiring`, validation |
| Existing board profiles | [board/profiles/README.md](board/profiles/README.md) | How a new profile is registered and what must be proven |
| 4848S040 wiring | [board profile](board/profiles/esp32s3_4848s040/README.md) | Factory GPIO tables and known board-specific constraints |
| Legacy-name migration errors | [board/compat/README.md](board/compat/README.md) | Why `ST7701_*` and `TS_*` names must not return |
| Display Contract | [display/README.md](display/README.md) | Panel recipe, effective display configuration and DisplayPort |
| Touch Contract | [touch/README.md](touch/README.md) | GT911, TouchPort and LVGL input path |
| UI Contract | [LVGL UI contract](../lvgl_ui/README.md#ui-contract--ui-контракт) | Explicit `BoardDefaults.ui.profile`, resolved viewport and UI metrics |

### Layers

| Name | Role | What the running firmware uses |
|---|---|---|
| `BoardProfile` / `kActiveBoard` | Factory facts of the selected board | Not changed by `myoptions.h` |
| Custom Wiring | `DISP_*` / `TOUCH_*` in `src/myoptions.h` when `YORADIO_CUSTOM_WIRING` is 1 | Only the lines you actually define |
| `kWiring` | Factory profile plus those overrides | Display and GT911 GPIO |
| `BoardDefaults` | Tested recommendations and explicit UI policy | Display host policy, touch poll / I2C clock and `ui.profile` are read. `audio` and `power` pins are not applied |
| Resolved display / touch config | Compile-time view for the backends | ST7701 480×480 recipe stays in PanelMode. Bounce, flash guard and boot orientation come from `myoptions.h` |

`I2S_DOUT`, `I2S_BCLK`, `I2S_LRC` and `WAKE_PIN` stay macros in `src/myoptions.h`.
Editing `BoardDefaults.audio` or `BoardDefaults.power` does not move the DAC or
the wake pin.

### Four common tasks

1. **Change an external I2S DAC.** Edit `I2S_DOUT`, `I2S_BCLK`, and `I2S_LRC`
   in `src/myoptions.h`, then build `[env:4848S040]` and flash that firmware.
   Packages in `build_bin/` ignore these edits. All three signals are ESP32
   outputs in I2S master mode: DOUT goes to the DAC DIN/DATA input, BCLK to
   BCK, LRC to LRCK/WS. The firmware does not drive an I2S data input or an
   MCLK pin. This is user configuration, not Custom Wiring. The validator
   checks each active I2S value before narrowing to `uint8_t`, rejects GPIO
   22–37 and values outside 0–48, and detects display/touch/I2S collisions.
   Explicit 255 omits that GPIO claim; it does not guarantee a working I2S
   pin combination (see the [I2S limits](board/README.md#external-i2s-dac)).
   The validator cannot prove the DAC or that an unchecked signal is free.
   After boot, Serial `bootInfo` prints `i2s-dout`, `i2s-bclk`
   and `i2s-lrc`. Those are the compiled macros, not proof that the I2S driver
   accepted the pins.
2. **Rewire the same ST7701 and GT911.** Set `YORADIO_CUSTOM_WIRING 1` in
   `src/myoptions.h`, then enable only the changed `DISP_*` or `TOUCH_*`
   lines. Unnamed lines stay factory values. Pins flow through `kWiring`.
   `ST7701_*` and `TS_*` names are rejected. Custom Wiring does not change
   the controller, 480×480 geometry, panel timing, the GT911 I2C address, or
   panel RESX. RESX has no `DISP_*` entry and the backend requires it to stay
   disconnected. Setting `TOUCH_INT` does not switch the touch to interrupt
   mode; the supported path stays polling. Serial prints `wiring: custom` and
   `backlight-gpio`. It does not print the RGB map or SDA/SCL.
3. **Add a board based on an existing profile.** This is implementation work:
   add an ID, a profile/defaults directory, a `board_select.h` branch and
   validation/build support. The same ST7701 RGB565 480×480 and GT911 do not
   need a new backend. A profile alone does not make another panel or touch
   controller supported. GPIO range and duplicate checks currently live inside
   the 4848S040 branch of `board_validate.h`.
4. **Use another panel or touch controller.** This needs a new tested backend
   and contract-compatible descriptor. No second backend is in the tree.
   Screen code stays behind DisplayPort; controller-specific touch code stays
   behind TouchPort. LVGL layout for another resolution is not built yet.

Only ESP32-S3 4848S040 with ST7701 RGB565 480x480 and GT911 is production
supported today. The `night_build` sources currently report
`0.9.434m-r2-lvgl-beta.3.08` in `YOVERSION`. That string is not a GitHub
Release. This documentation does not claim plug-and-play support for other
boards.

## Русский

`src/src/platform/` описывает аппаратные контракты, проверяемые при сборке.
Выбранный профиль платы и необязательные изменения разводки превращаются в
типизированные значения для драйверов дисплея и тача. Компоновка UI, тема и
хранение пользовательских настроек принадлежат другим модулям.

### Карта

```text
src/myoptions.h
  -> platform/board: BoardProfile + BoardDefaults + kWiring
  -> platform/display: PanelMode + ResolvedDisplayConfig
  -> DisplayPort -> прямой драйвер ST7701 / esp_lcd
  -> platform/touch: ResolvedTouchConfig -> Gt911Backend -> TouchPort
  -> lvgl_ui/lv_touch_indev.cpp -> ввод указателя LVGL
```

| Область | Начать здесь | Ответственность |
|---|---|---|
| Выбор платы и изменение GPIO | [board/README.md](board/README.md) | `YORADIO_BOARD`, `kActiveBoard`, `kWiring`, проверки |
| Существующие профили плат | [board/profiles/README.md](board/profiles/README.md) | Регистрация профиля и необходимое подтверждение |
| Разводка 4848S040 | [профиль платы](board/profiles/esp32s3_4848s040/README.md) | Заводские GPIO и известные ограничения платы |
| Ошибки миграции старых имён | [board/compat/README.md](board/compat/README.md) | Почему имена `ST7701_*` и `TS_*` не возвращаются |
| Display Contract | [display/README.md](display/README.md) | Параметры панели, итоговая конфигурация дисплея и DisplayPort |
| Touch Contract | [touch/README.md](touch/README.md) | GT911, TouchPort и путь ввода LVGL |
| UI Contract | [UI-контракт LVGL](../lvgl_ui/README.md#ui-contract--ui-контракт) | Явный `BoardDefaults.ui.profile`, физический размер экрана и UI-метрики |

### Слои

| Имя | Роль | Что использует работающая прошивка |
|---|---|---|
| `BoardProfile` / `kActiveBoard` | Заводские факты выбранной платы | `myoptions.h` их не меняет |
| Custom Wiring | `DISP_*` / `TOUCH_*` в `src/myoptions.h` при `YORADIO_CUSTOM_WIRING` 1 | Только реально заданные строки |
| `kWiring` | Заводской профиль с этими изменениями | GPIO дисплея и GT911 |
| `BoardDefaults` | Проверенные рекомендации и явный выбор UI | Читаются политика дисплея, режим опроса / частота I2C тача и `ui.profile`. Пины `audio` и `power` не применяются |
| ResolvedDisplayConfig / ResolvedTouchConfig | Итоговые настройки для драйверов, известные при сборке | Параметры ST7701 480×480 остаются в PanelMode. Флаги bounce-буфера, защиты записи flash и начальной ориентации берутся из `myoptions.h` |

`I2S_DOUT`, `I2S_BCLK`, `I2S_LRC` и `WAKE_PIN` остаются макросами в
`src/myoptions.h`. Правка `BoardDefaults.audio` или `BoardDefaults.power` не
переносит ЦАП и не меняет пин пробуждения.

### Четыре частых задачи

1. **Сменить внешний I2S ЦАП.** Измените `I2S_DOUT`, `I2S_BCLK`, `I2S_LRC` в
   `src/myoptions.h`, соберите `[env:4848S040]` и прошейте эту прошивку.
   Пакеты в `build_bin/` эти правки игнорируют. Все три сигнала — выходы ESP32
   в режиме I2S master: DOUT идёт на DIN/DATA ЦАП, BCLK на BCK, LRC на
   LRCK/WS. Прошивка не использует вход данных I2S и не выводит MCLK. Это
   пользовательская настройка, не Custom Wiring. Валидатор отвергает
   исходное значение каждого активного I2S-макроса вне 0–48 до преобразования
   в `uint8_t`, запрещает GPIO 22–37 и находит конфликты дисплея, тача и I2S.
   Явный 255 исключает сигнал из списка занятых GPIO, но не гарантирует
   работоспособность сочетания I2S-пинов (см. [ограничения I2S](board/README.md#внешний-i2s-цап)).
   Проверка не доказывает исправность ЦАП или свободу непроверяемых сигналов.
   После старта Serial `bootInfo` печатает `i2s-dout`, `i2s-bclk` и
   `i2s-lrc`. Это макросы, с которыми собрана прошивка, а не подтверждение,
   что драйвер I2S принял пины.
2. **Переразвести тот же ST7701 и GT911.** Установите `YORADIO_CUSTOM_WIRING 1`
   в `src/myoptions.h` и включите только изменившиеся строки `DISP_*` или
   `TOUCH_*`. Неназванные линии остаются заводскими. Пины попадают в `kWiring`.
   Имена `ST7701_*` и `TS_*` отвергаются. Custom Wiring не меняет контроллер,
   геометрию 480×480, тайминги панели, адрес I2C GT911 и RESX. У RESX нет строки
   `DISP_*`, драйвер требует, чтобы сброс панели оставался неподключённым.
   Задание `TOUCH_INT` не включает режим прерывания; поддерживаемый путь —
   опрос. Serial печатает `wiring: custom` и `backlight-gpio`. Карту RGB и
   SDA/SCL этот лог не печатает.
3. **Добавить плату по образцу профиля.** Нужна реализация: ID, каталог
   профиля с заводскими фактами и настройками, ветка `board_select.h`, проверки
   и совместимая конфигурация сборки. Те же ST7701 RGB565 480×480 и GT911
   нового драйвера не требуют. Профиль сам по себе не добавляет поддержку
   другой панели или контроллера тача. Проверки
   диапазона GPIO и дубликатов сейчас живут внутри ветки 4848S040 в
   `board_validate.h`.
4. **Использовать другую панель или контроллер тача.** Нужны новый проверенный
   драйвер и совместимое с контрактом описание оборудования. Второго драйвера
   панели или тача в дереве нет. Код экранов обращается к DisplayPort, код
   конкретного контроллера тача скрыт за TouchPort. Компоновка LVGL под другое
   разрешение ещё не реализована.

Сейчас рабочая прошивка поддерживает только ESP32-S3 4848S040 со ST7701 RGB565
480x480 и GT911. Исходники `night_build` сейчас сообщают
`0.9.434m-r2-lvgl-beta.3.08` в `YOVERSION`. Эта строка не является GitHub
Release. Документ не заявляет plug-and-play поддержку других плат.
