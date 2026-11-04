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

Один каталог представляет одну именованную плату. Profile записывает factory
facts; он не создаёт автоматически поддерживаемую display/touch implementation.

### Контракт каталога

    profiles/<board>/
      README.md          hardware facts и подтверждённые ограничения
      board_profile.h    factory initializer BoardProfile
      board_defaults.h   tested recommendations, обязательные для board_select.h

### Добавление profile

1. Добавьте уникальные macro YORADIO_BOARD_<NAME> и значение BoardId в board_id.h.
2. Создайте board_profile.h и board_defaults.h. Selector сейчас требует оба файла.
3. Добавьте соответствующую ветку в board_select.h, публикующую kActiveBoard и
   kActiveBoardDefaults.
4. Расширьте board_validate.h board-specific assertions и используйте matching
   PlatformIO environment.
5. Добавьте/квалифицируйте PanelMode/backend и TouchPort backend при отличии
   controller или transport, затем выполните hardware validation. Те же ST7701
   RGB565 480×480 и GT911 остаются на текущих backend.

DisplayWiring содержит 16 RGB slots в фиксированном порядке esp_lcd B0..B4,
G0..G5, R0..R4. Отсутствующие hardware lines используют kPinNotConnected (255),
никогда GPIO0. BoardDefaults хранит recommendations. Политика хоста дисплея и
обслуживание тача читаются; пины audio и wake не заменяют `I2S_*` и `WAKE_PIN`
в myoptions.h. Production profile:
[ESP32-S3 4848S040](esp32s3_4848s040/README.md).
В defaults платы явно выберите совместимый `ui.profile`; см.
[UI-контракт](../../../lvgl_ui/README.md#ui-contract--ui-контракт).

### Ограничения scope

Реализована только комбинация ST7701 RGB565 480x480 и GT911.
`ResolvedDisplayConfig` — этот один рецепт, а проверки диапазона GPIO и
дубликатов в `board_validate.h` находятся внутри ветки 4848S040, поэтому новый
id не наследует их, пока этот блок не расширен. Новый BoardProfile может задать
свой `touch.i2c_address`; backend GT911 читает это поле. Custom Wiring по-прежнему
не может его переопределить. Новый profile не даёт автоматически raw-range
portability, touch rotation 90/270, раскладку LVGL под другое разрешение или
device validation. Изменение зафиксированной политики 160 строк / 6/10/6 МГц
или подключение RESX панели останавливает сборку текущими `static_assert`.
