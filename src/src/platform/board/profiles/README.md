# Board profiles

## English

One directory represents one named board. A profile records factory facts; it
does not automatically create a supported display or touch implementation.

### Directory contract

    profiles/<board>/
      README.md          hardware facts and confirmed constraints
      board_profile.h    BoardProfile factory initializer
      board_defaults.h   tested recommendations required by board_select.h

### Add a profile

1. Add a unique YORADIO_BOARD_<NAME> macro and BoardId value in board_id.h.
2. Create board_profile.h and board_defaults.h. The selector currently requires
   both files.
3. Add the corresponding branch in board_select.h, publishing kActiveBoard and
   kActiveBoardDefaults.
4. Extend board_validate.h with board-specific assertions and use a matching
   PlatformIO environment.
5. Add/qualify PanelMode/backend and TouchPort backend when controller or
   transport differs; then validate hardware. The same ST7701 RGB565 480×480
   and GT911 keep the current backends.

DisplayWiring contains 16 RGB slots in fixed esp_lcd order B0..B4, G0..G5,
R0..R4. Missing hardware lines use kPinNotConnected (255), never GPIO0.
BoardDefaults records recommendations. Its display host policy and touch
service are read; its audio and wake pins are not a replacement for
`I2S_*` and `WAKE_PIN` in myoptions.h. The production profile is
[ESP32-S3 4848S040](esp32s3_4848s040/README.md).
Each board defaults file must explicitly select a compatible `ui.profile`;
see the [UI contract](../../../lvgl_ui/README.md#ui-contract--ui-контракт).

### Worked example: a new revision with the same ST7701 and GT911

Assume a revision still uses ESP32-S3, ST7701 RGB565 480×480 and GT911, but its
schematic routes signals differently. For a one-off build changing only exposed
`DISP_*`/`TOUCH_*` pins, keep `YORADIO_BOARD_4848S040` and use Custom Wiring in
`src/myoptions.h`. A separately named factory revision, or different defaults or
facts not exposed by Custom Wiring, needs a distinct BoardProfile.

The following is a documentation example, not an added or hardware-qualified profile.
No replacement GPIO map is proposed: use the actual revision schematic.

| File / step | Concrete edit for the example revision |
|---|---|
| [board_id.h](../board_id.h) | Register the currently unused macro `YORADIO_BOARD_4848S040_REV_B` as 2 and enum member `BoardId::Esp32S3_4848S040_RevB` with that macro value; never renumber existing IDs |
| `profiles/esp32s3_4848s040_rev_b/` | Copy the existing profile directory; give both headers distinct include guards and their own profile-specific macro names; document the actual revision |
| `board_profile.h` in that directory | Name the object `kBoardProfile_Esp32S3_4848S040_RevB`; update `identity.id`, `identity.name`, `identity.revision`, `identity.build` as needed and the real `display` / `touch` wiring fields (`data_gpio`, sync/command pins, `i2c_sda_gpio`, `i2c_scl_gpio`, `int_gpio`, `reset_gpio`); retain only facts confirmed for that revision |
| `board_defaults.h` in that directory | Name the object `kBoardDefaults_Esp32S3_4848S040_RevB` and update its assertions to reference it; keep the current compatible display/touch policy and explicitly select `.ui = { .profile = UiProfile::Square480 }` |
| [board_select.h](../board_select.h) | Add an `#elif YORADIO_BOARD == YORADIO_BOARD_4848S040_REV_B` branch, include the two new headers and bind `kActiveBoard` / `kActiveBoardDefaults` to those objects; retain the common `makeWiring(kActiveBoard)` path |
| [board_validate.h](../board_validate.h) | Extend board/display-selector consistency and apply required-line, raw-range, reserved-GPIO and duplicate checks to the new ID; adapt factory assertions to the new schematic instead of imposing old 4848S040 GPIO numbers |
| [compat/legacy_macros.h](../compat/legacy_macros.h) | Extend its board guard so the new ID also rejects retired GPIO names |
| `src/myoptions.h` and build | Select `YORADIO_BOARD_4848S040_REV_B`; use Custom Wiring 0 for the new factory map. Reuse `[env:4848S040]` only if its SoC/flash/PSRAM/partitions still match; otherwise add a suitable environment |

Selection still follows `options.h → myoptions.h → board_select.h`. The current
PanelMode/backends can serve this same panel/controller combination, with
`ResolvedDisplayConfig` checking compatibility and `ResolvedUiMetrics` selecting
Square480. This does not validate wiring, GT911 behaviour or the panel on the
new board: compile checks, firmware build and hardware checks are still required.
Square720/Wide are not implemented.

### Scope limits

Only the ST7701 RGB565 480x480 and GT911 combination is implemented.
`ResolvedDisplayConfig` is that one recipe, and the GPIO range/duplicate
checks in `board_validate.h` sit inside the 4848S040 branch, so a new id does
not inherit them until that block is extended. A new BoardProfile may set its
own `touch.i2c_address`; the GT911 backend reads that field. Custom Wiring
still cannot override it. A new profile does not provide raw-range
portability, 90/270 touch rotation, an LVGL layout for another resolution, or
device validation. Changing the locked 160-line / 6/10/6 MHz policy, or wiring
panel RESX, fails the current asserts.

## Русский

Один каталог представляет одну именованную плату. Профиль записывает заводские
факты; он не добавляет автоматически поддержку дисплея или тача.

### Контракт каталога

    profiles/<board>/
      README.md          факты об оборудовании и подтверждённые ограничения
      board_profile.h    заводские значения BoardProfile
      board_defaults.h   рекомендации и выбор UI, файл обязателен для board_select.h

### Добавление профиля

1. Добавьте уникальные макрос YORADIO_BOARD_<NAME> и значение BoardId в board_id.h.
2. Создайте board_profile.h и board_defaults.h. Механизм выбора требует оба файла.
3. Добавьте соответствующую ветку в board_select.h, публикующую kActiveBoard и
   kActiveBoardDefaults.
4. Расширьте board_validate.h проверками новой платы и используйте совместимое
   окружение PlatformIO.
5. Добавьте/проверьте PanelMode и драйверы DisplayPort/TouchPort, если отличаются
   контроллер или интерфейс, затем проверьте устройство. Те же ST7701 RGB565
   480×480 и GT911 используют текущие драйверы.

DisplayWiring содержит 16 линий RGB в фиксированном порядке esp_lcd B0..B4,
G0..G5, R0..R4. Неподключённые линии обозначаются kPinNotConnected (255),
а не GPIO0. BoardDefaults хранит рекомендации и выбор UI. Политика дисплея и
обслуживание тача используются; пины audio и wake не заменяют `I2S_*` и `WAKE_PIN`
в myoptions.h. Поддерживаемый профиль:
[ESP32-S3 4848S040](esp32s3_4848s040/README.md).
В настройках платы явно выберите совместимый `ui.profile`; см.
[UI-контракт](../../../lvgl_ui/README.md#ui-contract--ui-контракт).

### Учебный пример: новая ревизия с теми же ST7701 и GT911

Предположим, ревизия сохраняет ESP32-S3, ST7701 RGB565 480×480 и GT911, но
имеет другую разводку по схеме. Для разовой сборки, меняющей только доступные
пины `DISP_*`/`TOUCH_*`, оставьте `YORADIO_BOARD_4848S040` и используйте Custom
Wiring в `src/myoptions.h`. Отдельной заводской ревизии со своим именем,
настройками или фактами вне Custom Wiring нужен отдельный BoardProfile.

Это пример в документации, а не добавленный или проверенный на устройстве профиль.
Карта новых GPIO не предлагается: берите её из реальной схемы ревизии.

| Файл / шаг | Конкретное изменение для учебной ревизии |
|---|---|
| [board_id.h](../board_id.h) | Зарегистрируйте пока свободный макрос `YORADIO_BOARD_4848S040_REV_B` со значением 2 и элемент `BoardId::Esp32S3_4848S040_RevB` с этим значением; существующие ID не перенумеровывайте |
| `profiles/esp32s3_4848s040_rev_b/` | Скопируйте существующий каталог профиля; задайте обоим заголовкам отдельные защитные макросы включения и собственные имена макросов профиля; опишите реальную ревизию |
| `board_profile.h` в новом каталоге | Назовите объект `kBoardProfile_Esp32S3_4848S040_RevB`; обновите `identity.id`, `identity.name`, `identity.revision`, при необходимости `identity.build` и фактические поля разводки `display` / `touch` (`data_gpio`, пины синхронизации/команд, `i2c_sda_gpio`, `i2c_scl_gpio`, `int_gpio`, `reset_gpio`); оставьте только подтверждённые для ревизии факты |
| `board_defaults.h` в новом каталоге | Назовите объект `kBoardDefaults_Esp32S3_4848S040_RevB` и обновите ссылки на него в проверках; сохраните совместимую политику дисплея/тача и явно выберите `.ui = { .profile = UiProfile::Square480 }` |
| [board_select.h](../board_select.h) | Добавьте ветку `#elif YORADIO_BOARD == YORADIO_BOARD_4848S040_REV_B`, подключите два новых заголовка и свяжите `kActiveBoard` / `kActiveBoardDefaults` с новыми объектами; сохраните общий вызов `makeWiring(kActiveBoard)` |
| [board_validate.h](../board_validate.h) | Расширьте проверку согласованности платы и селектора дисплея; примените к новому ID проверки обязательных линий, исходного диапазона, зарезервированных GPIO и конфликтов; заводские проверки адаптируйте к новой схеме, не навязывая ей прежние GPIO 4848S040 |
| [compat/legacy_macros.h](../compat/legacy_macros.h) | Расширьте условие выбора платы, чтобы старые имена GPIO отвергались и для нового ID |
| `src/myoptions.h` и сборка | Выберите `YORADIO_BOARD_4848S040_REV_B`; для новой заводской разводки оставьте Custom Wiring 0. Повторно используйте `[env:4848S040]`, только если совпадают SoC/flash/PSRAM/таблица разделов; иначе добавьте подходящее окружение |

Порядок выбора остаётся `options.h → myoptions.h → board_select.h`. Текущие
PanelMode и драйверы подходят для той же комбинации панели и контроллера;
`ResolvedDisplayConfig` проверяет совместимость, `ResolvedUiMetrics` выбирает
Square480. Это ещё не подтверждает разводку, работу GT911 и панели на новой
плате: нужны проверки компиляции, сборка прошивки и проверка устройства.
Square720/Wide не реализованы.

### Ограничения

Реализована только комбинация ST7701 RGB565 480x480 и GT911.
`ResolvedDisplayConfig` — этот один рецепт, а проверки диапазона GPIO и
дубликатов в `board_validate.h` находятся внутри ветки 4848S040, поэтому новый
идентификатор не наследует их, пока этот блок не расширен. Новый BoardProfile
может задать свой `touch.i2c_address`; драйвер GT911 читает это поле. Custom Wiring
по-прежнему не может его переопределить. Новый профиль не обеспечивает перенос
исходного диапазона координат, поворот тача на 90/270 градусов, компоновку LVGL
под другое разрешение или подтверждение работы на устройстве. Изменение
зафиксированной политики 160 строк / 6/10/6 МГц
или подключение RESX панели останавливает сборку текущими `static_assert`.
