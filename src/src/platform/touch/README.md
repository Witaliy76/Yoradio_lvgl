# Touch Contract

## English

The production touch path for 4848S040 is deliberately short:

    BoardProfile + Custom Wiring
      -> kWiring.touch + BoardDefaults.touch
      -> ResolvedTouchConfig
      -> Gt911Backend (esp_lcd_touch_gt911 1.2.1)
      -> TouchPort
      -> LvglTouchAdapter / lv_touch_indev.cpp
      -> LVGL 9.5

### Ownership

| Component | Owns / does | Does not own |
|---|---|---|
| board_profile.h | Factory GT911 controller, GPIO and I2C address | Display geometry, gestures, calibration |
| kWiring.touch | Effective GPIO after Custom Wiring | A second controller table |
| BoardDefaults.touch | Polling recommendation and 100 kHz I2C policy | Physical pins or address |
| ResolvedTouchConfig | Wiring, display-derived logical geometry, Flip orientation | Vendor handles or UI policy |
| Gt911Backend | Official panel-I/O and GT911 handles, read transaction, range-safety clamp | I2C controller, smoothing, debounce, gestures |
| TouchPort | LVGL-neutral Valid, NoData, Error, Unavailable sample contract | LVGL objects or screensaver logic |
| TouchScreen | Production TouchPort owner and persisted Flip update | A second transform |
| LVGL adapter | Fault rearm, release feed and screensaver wake-only policy | GT911 registers or wiring |

Wire.begin() initializes the existing Arduino I2C bus. Gt911Backend obtains its
typed bus handle and owns only its panel-I/O and touch handles; end() does not
call Wire.end(). I2C failures return Error and never consume a cached point
after a failed transaction.

### Configure GT911 GPIO

In src/myoptions.h, use only Custom Wiring:

    #define YORADIO_CUSTOM_WIRING 1
    #define TOUCH_SDA 19   // replace only when physically rewired
    #define TOUCH_SCL 45
    // #define TOUCH_INT 255
    // #define TOUCH_RST 255

Uncomment only the changed lines. The numbers 19 and 45 above are the factory
4848S040 pins, shown so an unchanged line can stay commented. Replace a number
only after you have checked that GPIO on your own schematic. TOUCH_INT and
TOUCH_RST may be 255 (kPinNotConnected). The supported path polls:
`Gt911Backend` accepts only `TouchServiceMode::Polling`, so a real `TOUCH_INT`
does not select interrupt mode. TOUCH_SDA and TOUCH_SCL are required. The
compile-time validator checks the effective GPIO set. Do not define TS_SDA,
TS_SCL, TS_INT, or TS_RST: compatibility guards stop the build. The I2C
address (0x5D for 4848S040) is a BoardProfile field with no Custom Wiring
override. Serial `bootInfo` prints `wiring: custom` when the flag is on. It
does not print SDA/SCL.

### Runtime behavior

ResolvedTouchConfig uses the display contract's 480x480 logical geometry.
Flip Touch is persisted user state: OFF is native orientation; ON uses official
GT911 mirror_x and mirror_y (no software 180-degree replica). The backend keeps
only a range clamp before official mirror/swap processing.

NoData proves physical release. Error or Unavailable releases LVGL and requires
a subsequent NoData before a new press is accepted; this prevents a held/cached
point from becoming a false post-fault press. For a point-less LVGL RELEASED
frame, the adapter leaves LVGL's preloaded last point intact; it never injects
(0,0).

The screensaver/blank first touch wakes the screen but is not forwarded as a
widget press. The next stroke is normal. WebUI Debug Touch is permanent and is
controlled by config.store.dbgtouch; OFF emits no detailed trace, and MOVE
logging is rate-limited to about 10 messages per second.

### Add another controller

Add a small backend that implements TouchPort semantics, then connect it in
TouchScreen/controller selection without changing the LVGL adapter or UI. It
must supply logical display-space coordinates, distinguish hardware errors from
a release, preserve release-before-rearm behavior, and have device tests. This
is implementation work, not a profile-only change.

### Current limits

- Controller raw range is still coupled to panel logical dimensions.
- Rotation 90/270 needs an explicit contract extension.
- Only GT911 is implemented.
- Custom Wiring does not override the I2C address. A new BoardProfile can set
  `touch.i2c_address`; the GT911 backend reads that field.
- Assigning `TOUCH_INT` does not leave polling mode.
- GPIO/geometry changes and a new controller require hardware validation; host
  checks do not prove a new board.

## Русский

