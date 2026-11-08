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
| Gt911ReportIo | Observes the vendor driver's existing synchronous status/acknowledgement transactions | Extra I2C transactions or ownership of the underlying transport |
| TouchPort | LVGL-neutral Valid, Released, NoData, Error, Unavailable sample contract | LVGL objects or screensaver logic |
| TouchScreen | Production TouchPort owner, latest Flip request and safe application on DspTask | Persistence writes or a second transform |
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

WebUI persists Flip Touch and publishes its explicit value through an atomic
latest-request slot. DspTask initializes the backend from the boot snapshot and
applies pending orientation before LVGL timer polling. A held contact or fault
uncertainty defers the change until a confirmed Released; NoData cannot unlock it.
The complete vendor flag update finishes before the next coordinate read.

| TouchSampleStatus | Meaning |
|---|---|
| `Valid` | Fresh, trustworthy pressed point in logical display coordinates |
| `Released` | Fresh ready zero-touch report, successfully consumed by the driver |
| `NoData` | No fresh touch report; physical release remains unconfirmed |
| `Error` | Failed or untrustworthy read; cached coordinates cannot establish a new press |
| `Unavailable` | Backend is not ready; physical release remains unconfirmed |

Released confirms physical release according to the controller report: the backend
observed ready with zero contacts and a successful write of zero to status register
`0x814E` in the official driver's own transactions. This report acknowledgement
is distinct from an I2C bus-level ACK. `Gt911ReportIo` forwards those transactions
and records their outcome; it borrows the transport owned by Gt911Backend and
adds no I2C reads or writes. NoData means no fresh touch report (including
key-only reports), so it preserves the last trustworthy pointer state and cannot
clear fault rearm or wake suppression. Error or Unavailable releases LVGL and
requires a subsequent Released before a new press is accepted; this prevents a
held/cached point from becoming a false post-fault press. For a point-less LVGL RELEASED
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

Рабочий путь тача 4848S040 намеренно короткий:

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
| board_profile.h | Заводскими данными GT911: контроллер, GPIO и адрес I2C | Геометрией дисплея, жестами, калибровкой |
| kWiring.touch | Итоговыми GPIO после Custom Wiring | Второй таблицей контроллера |
| BoardDefaults.touch | Рекомендацией опроса и частотой I2C 100 кГц | Физическими пинами и адресом |
| ResolvedTouchConfig | Разводкой, геометрией из контракта дисплея и ориентацией Flip | Дескрипторами драйвера и политикой UI |
| Gt911Backend | Дескрипторами официального драйвера GT911 и panel IO, чтением отчёта, ограничением диапазона координат | Контроллером I2C, сглаживанием, подавлением дребезга, жестами |
| Gt911ReportIo | Наблюдением за существующими синхронными транзакциями чтения и подтверждения отчёта | Дополнительными транзакциями I2C или временем жизни транспорта |
| TouchPort | Независимым от LVGL контрактом отчётов Valid, Released, NoData, Error, Unavailable | Объектами LVGL и логикой заставки |
| TouchScreen | Реализацией TouchPort, последним запросом Flip и его безопасным применением в DspTask | Записью настроек или вторым преобразованием координат |
| Адаптер LVGL | Допуском новых нажатий после ошибки, передачей отпускания и пробуждением без нажатия | Регистрами GT911 и разводкой |

Wire.begin() инициализирует существующую шину I2C Arduino. Gt911Backend
получает её типизированный дескриптор и владеет только дескрипторами panel IO
и тача; end() не вызывает Wire.end(). Ошибка I2C возвращает Error; после
неудачной транзакции сохранённая точка не читается из кеша.

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
могут быть 255 (kPinNotConnected). Поддерживаемый путь — периодический опрос:
`Gt911Backend` принимает только `TouchServiceMode::Polling`, поэтому реальный
`TOUCH_INT` не включает режим прерывания. TOUCH_SDA и TOUCH_SCL обязательны.
Валидатор проверяет итоговые GPIO при сборке. Не определяйте TS_SDA,
TS_SCL, TS_INT, TS_RST: защита от старых имён останавливает сборку. Адрес I2C
(0x5D на 4848S040) — поле BoardProfile, Custom Wiring его не переопределяет. Serial
`bootInfo` печатает `wiring: custom`, когда флаг включён. SDA/SCL он не
печатает.

