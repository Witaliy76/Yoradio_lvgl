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
  records the tested 0; the value passed to the panel is
  `YORADIO_RGB_BOUNCE10`.
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

Display Contract разделяет факты панели, effective board wiring, проверенную
host policy и границу backend. Поддерживаемый production-путь:

    BoardProfile + Custom Wiring -> kWiring.display
    PanelMode (рецепт ST7701 RGB565 480x480)
    BoardDefaults.display + поддерживаемые user choices
      -> ResolvedDisplayConfig
      -> DisplayPort
      -> прямой esp_lcd ST7701 backend
      -> LVGL flush

### Ответственность

| Компонент | Ответственность |
|---|---|
| panel_mode.h / panel_st7701_rgb565_480.h | Внутренние факты панели: controller, geometry 480x480, RGB565/16-bit interface, timing, идентичность Type9 recipe, quirks |
| board_profile.h | Заводские display GPIO и совместимые controller/transport facts |
| kWiring.display | Effective GPIO после Custom Wiring |
| board_defaults.h | Проверенная host recommendation: partial PSRAM draw buffer 160 строк, один panel-driver PSRAM framebuffer, bounce 0, PCLK 6/10/6 МГц |
| ResolvedDisplayConfig | Compile-time composition, compatibility assertions и поддерживаемые build choices |
| DisplayPort | Backend-neutral seam begin, geometry, flush, brightness, sleep/wake и RGB-PCLK |
| esp_lcd_st7701_backend.cpp | Активный backend: Type9 byte stream, esp_lcd RGB panel, physical framebuffer, scanout |

Байты команд Type9 остаются в translation unit ST7701 backend; PanelMode хранит
только их проверенную identity/counts. DisplayPort не раскрывает raw panel
handle. Активный backend владеет RGB peripheral и одним physical PSRAM
framebuffer. LVGL отправляет flush rectangles через DisplayPort и не
программирует RGB GPIO или timing напрямую.

### PCLK и запись flash

Проверенная policy запускает scanout на 6 МГц, один раз поднимает PCLK до
10 МГц на Boot-to-Main и опускает его до 6 МГц во время YoRadio-controlled
physical flash writes. rgb_flash_guard координирует это через DisplayPort на
DspTask и VSYNC. Автоматический RGB restart не является recovery policy;
доступный restart только ручной diagnostic/service.

### Настройка и реализация

- **Изменение GPIO существующей платы:** используйте Custom Wiring DISP_* в
  src/myoptions.h; см. [board README](../board/README.md). Это меняет effective
  wiring, но не timing панели, геометрию 480×480 и не поддержку controller.
  RESX не является строкой DISP_*.
- **Поддерживаемые build choices:** YORADIO_RGB_BOUNCE10,
  YORADIO_RGB_FLASH_GUARD, ST7701_BOOT_ORIENTATION_180 — resolved build
  choices. Они не заменяют validation новой панели. `bounce_buffer_px` в
  BoardDefaults записывает проверенный 0; в панель передаётся
  `YORADIO_RGB_BOUNCE10`.
- **Другое resolution/panel:** создайте проверенный PanelMode, обеспечьте
  совпадающую BoardProfile wiring и добавьте/квалифицируйте backend за
  DisplayPort. Второго panel backend в дереве нет. Раскладка LVGL под другое
  разрешение ещё не собрана.

### Текущие ограничения и evidence

Реализован только ST7701 RGB565 parallel-16 480x480. Заводской reset панели не
подключён к SoC, и backend требует, чтобы `panel_reset_gpio` оставался
неподключённым. Политика 6/10/6 МГц — evidence 4848S040. Эти частоты и
partial-буфер в 160 строк читают backend и LVGL, а `static_assert` фиксирует
их на проверенных числах. `render_mode`, `draw_buffer_count`,
`draw_buffer_memory`, `flush` и `promote_pclk_once_on_boot_to_main`
проверяются так же; рабочий путь — backend ST7701 и partial-инициализация
LVGL. После изменения wiring, timing, resolution, framebuffer topology или
controller требуется hardware validation.
