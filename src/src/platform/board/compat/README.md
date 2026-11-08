# Compatibility migration guards

## English

compat/legacy_macros.h is a deliberate compile-time migration boundary. It is
included by src/src/core/options.h after board selection. Production display and
touch code reads typed resolved configuration; this directory does not provide
runtime aliases or a fallback configuration.

### Rejected legacy GPIO names

- Display: ST7701_B0 through ST7701_B4, ST7701_G0 through ST7701_G5,
  ST7701_R0 through ST7701_R4, ST7701_HSYNC, ST7701_VSYNC, ST7701_DE,
  ST7701_PCLK, ST7701_CS, ST7701_SCK, ST7701_SDA and ST7701_BL.
- Touch: TS_SDA, TS_SCL, TS_INT and TS_RST.

If any of these is defined in myoptions.h, compilation stops with a directed
message to use DISP_* or TOUCH_* under YORADIO_CUSTOM_WIRING. The guards prevent
an old setting from being silently ignored; they are not aliases.

ST7701_BOOT_ORIENTATION_180 is not a legacy GPIO name. It remains an allowed
display policy choice. TS_MODEL remains the controller selector, but does not
own GT911 wiring.

Use [Board Contract](../README.md) for Custom Wiring and
[Touch Contract](../../touch/README.md) for the active GT911 path. New code
must not recreate another compatibility layer.

## Русский

compat/legacy_macros.h — защита миграции, проверяемая при компиляции.
src/src/core/options.h подключает его после выбора платы. Рабочий код дисплея
и тача читает итоговую типизированную конфигурацию; этот каталог не подставляет
псевдонимы старых имён или запасную конфигурацию.

### Отклоняемые старые имена GPIO

- Дисплей: ST7701_B0 до ST7701_B4, ST7701_G0 до ST7701_G5,
  ST7701_R0 до ST7701_R4, ST7701_HSYNC, ST7701_VSYNC, ST7701_DE,
  ST7701_PCLK, ST7701_CS, ST7701_SCK, ST7701_SDA, ST7701_BL.
- Тач: TS_SDA, TS_SCL, TS_INT, TS_RST.

Если любое из этих имён определено в myoptions.h, компиляция останавливается
с подсказкой использовать DISP_* или TOUCH_* при YORADIO_CUSTOM_WIRING.
Эта защита не даёт незаметно проигнорировать старую настройку;
она не превращает старые имена в псевдонимы новых.

ST7701_BOOT_ORIENTATION_180 не является старым именем GPIO. Это допустимая
настройка дисплея. TS_MODEL остаётся селектором контроллера, но не задаёт
разводку GT911.

Для Custom Wiring используйте [Board Contract](../README.md), для активного
пути GT911 — [Touch Contract](../../touch/README.md). Новый код не должен
создавать ещё один слой совместимости.