### Поведение во время работы

ResolvedTouchConfig использует логическую геометрию 480x480 из контракта дисплея.
Flip Touch — сохраняемая пользовательская настройка: OFF — исходная ориентация;
ON использует штатные флаги mirror_x и mirror_y драйвера GT911. Дополнительного
программного поворота на 180 градусов нет. Перед штатным mirror/swap драйвер
YoRadio только ограничивает диапазон координат.

WebUI сохраняет Flip Touch и публикует явное значение через атомарную переменную
последнего запроса. DspTask инициализирует драйвер из начального значения при
запуске и применяет запросы между опросами, до обработки таймеров LVGL.
Удержание пальца или неопределённость после ошибки откладывают смену до
подтверждённого Released; NoData не снимает блокировку. Все флаги ориентации
обновляются до следующего чтения координат.

| TouchSampleStatus | Значение |
|---|---|
| `Valid` | Свежая достоверная точка нажатия в логических координатах экрана |
| `Released` | Свежий готовый отчёт с нулём касаний, успешно обработанный драйвером |
| `NoData` | Свежего отчёта о касании нет; физическое отпускание не подтверждено |
| `Error` | Ошибка или недостоверное чтение; точка из кеша не подтверждает новое нажатие |
| `Unavailable` | Драйвер не готов; физическое отпускание не подтверждено |

Released подтверждает физическое отпускание по отчёту контроллера: получен
свежий отчёт с ready и нулём касаний, а драйвер успешно записал ноль в регистр
статуса `0x814E`. Это подтверждение обработки отчёта, а не бит ACK шины I2C.
`Gt911ReportIo` передаёт существующие транзакции и запоминает их результат;
он заимствует транспорт у Gt911Backend и не добавляет чтений или записей I2C.
NoData означает отсутствие свежего отчёта о касании (включая отчёты только
о кнопках). Он сохраняет последнее достоверное состояние указателя, но не
снимает блокировку после ошибки или подавление нажатия при пробуждении.
Error или Unavailable передаёт отпускание в LVGL; новое нажатие допускается
только после Released. Так удерживаемая или сохранённая точка не становится
ложным нажатием после ошибки. При передаче RELEASED без точки адаптер сохраняет
последнюю точку, предварительно установленную LVGL, и не подставляет (0,0).

Первое касание при заставке или погашенном экране будит устройство, но не
передаётся виджету как нажатие. Следующее касание обрабатывается обычно.
Debug Touch в WebUI управляется config.store.dbgtouch; OFF отключает подробную
трассировку, сообщения MOVE ограничены примерно 10 сообщениями в секунду.

### Добавление другого контроллера

Добавьте небольшой драйвер с семантикой TouchPort и подключите его в TouchScreen
при выборе контроллера, сохранив адаптер LVGL и UI. Он должен отдавать логические
координаты экрана, отличать аппаратную ошибку от отпускания и допускать новое
нажатие после ошибки только после подтверждённого отпускания. Требуется
проверка на устройстве. Это изменение реализации, а не только замена профиля.

### Текущие ограничения

- Исходный диапазон координат контроллера пока связан с логическими размерами панели.
- Поворот на 90/270 градусов требует явного расширения контракта.
- Реализован только GT911.
- Custom Wiring не переопределяет адрес I2C. Новый BoardProfile может задать
  `touch.i2c_address`; драйвер GT911 читает это поле.
- Назначение `TOUCH_INT` не выключает периодический опрос.
- Изменение GPIO/геометрии и новый контроллер требуют проверки на устройстве;
  проверки на компьютере не подтверждают работоспособность новой платы.
