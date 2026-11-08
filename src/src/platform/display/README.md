# Display Contract

## English

The Display Contract separates panel facts, effective board wiring, tested host
policy, and the backend boundary. The supported production path is:

    BoardProfile + Custom Wiring -> kWiring.display
    PanelMode (ST7701 RGB565 480x480 recipe)
    BoardDefaults.display + supported user choices
      -> ResolvedDisplayConfig
      -> DisplayPort
      -> direct esp_lcd ST7701 backend
      -> LVGL flush

### Responsibilities

| Component | Responsibility |
|---|---|
| panel_mode.h / panel_st7701_rgb565_480.h | Panel-intrinsic controller, 480x480 geometry, RGB565/16-bit interface, timing, Type9 recipe identity and panel quirks |
| board_profile.h | Factory display GPIO and compatible controller/transport facts |
| kWiring.display | Effective GPIO after Custom Wiring |
| board_defaults.h | Tested host recommendation: partial 160-line PSRAM draw buffer, one panel-driver PSRAM framebuffer, bounce 0, PCLK 6/10/6 MHz |
| ResolvedDisplayConfig | Compile-time composition plus compatibility assertions and supported build choices |
| DisplayPort | Backend-neutral begin, geometry, flush, brightness, sleep/wake and RGB-PCLK seam |
| esp_lcd_st7701_backend.cpp | Active backend; Type9 byte stream, esp_lcd RGB panel, physical framebuffer and scanout |

The Type9 command bytes stay in the ST7701 backend translation unit; PanelMode
stores only their verified identity/counts. DisplayPort owns no raw panel handle.
The active backend owns the RGB peripheral and one physical PSRAM framebuffer.
LVGL submits flush rectangles through DisplayPort; it does not program RGB GPIO
or timings directly.

### PCLK and flash writes

The tested policy starts scanout at 6 MHz, performs one Boot-to-Main promotion
to 10 MHz, and lowers PCLK to 6 MHz during YoRadio-controlled physical flash
writes. rgb_flash_guard coordinates this through DisplayPort on DspTask and
VSYNC. Automatic RGB restart is not a recovery policy; the exposed restart is
manual diagnostic/service only.

### Configure versus implement

- **Existing-board GPIO changes:** use DISP_* Custom Wiring entries in
  src/myoptions.h; see [board README](../board/README.md). This changes
  effective wiring, not panel timing, 480×480 geometry, or controller support.
  Panel RESX is not a DISP_* line.
- **Supported build choices:** YORADIO_RGB_BOUNCE10, YORADIO_RGB_FLASH_GUARD,
  and ST7701_BOOT_ORIENTATION_180 are resolved build choices. They are not a
  substitute for a validated new panel. `bounce_buffer_px` in BoardDefaults
  records the tested 0. `YORADIO_RGB_BOUNCE10` is a flag, not a pixel count:
  `effective_bounce_buffer_px` is 0 when disabled or native width × 10 when
  enabled, so the current panel receives 480 × 10 = 4800 pixels.
- **Another resolution/panel:** create a verified PanelMode, ensure matching
  BoardProfile wiring and add/qualify a backend behind DisplayPort. No second
  panel backend is in the tree. LVGL layout for another resolution is not
  built yet.

### Current limits and evidence

Only ST7701 RGB565 parallel-16 at 480x480 is implemented. The factory panel
reset is not wired to the SoC, and the backend requires
`panel_reset_gpio` to stay disconnected. The 6/10/6 MHz policy is 4848S040
evidence. Those frequencies and the 160-line partial buffer are read by the
backend and LVGL, and `static_assert` locks them to the tested numbers.
`render_mode`, `draw_buffer_count`, `draw_buffer_memory`, `flush` and
`promote_pclk_once_on_boot_to_main` are checked the same way; the running
path is the ST7701 backend and the LVGL partial init. Hardware validation is
required after changing panel wiring, timing, resolution, framebuffer topology,
or controller.

## Русский