Production-путь тача 4848S040 намеренно короткий:

    BoardProfile + Custom Wiring
      -> kWiring.touch + BoardDefaults.touch
      -> ResolvedTouchConfig
      -> Gt911Backend (esp_lcd_touch_gt911 1.2.1)
      -> TouchPort
      -> LvglTouchAdapter / lv_touch_indev.cpp
      -> LVGL 9.5

### Владение

| Компонент | Чем владеет / что делает | Чем не владеет |
|---|---|---|
| board_profile.h | Заводской controller GT911, GPIO и адрес I2C | Геометрией display, жестами, calibration |
| kWiring.touch | Effective GPIO после Custom Wiring | Второй таблицей контроллера |
| BoardDefaults.touch | Рекомендацией polling и политикой I2C 100 кГц | Физическими пинами и адресом |
| ResolvedTouchConfig | Разводкой, display-derived logical geometry, Flip orientation | Vendor handles и UI policy |
| Gt911Backend | Official panel-I/O/GT911 handles, read transaction, range-safety clamp | I2C controller, smoothing, debounce, gestures |
| TouchPort | LVGL-neutral контракт samples Valid, NoData, Error, Unavailable | LVGL objects и screensaver logic |
| TouchScreen | Production-владельцем TouchPort и persisted Flip update | Вторым transform |
| LVGL adapter | Fault rearm, release feed, screensaver wake-only policy | GT911 registers и wiring |

Wire.begin() инициализирует существующую Arduino I2C шину. Gt911Backend
получает её typed handle и владеет только panel-I/O/touch handles; end() не
вызывает Wire.end(). Ошибка I2C возвращает Error и после неудачной транзакции
не читает cached point.

### Настройка GPIO GT911

В src/myoptions.h используйте только Custom Wiring:

    #define YORADIO_CUSTOM_WIRING 1
    #define TOUCH_SDA 19   // меняйте только после физической перекоммутации
    #define TOUCH_SCL 45
    // #define TOUCH_INT 255
    // #define TOUCH_RST 255

Раскомментируйте только изменённые строки. Числа 19 и 45 выше — заводские пины
4848S040: так неизменённая строка может остаться закомментированной. Меняйте
число только после проверки этого GPIO по своей схеме. TOUCH_INT и TOUCH_RST
могут быть 255 (kPinNotConnected). Поддерживаемый путь — polling:
`Gt911Backend` принимает только `TouchServiceMode::Polling`, поэтому реальный
`TOUCH_INT` не включает режим прерывания. TOUCH_SDA и TOUCH_SCL обязательны.
Compile-time validator проверяет effective GPIO. Не определяйте TS_SDA,
TS_SCL, TS_INT, TS_RST: compatibility guards останавливают сборку. Адрес I2C
(0x5D на 4848S040) — поле BoardProfile, у Custom Wiring нет override. Serial
`bootInfo` печатает `wiring: custom`, когда флаг включён. SDA/SCL он не
печатает.

### Поведение runtime

ResolvedTouchConfig использует logical geometry 480x480 из display contract.
Flip Touch — persisted user state: OFF — native orientation; ON использует
штатные GT911 mirror_x и mirror_y (без software replica 180 градусов). Backend
оставляет только range clamp до official mirror/swap processing.

NoData доказывает физический release. Error или Unavailable освобождает LVGL и
требует следующего NoData до принятия нового press; это не даёт held/cached
point стать ложным нажатием после fault. Для point-less LVGL frame RELEASED
adapter оставляет preloaded last point LVGL; (0,0) не подставляется.

Первое касание screensaver/blank будит экран, но не передаётся как press
виджету. Следующий stroke обычный. WebUI Debug Touch постоянен и управляется
config.store.dbgtouch; OFF не даёт подробного trace, MOVE ограничен примерно
10 сообщениями в секунду.

### Добавление другого controller

Добавьте небольшой backend с семантикой TouchPort, затем подключите его в
TouchScreen/controller selection без изменения LVGL adapter и UI. Он обязан
отдавать logical display-space coordinates, отличать hardware error от release,
сохранять release-before-rearm и иметь device tests. Это реализация, не простая
замена профиля.

### Текущие ограничения

- Raw range controller пока связан с logical dimensions панели.
- Rotation 90/270 требует явного расширения контракта.
- Реализован только GT911.
- Custom Wiring не переопределяет адрес I2C. Новый BoardProfile может задать
  `touch.i2c_address`; backend GT911 читает это поле.
- Назначение `TOUCH_INT` не выключает polling.
- Изменение GPIO/geometry и новый controller требуют hardware validation; host
  checks не доказывают новую плату.
