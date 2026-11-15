# YoRadio platform architecture

Author: Witaliy76 - https://github.com/Witaliy76

## English

`src/src/platform/` defines hardware contracts and the Storage mount lifecycle.
It turns a selected board profile and optional wiring overrides into typed
values for display/touch backends. Storage Owner and the read-only Audio FS
bridge are implemented; their enabled device integration is still gated.
UI, theme and user-settings persistence belong to other modules.

### Map

```text
src/myoptions.h
  -> platform/board: BoardProfile + BoardDefaults + kWiring
  -> platform/display: PanelMode + ResolvedDisplayConfig
  -> DisplayPort -> direct ST7701 / esp_lcd backend
  -> platform/touch: ResolvedTouchConfig -> Gt911Backend -> TouchPort
  -> platform/storage: ResolvedStorageConfig -> StorageOwner -> StorageFsBridge / Directory Service
  -> Audio File/decoder; Player owns path commands and epoch (stock SD disabled)
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
| Storage Contract | [storage/README.md](storage/README.md) | Mount owner, Audio FS bridge, per-folder Directory Service; stock SD disabled |
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

### Storage entry points

`main.cpp` calls `StorageOwner::prepareBeforeDisplay()`,
`mountAfterDisplay()` and `processFault()` on the setup/loop task. The owner
controls mount generation and operation/handle accounting; `CloseLease` permits
counted close after new operations are prohibited, without treating unknown
close as success. The read-only `StorageFsBridge` exposes the existing mount to
Audio; Audio owns File/decoder stop. Player owns `PathPlayRequest`, epoch,
MP3/FLAC policy and command results. Player confirms stop/close before Radio↔SD
or SD↔SD transitions; `PlaybackContext` and Config mode change only on confirmed
playback. Control runs without Wi-Fi; ordinary STOP leaves SD mounted.
Shared UTF-8 validation is in `utils/utf8.h`. Directory Service lists one
folder on task `S07Dir` and returns an immutable snapshot for `WebUi`,
`Lvgl`, or `Playback`. The engineering WebUI and the folder-queue next-track
search are the current callers; LVGL does not. Path SD Next/EOF/Prev live in
`core/folder_queue.*` and stay independent of the browse cwd. Detail is in
the storage README.

`core/source_catalog.h` is the static source list. It registers only Radio
(`radio`, station list) and SD (`sd`, directory). Availability is read from
the existing station count and Storage status; the catalog does not keep a
second copy. WebUI reads that list through read-only `GET /sources` and opens
Radio playlist or the existing SD folder browser without calling
`Player::activate`. Browser navigation is tab-local RAM only. `Player::activate`
admits one switch at a time. `Pending` means the request was accepted and is
not Playing. A second call while that request is still in flight returns
`Busy` and does not move the playback epoch. A missing bookmark file is
`NeedsSelection`: the current sound keeps playing. The sounding source is
only the confirmed Playing context. After SmartStart 2 selects a stopped
source, Next and Prev follow that source. The last SD file that actually
started is a RAM bookmark: it survives STOP and Radio, and reboot drops it.
The bookmark's mount generation must still match; a new card does not inherit
the old path.

USB would be another directory entry, with its own mount generation and the
same path playback. Bluetooth would be a control panel: no file path and no
folder queue. Neither is registered, and there is no playback stub for either.

The ordinary `4848S040` build enables the Storage Owner. Legacy `USE_SD` and
SDManager stay off. The disposable SDSPI probe sources are gone. Build and flash are described in
[S07 working build](storage/README.md#s07-working-build). Device acceptance remains pending; probe housekeeping
follows integration. Operation accounting does not guarantee synchronous FS
completion time. See [Storage roles and limits](storage/README.md).

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
supported today. `YOVERSION` in `core/options.h` identifies the current local
engineering build, not a GitHub Release or device acceptance. This documentation
does not claim plug-and-play support for other boards.

## Русский

`src/src/platform/` описывает аппаратные контракты и жизненный цикл монтирования
Storage. Выбранный профиль платы и необязательные изменения разводки превращаются
в типизированные значения для драйверов дисплея и тача. Storage Owner и адаптер
Audio FS только для чтения реализованы; включение на устройстве ещё ограничено.
Компоновка UI, тема и хранение пользовательских настроек принадлежат другим модулям.

### Карта

```text
src/myoptions.h
  -> platform/board: BoardProfile + BoardDefaults + kWiring
  -> platform/display: PanelMode + ResolvedDisplayConfig
  -> DisplayPort -> прямой драйвер ST7701 / esp_lcd
  -> platform/touch: ResolvedTouchConfig -> Gt911Backend -> TouchPort
  -> platform/storage: ResolvedStorageConfig -> StorageOwner -> StorageFsBridge / Directory Service
  -> Audio File/декодер; Player владеет командами и epoch (штатная SD выключена)
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
| Storage Contract | [storage/README.md](storage/README.md) | Владелец mount, адаптер Audio FS, Directory Service одной папки; штатная SD выключена |
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