Display Contract разделяет параметры панели, итоговую разводку платы,
проверенную политику обслуживания дисплея и интерфейс драйвера. Рабочий путь:

    BoardProfile + Custom Wiring -> kWiring.display
    PanelMode (рецепт ST7701 RGB565 480x480)
    BoardDefaults.display + поддерживаемые пользовательские настройки
      -> ResolvedDisplayConfig
      -> DisplayPort
      -> прямой драйвер ST7701 на esp_lcd
      -> передача обновляемой области LVGL (flush)

### Ответственность

| Компонент | Ответственность |
|---|---|
| panel_mode.h / panel_st7701_rgb565_480.h | Параметры панели: контроллер, геометрия 480x480, 16-битный RGB565, тайминги, описание последовательности Type9 и особенности панели |
| board_profile.h | Заводские GPIO дисплея, тип контроллера и интерфейса |
| kWiring.display | Итоговые GPIO после Custom Wiring |
| board_defaults.h | Проверенная политика: частичный буфер отрисовки в PSRAM на 160 строк, один кадровый буфер драйвера панели в PSRAM, bounce 0, PCLK 6/10/6 МГц |
| ResolvedDisplayConfig | Объединение данных при сборке, проверки совместимости и поддерживаемые настройки |
| DisplayPort | Независимый от драйвера интерфейс: begin, geometry, flush, brightness, sleep/wake и управление RGB-PCLK |
| esp_lcd_st7701_backend.cpp | Активный драйвер: байты команд Type9, панель RGB esp_lcd, физический кадровый буфер и вывод изображения |

Байты команд Type9 остаются в единице трансляции драйвера ST7701; PanelMode хранит
только проверенное описание последовательности и число команд/байтов.
DisplayPort не раскрывает дескриптор панели. Активный драйвер владеет RGB-периферией
и одним физическим кадровым буфером в PSRAM. LVGL отправляет обновляемые области
через DisplayPort и не программирует GPIO RGB или тайминги напрямую.

### PCLK и запись flash

Проверенная политика запускает вывод на 6 МГц, один раз поднимает PCLK до
10 МГц при переходе Boot → Main и опускает его до 6 МГц во время физических
записей flash, контролируемых YoRadio. rgb_flash_guard координирует это через
DisplayPort в DspTask с учётом VSYNC. Автоматического перезапуска RGB для
восстановления нет; доступный перезапуск предназначен для ручной диагностики.

### Настройка и реализация

- **Изменение GPIO существующей платы:** используйте Custom Wiring DISP_* в
  src/myoptions.h; см. [Board Contract](../board/README.md). Это меняет итоговую
  разводку, но не тайминги панели, геометрию 480×480 и не поддержку контроллера.
  RESX не является строкой DISP_*.
- **Поддерживаемые настройки сборки:** YORADIO_RGB_BOUNCE10,
  YORADIO_RGB_FLASH_GUARD, ST7701_BOOT_ORIENTATION_180. Они не заменяют проверку
  новой панели. `bounce_buffer_px` в BoardDefaults хранит проверенный 0.
  `YORADIO_RGB_BOUNCE10` — флаг, а не число пикселей: `effective_bounce_buffer_px`
  равен 0 при выключенном флаге или ширине панели × 10 при включённом.
  Для текущей панели драйвер получает 480 × 10 = 4800 пикселей.
- **Другое разрешение или панель:** создайте проверенный PanelMode, согласуйте
  с ним разводку BoardProfile и подключите/проверьте драйвер за DisplayPort.
  Второго драйвера панели в дереве нет. Компоновка LVGL под другое разрешение
  ещё не реализована.

### Текущие ограничения и подтверждённые параметры

Реализован только ST7701 с 16-битной параллельной шиной RGB565 и геометрией 480x480.
Сброс панели не подключён к SoC, и драйвер требует, чтобы `panel_reset_gpio`
оставался неподключённым. Политика 6/10/6 МГц проверена на 4848S040. Эти частоты
и частичный буфер в 160 строк читают драйвер и LVGL, а `static_assert` фиксирует
их на проверенных числах. `render_mode`, `draw_buffer_count`,
`draw_buffer_memory`, `flush` и `promote_pclk_once_on_boot_to_main`
проверяются так же; рабочий путь — драйвер ST7701 и инициализация LVGL с частичным
буфером. После изменения разводки, таймингов, разрешения, организации кадровых
буферов или контроллера требуется проверка на устройстве.
