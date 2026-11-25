# YoRadio platform architecture

Author: Witaliy76 - https://github.com/Witaliy76

## English

`src/src/platform/` defines hardware contracts and the Storage mount lifecycle.
It turns a selected board profile and optional wiring overrides into typed
values for display/touch backends. Storage Owner and the read-only Audio FS
bridge are implemented and published on `night_build` as part of beta.3.10
(SD playback from FAT/exFAT). UI, theme and user-settings persistence belong
to other modules.

### Map

```text
src/myoptions.h
  -> platform/board: BoardProfile + BoardDefaults + kWiring
  -> platform/display: PanelMode + ResolvedDisplayConfig
  -> platform/composition.h: board -> PanelMode / display host / touch backend
  -> DisplayPort -> Rgb3Wire host (ST7701 or GC9503 init recipe) / esp_lcd
  -> platform/touch: ResolvedTouchConfig -> Gt911Backend or Ft5x06Backend -> TouchPort
  -> platform/storage: ResolvedStorageConfig -> StorageOwner -> StorageFsBridge / Directory Service
  -> Audio File/decoder; Player owns path commands and epoch (legacy USE_SD off)
  -> lvgl_ui/lv_touch_indev.cpp -> LVGL pointer input
```

| Area | Start here | Responsibility |
|---|---|---|
| Board selection and GPIO overrides | [board/README.md](board/README.md) | `YORADIO_BOARD`, `kActiveBoard`, `kWiring`, validation |
| Existing board profiles | [board/profiles/README.md](board/profiles/README.md) | How a new profile is registered and what must be proven |
| 4848S040 wiring | [board profile](board/profiles/esp32s3_4848s040/README.md) | Factory GPIO tables and known board-specific constraints |
| VIEWE UEDX48480040 wiring | [board profile](board/profiles/esp32s3_uedx48480040/README.md) | Factory GPIO, external DAC wiring by silkscreen labels, PCLK choice |
| Legacy-name migration errors | [board/compat/README.md](board/compat/README.md) | Why `ST7701_*` and `TS_*` names must not return |
| Display Contract | [display/README.md](display/README.md) | Panel recipe, effective display configuration and DisplayPort |
| Touch Contract | [touch/README.md](touch/README.md) | GT911 / FT5x06, TouchPort and LVGL input path |
| Storage Contract | [storage/README.md](storage/README.md) | Mount owner, Audio FS bridge, per-folder Directory Service; user switch `YORADIO_SD_ENABLE` |
| UI Contract | [LVGL UI contract](../lvgl_ui/README.md#ui-contract--ui-контракт) | Explicit `BoardDefaults.ui.profile`, resolved viewport and UI metrics |

### Layers

| Name | Role | What the running firmware uses |
|---|---|---|
| `BoardProfile` / `kActiveBoard` | Factory facts of the selected board | Not changed by `myoptions.h` |
| Custom Wiring | `DISP_*` / `TOUCH_*` in `src/myoptions.h` when `YORADIO_CUSTOM_WIRING` is 1 | Only the lines you actually define |
| `kWiring` | Factory profile plus those overrides | Display and touch GPIO |
| `BoardDefaults` | Tested recommendations and explicit UI policy | Display host policy, touch poll / I2C clock and `ui.profile` are read. I2S pins, builtin LED and RGB bounce / flash-guard flags are applied as defaults through `core/options.h`. `power` / wake is not applied |
| Resolved display / touch config | Compile-time view for the backends | The panel recipe (ST7701 or GC9503, 480×480) stays in PanelMode. Bounce and flash guard default to the board profile (`myoptions.h` may override them); boot orientation comes from `myoptions.h` |

`I2S_DOUT`, `I2S_BCLK` and `I2S_LRC` default to the board profile and are
overridden by uncommenting them in `src/myoptions.h`. `WAKE_PIN` stays a macro in
`src/myoptions.h`; editing `BoardDefaults.power` does not move the wake pin.

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
folder on task `StorageDir` and returns an immutable snapshot for `WebUi`,
`Lvgl`, or `Playback`. The callers are WebUI (`GET /sd/dir`), the LVGL
browser, and the folder-queue search. Path SD Next/EOF/Prev live in
`core/folder_queue.*` and stay independent of the browse cwd. Detail is in
the storage README.

`core/source_catalog.h` is the static source list. Radio (`radio`, station
list) and SD (`sd`, directory) are the only Implemented entries. Nine later
entries are Planned, in carousel order: USB, DLNA, NAS/SMB2, Yandex Music,
Spotify, AirPlay, Bluetooth, AUX, and I2S. Planned is not a temporarily
unavailable Radio or SD, and it has no playback stub. Availability of the
implemented entries is read from the existing station count and Storage
status; the catalog does not keep a second copy. WebUI `GET /sources` and the
media browser publish Implemented entries only. `Player::activate` admits one
switch at a time. `Pending` means the request was accepted and is not Playing.
A second call while that request is still in flight returns `Busy` and does
not move the playback epoch. A missing bookmark file is `NeedsSelection`: the
current sound keeps playing. The sounding source is only the confirmed Playing
context. After SmartStart 2 selects a stopped source, Next and Prev follow
that source. The last SD file that actually started is a RAM bookmark: it
survives STOP and Radio, and reboot drops it. The bookmark's mount generation
must still match; a new card does not inherit the old path.

USB, DLNA, and NAS/SMB2 are reserved Directory names. NAS means SMB2, not SMB1
and not WebDAV. Bluetooth and the other planned panels are ControlPanel: no
file path and no folder queue. None of them is activatable yet.

The ordinary `S3_N16R8` build enables the Storage Owner through the internal
flag `YORADIO_STORAGE_OWNER_ENABLED`. The user switch is `YORADIO_SD_ENABLE`
in `src/myoptions.h`. Legacy `USE_SD` and SDManager stay off. The disposable
SDSPI probe sources are gone. Build and flash are described in
[Working build (beta.3.10)](storage/README.md#working-build-beta-310).
Operation accounting does not guarantee synchronous FS completion time. See
[Storage roles and limits](storage/README.md). Historical S07/B083 engineering
notes remain in the storage README.

### Four common tasks

1. **Change an external I2S DAC.** Edit `I2S_DOUT`, `I2S_BCLK`, and `I2S_LRC`
   in `src/myoptions.h`, then build `[env:S3_N16R8]` and flash that firmware.
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
   add an ID, a profile/defaults directory, a `board_select.h` branch,
   validation, and the board → backend choice in `composition.h`. Boards with
   ST7701 or GC9503 RGB565 480×480 on a 3-wire command bus and GT911 or
   FT5x06-family touch reuse the existing backends; VIEWE UEDX48480040 is the
   worked example. A profile alone does not make another panel or touch
   controller supported. GPIO range and duplicate checks are common
   (`board_validate.h`) and apply to any board; factory-specific checks go into
   the profile's `board_validate_profile.h`.
4. **Use another panel or touch controller.** This needs a new tested backend
   and contract-compatible descriptor. A new init recipe for a panel on the same
   Rgb3Wire host is a PanelMode plus an `InitRecipe` entry; a different bus
   (SPI, MIPI-DSI, QSPI) needs a new display host. Screen code stays behind DisplayPort; controller-specific touch code stays
   behind TouchPort. LVGL layout for another resolution is not built yet.

Two boards are supported on the `S3_N16R8` family: ESP32-S3 4848S040 (ST7701
RGB565 480x480, GT911), which also has release packages, and VIEWE
UEDX48480040E-WB-A (GC9503 RGB565 480x480, FT6336U), device-verified and built
from source. `YOVERSION` in `core/options.h` is the published `night_build`
version string (beta.3.10), not a GitHub Release. This documentation does not
claim plug-and-play support for other boards.

## Русский

`src/src/platform/` описывает аппаратные контракты и жизненный цикл монтирования
Storage. Выбранный профиль платы и необязательные изменения разводки превращаются
в типизированные значения для драйверов дисплея и тача. Storage Owner и адаптер
Audio FS только для чтения реализованы и опубликованы на `night_build` в составе
beta.3.10 (воспроизведение с SD FAT/exFAT). Компоновка UI, тема и хранение
пользовательских настроек принадлежат другим модулям.

### Карта

```text
src/myoptions.h
  -> platform/board: BoardProfile + BoardDefaults + kWiring
  -> platform/display: PanelMode + ResolvedDisplayConfig
  -> platform/composition.h: плата -> PanelMode / хост дисплея / backend тача
  -> DisplayPort -> хост Rgb3Wire (рецепт ST7701 или GC9503) / esp_lcd
  -> platform/touch: ResolvedTouchConfig -> Gt911Backend или Ft5x06Backend -> TouchPort
  -> platform/storage: ResolvedStorageConfig -> StorageOwner -> StorageFsBridge / Directory Service
  -> Audio File/декодер; Player владеет командами и epoch (legacy USE_SD выключен)
  -> lvgl_ui/lv_touch_indev.cpp -> ввод указателя LVGL
```

| Область | Начать здесь | Ответственность |
|---|---|---|
| Выбор платы и изменение GPIO | [board/README.md](board/README.md) | `YORADIO_BOARD`, `kActiveBoard`, `kWiring`, проверки |
| Существующие профили плат | [board/profiles/README.md](board/profiles/README.md) | Регистрация профиля и необходимое подтверждение |
| Разводка 4848S040 | [профиль платы](board/profiles/esp32s3_4848s040/README.md) | Заводские GPIO и известные ограничения платы |
| Разводка VIEWE UEDX48480040 | [профиль платы](board/profiles/esp32s3_uedx48480040/README.md) | Заводские GPIO, подключение ЦАП по шелкографии, выбор PCLK |
| Ошибки миграции старых имён | [board/compat/README.md](board/compat/README.md) | Почему имена `ST7701_*` и `TS_*` не возвращаются |
| Display Contract | [display/README.md](display/README.md) | Параметры панели, итоговая конфигурация дисплея и DisplayPort |
| Touch Contract | [touch/README.md](touch/README.md) | GT911 / FT5x06, TouchPort и путь ввода LVGL |
| Storage Contract | [storage/README.md](storage/README.md) | Владелец mount, адаптер Audio FS, Directory Service одной папки; переключатель `YORADIO_SD_ENABLE` |
| UI Contract | [UI-контракт LVGL](../lvgl_ui/README.md#ui-contract--ui-контракт) | Явный `BoardDefaults.ui.profile`, физический размер экрана и UI-метрики |

### Слои

| Имя | Роль | Что использует работающая прошивка |
|---|---|---|
| `BoardProfile` / `kActiveBoard` | Заводские факты выбранной платы | `myoptions.h` их не меняет |
| Custom Wiring | `DISP_*` / `TOUCH_*` в `src/myoptions.h` при `YORADIO_CUSTOM_WIRING` 1 | Только реально заданные строки |
| `kWiring` | Заводской профиль с этими изменениями | GPIO дисплея и тача |
| `BoardDefaults` | Проверенные рекомендации и явный выбор UI | Читаются политика дисплея, режим опроса / частота I2C тача и `ui.profile`. Пины I2S, builtin LED и флаги bounce / защиты flash применяются как умолчания через `core/options.h`. `power` / пробуждение не применяются |
| ResolvedDisplayConfig / ResolvedTouchConfig | Итоговые настройки для драйверов, известные при сборке | Рецепт панели (ST7701 или GC9503, 480×480) остаётся в PanelMode. Флаги bounce-буфера и защиты записи flash по умолчанию берутся из профиля платы (`myoptions.h` может их переопределить); начальная ориентация берётся из `myoptions.h` |

`I2S_DOUT`, `I2S_BCLK`, `I2S_LRC` по умолчанию берутся из профиля платы и
переопределяются раскомментированием в `src/myoptions.h`. `WAKE_PIN` остаётся
макросом в `src/myoptions.h`; правка `BoardDefaults.power` не меняет пин пробуждения.

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
Directory Service читает одну папку в задаче `StorageDir` и отдаёт неизменяемый
снимок для `WebUi`, `Lvgl` или `Playback`. Вызывающие — WebUI
(`GET /sd/dir`), браузер LVGL и поиск folder-queue. Path SD
Next/EOF/Prev живут в `core/folder_queue.*` и не зависят от browse cwd.
Подробности — в README Storage.

`core/source_catalog.h` — статический список источников. Radio (`radio`, список
станций) и SD (`sd`, каталог) — единственные записи Implemented. Девять
следующих записей — Planned, в порядке карусели: USB, DLNA, NAS/SMB2, Яндекс
Музыка, Spotify, AirPlay, Bluetooth, AUX и I2S. Planned — не временно
недоступные Radio или SD, и у них нет заглушки воспроизведения. Доступность
реализованных записей читается из числа станций и статуса Storage; второй
копии каталог не хранит. WebUI `GET /sources` и медиабраузер публикуют только
Implemented. `Player::activate` принимает одно переключение за раз. `Pending`
означает, что запрос принят, и это ещё не Playing. Повторный вызов, пока
запрос ещё исполняется, возвращает `Busy` и не двигает epoch. Отсутствующий
файл закладки — `NeedsSelection`: текущий звук продолжает играть. Звучащий
источник — только подтверждённый Playing. После выбора без запуска при
SmartStart 2 команды Next и Prev идут в выбранный источник. Последний реально
запущенный SD-файл хранится в RAM: он переживает STOP и Radio и исчезает
после reboot. Поколение mount закладки должно совпадать; новая карта не
наследует старый путь.

USB, DLNA и NAS/SMB2 — зарезервированные имена Directory. NAS — это SMB2, не
SMB1 и не WebDAV. Bluetooth и остальные planned-панели — ControlPanel: без
пути файла и без folder queue. Активировать их пока нельзя.

Штатная сборка `S3_N16R8` включает Storage Owner внутренним флагом
`YORADIO_STORAGE_OWNER_ENABLED`. Пользовательский переключатель —
`YORADIO_SD_ENABLE` в `src/myoptions.h`. Legacy `USE_SD` и SDManager
выключены. Исходники одноразового SDSPI probe удалены.
Сборка и прошивка описаны в
[рабочей сборке (beta.3.10)](storage/README.md#рабочая-сборка-beta-310).
Учёт операций не гарантирует время завершения синхронного FS. Подробнее:
[роли и ограничения Storage](storage/README.md). Исторические заметки S07/B083
остаются в README Storage.

### Четыре частых задачи

1. **Сменить внешний I2S ЦАП.** Измените `I2S_DOUT`, `I2S_BCLK`, `I2S_LRC` в
   `src/myoptions.h`, соберите `[env:S3_N16R8]` и прошейте эту прошивку.
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
   и выбор backend'ов для платы в `composition.h`. Платам с ST7701 или GC9503
   RGB565 480×480 на 3-wire шине команд и тачем GT911 или семейства FT5x06
   новый драйвер не нужен; рабочий пример — VIEWE UEDX48480040. Профиль сам по
   себе не добавляет поддержку другой панели или контроллера тача. Проверки
   диапазона GPIO и дубликатов — общие (`board_validate.h`) и действуют для любой
   платы; заводские проверки платы пишутся в `board_validate_profile.h` её профиля.
4. **Использовать другую панель или контроллер тача.** Нужны новый проверенный
   драйвер и совместимое с контрактом описание оборудования. Новый рецепт
   инициализации для панели на том же хосте Rgb3Wire — это PanelMode и запись
   `InitRecipe`; другая шина (SPI, MIPI-DSI, QSPI) требует нового хоста дисплея.
   Код экранов обращается к DisplayPort, код
   конкретного контроллера тача скрыт за TouchPort. Компоновка LVGL под другое
   разрешение ещё не реализована.

На семействе `S3_N16R8` поддерживаются две платы: ESP32-S3 4848S040 (ST7701
RGB565 480x480, GT911), для неё есть и готовые пакеты, и VIEWE
UEDX48480040E-WB-A (GC9503 RGB565 480x480, FT6336U), проверенная на устройстве
и собираемая из исходников. `YOVERSION` в `core/options.h` — опубликованная
строка версии `night_build` (beta.3.10), не GitHub Release. Документ не
заявляет plug-and-play поддержку других плат.