### Точки входа Storage

`main.cpp` вызывает `StorageOwner::prepareBeforeDisplay()`,
`mountAfterDisplay()` и `processFault()` в задаче setup/loop. Владелец управляет
поколением монтирования и учётом операций/дескрипторов. `CloseLease` допускает
учитываемое закрытие после запрета новых операций, сохраняя ошибку при неизвестном
результате close. `StorageFsBridge` предоставляет существующий mount для чтения
Audio; остановкой File/декодера владеет Audio. Player владеет `PathPlayRequest`,
epoch, правилами MP3/FLAC и результатами команд. Перед переходом Radio↔SD или
SD↔SD Player подтверждает stop/close; источник в `PlaybackContext` и режим Config
меняются только после успешного запуска. Управление работает без Wi-Fi; обычный
STOP сохраняет mount SD. Общая проверка UTF-8 находится в `utils/utf8.h`.
Directory Service читает одну папку в задаче `S07Dir` и отдаёт неизменяемый
снимок для `WebUi`, `Lvgl` или `Playback`. Текущие вызывающие — инженерный
WebUI и поиск следующего трека folder-queue; LVGL — нет. Path SD
Next/EOF/Prev живут в `core/folder_queue.*` и не зависят от browse cwd.
Подробности — в README Storage.

`core/source_catalog.h` — статический список источников. Зарегистрированы только
Radio (`radio`, список станций) и SD (`sd`, каталог). Доступность читается из
числа станций и статуса Storage; второй копии каталог не хранит. WebUI читает
этот список через read-only `GET /sources` и открывает playlist Radio или
существующий SD folder browser без вызова `Player::activate`. Навигация
браузера — только RAM вкладки. `Player::activate` принимает одно переключение
за раз. `Pending` означает, что запрос принят, и это ещё не Playing. Повторный
вызов, пока запрос ещё исполняется, возвращает `Busy` и не двигает epoch.
Отсутствующий файл закладки — `NeedsSelection`: текущий звук продолжает
играть. Звучащий источник — только подтверждённый Playing. После выбора без
запуска при SmartStart 2 команды Next и Prev идут в выбранный источник.
Последний реально запущенный SD-файл хранится в RAM: он переживает STOP и
Radio и исчезает после reboot. Поколение mount закладки должно совпадать;
новая карта не наследует старый путь.

USB был бы ещё одной записью каталога со своим поколением mount и тем же
путевым воспроизведением. Bluetooth был бы панелью управления: без пути файла
и без folder queue. Ни один из них не зарегистрирован, заглушек воспроизведения нет.

Штатная сборка `4848S040` включает Storage Owner. Legacy `USE_SD` и SDManager
выключены. Исходники одноразового SDSPI probe удалены.
Сборка и прошивка описаны в
[рабочей сборке S07](storage/README.md#рабочая-сборка-s07). Приёмка на устройстве ещё не выполнена; уборка probe следует
после интеграции. Учёт операций не гарантирует время завершения синхронного FS.
Подробнее: [роли и ограничения Storage](storage/README.md).

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
480x480 и GT911. `YOVERSION` в `core/options.h` обозначает текущую локальную
инженерную сборку, не GitHub Release или приёмку на устройстве. Документ не
заявляет plug-and-play поддержку других плат.
