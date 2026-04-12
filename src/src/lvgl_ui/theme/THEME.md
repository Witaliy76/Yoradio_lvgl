# YoRadio LVGL theme — карта для разработчика

Каноническая семантика и словарь токенов: `**docs/YoRadio_LVGL_Theme_Bible.txt**` (v1.2).  
Здесь — **где в коде** что лежит и **куда лезть**, чтобы поменять цвет/пресет, не путаться с legacy.

---

## 1. Pipeline в двух фразах

1. `**lv_theme_yoradio.cpp`** — таблицы RGB для пресетов **Dark / Light / Custom** и вызов `**lv_theme_default_init`** (базовый вид стандартных виджетов LVGL: accent, тёмная тема, шрифт по умолчанию).
2. Экраны и виджеты берут цвета через `**yoradio_palette()**` → поля `**YoRadioPalette**` (семантические имена: `station_name_text`, `volume_bar_fill`, …).

**Не источник правды для LVGL:** `config.theme`, `mytheme.h`, макросы `COLOR_*` в legacy — они для Canvas path.

---

## 2. Файлы в `theme/` и рядом


| Файл                       | Роль                                                                                                                                                                 |
| -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `**lv_theme_yoradio.h`**   | Структура `**YoRadioPalette**` (все поля-токены), enum `**ThemePreset**`, объявления API: `yoradio_palette()`, `yoradio_theme_init()`, `yoradio_theme_set_preset()`. |
| `**lv_theme_yoradio.cpp**` | Значения `**lv_color_hex(...)**` для **Dark** (`kPaletteDark`), **Light** (`kPaletteLight`), **Custom** (сейчас = копия Dark). Связка с `**lv_theme_default_init`**. |
| `**THEME.md**`             | Этот справочник (не влияет на сборку).                                                                                                                               |


Инициализация темы **не** в `theme/`, а в:


| Файл                 | Что сделать                                                                                                                                       |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `**../lvgl_ui.cpp`** | После успешного `**lv_disp_drv_register**` вызывается `**yoradio_theme_init(s_disp)**` — единственная точка входа; не дублировать из других мест. |


Шрифты **не** задаются в `lv_theme_yoradio.*` для каждого токена — см. раздел 5.

---

## 3. Где менять цвета (пресеты)

**Файл:** `lv_theme_yoradio.cpp`

- `**kPaletteDark`** — массив инициализации в **том же порядке**, что и поля в `**YoRadioPalette`** в `.h` (комментарии в конце строк указывают поле).
- `**kPaletteLight**` — то же количество полей, позиционно (без подписей в коде — ориентир: порядок как у Dark).
- `**kPaletteCustom**` — сейчас `**= kPaletteDark**`; зарезервировано под будущее хранилище.

Чтобы сменить, например, цвет названия станции на Main:

1. Найти в `**kPaletteDark**` строку с комментарием `**// station_name_text**` (или по порядку полей в `lv_theme_yoradio.h`).
2. Поменять `**lv_color_hex(0x......)**`.
3. Для Light — править соответствующую позицию в `**kPaletteLight**`.

Переключение активного пресета: `**yoradio_theme_set_preset(ThemePreset::Light)**` (сейчас UI для этого может отсутствовать; после смены базовая тема LVGL **не** пересоздаётся автоматически — см. ограничения в коде `yoradio_theme_init`).

---

## 4. Соответствие полей `YoRadioPalette` → смысл (шпаргалка)

Секции **§4.1–§4.6** в Bible; в коде — одна структура.


| Группа        | Поля (примеры)                                                                                                                                                                                                                | Типичное использование в проекте                                                                                                                  |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| Foundation    | `device_background`, `text_primary`, `accent`, …                                                                                                                                                                              | Фон экрана, базовый текст, акцент                                                                                                                 |
| Main / player | `status_line_text`, `status_line_meta`, `**status_weather_icon`**, `**status_weather_temp**`, `clock_text`, `station_name_text`, `track_text`, `volume_bar_track`, `volume_bar_fill`, `bottom_weather_text`, `bottom_ai_text` | Main, status line widget; компактная погода в **верхней** полосе — `**status_weather_*`** (не путать с `**bottom_weather_text**` для нижней зоны) |
| Lists         | `list_row_*`                                                                                                                                                                                                                  | Будущие списки (Station, Settings)                                                                                                                |
| Overlay       | `overlay_scrim`, `overlay_title_text`, …                                                                                                                                                                                      | LOST / UPDATING                                                                                                                                   |
| Screensaver   | `screensaver_background`, `screensaver_clock_text`                                                                                                                                                                            | `lv_screensaver.cpp`                                                                                                                              |
| Boot          | `boot_background`, `boot_status_text`, `boot_progress_*`                                                                                                                                                                      | Минимально в `scr_boot`; декор shuttle/track — **локальные** цвета в `scr_boot.cpp`                                                               |


Какой файл какой токен читает — см. раздел 7.

**Status-line weather (glance):** `status_weather_icon` и `status_weather_temp` задают цвет глифа и строки температуры в верхней полосе (`wgt_status_line`). Значения по пресетам в `**lv_theme_yoradio.cpp`**:


| Preset | `status_weather_icon`       | `status_weather_temp` |
| ------ | --------------------------- | --------------------- |
| Dark   | `#AEB4BC`                   | `#C4CAD2`             |
| Light  | `#8F98A3`                   | `#4E5A66`             |
| Custom | как Dark (= `kPaletteDark`) | как Dark              |


---

## 5. Шрифты: где настраиваются (не в `theme/`)


| Тема                           | За что отвечает                                                                                                                                                                                           |
| ------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `**lv_conf.h**`                | `**LV_FONT_DEFAULT**` — дефолтный шрифт для `**lv_theme_default_init(..., LV_FONT_DEFAULT)**` в `yoradio_theme_init`.                                                                                     |
| `**profiles/lv_profile_*.h**`  | `**LvglDisplayProfile**`: `font_small`, `font_normal`, `font_large`, `font_clock`, `font_header`, размеры экрана, `frame_padding`. Подключение через `**lv_profile_select.h**` → `**LV_ACTIVE_PROFILE**`. |
| `**fonts/**`, `**lv_fonts.h**` | Сгенерированные Montserrat / иконки (например status line). Конкретный виджет может взять фиксированный шрифт из `lv_fonts.h` (см. `wgt_status_line.cpp`).                                                |


**Итог:** поменять «шрифт всего LVGL по умолчанию» → `lv_conf.h`. Поменять шрифты по зонам для плат → профиль платы. Поменять шрифт одной новой полосы → код виджета / экрана.

---

## 6. API из `lv_theme_yoradio.h`

```text
yoradio_palette()           → const YoRadioPalette&   // активная палитра
yoradio_theme_active_preset() → ThemePreset
yoradio_theme_set_preset(p)   // смена пресета (без автопересоздания lv_theme)
yoradio_theme_init(disp)     // один раз после регистрации дисплея
```

В экранах паттерн: `**const YoRadioPalette& pal = yoradio_palette();**` затем `**pal.station_name_text**` и т.д.

---

## 7. Кто к каким токенам привязан (по репозиторию)

Проверяйте актуальность поиском по `yoradio_palette` / `pal.` в `lvgl_ui`.


| Область             | Файл                          | Токены (типично)                                                                                                                                                          |
| ------------------- | ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Main player         | `screens/scr_main.cpp`        | `device_background`, `status_line_text`, `station_name_text`, `track_text`, `volume_bar_*`, `text_*`                                                                      |
| Status line widget  | `widgets/wgt_status_line.cpp` | `status_line_text`, `clock_text`; `**status_weather_icon**`, `**status_weather_temp**` — цвета компактной погоды в верхней полосе (отдельно от `**bottom_weather_text**`) |
| Info                | `screens/scr_info.cpp`        | `device_background`, `text_primary`, `text_secondary`                                                                                                                     |
| Stub                | `screens/scr_stub.cpp`        | `device_background`, `text_primary`                                                                                                                                       |
| Overlays            | `lv_overlay.cpp`              | `overlay_scrim`, `overlay_title_text`                                                                                                                                     |
| Screensaver         | `lv_screensaver.cpp`          | `screensaver_background`, `screensaver_clock_text`                                                                                                                        |
| Boot (минимум темы) | `screens/scr_boot.cpp`        | `boot_background`, `boot_status_text`                                                                                                                                     |


**Исключение:** декоративный chrome Boot (shuttle / glow / track) — **жёстко заданные** `lv_color_make` внутри `scr_boot.cpp` в функциях `**apply_track_style` / `apply_shuttle_style` / `apply_glow_style`** — это **не** токены палитры (политика Bible / Stage 6.6C).

---

## 8. Что трогать нельзя без согласования

- Точку вызова `**yoradio_theme_init`** (только после `**lv_disp_drv_register**` в `initDisplayDriver`).
- Зависимости **LVGL / PlatformIO** ради темы — по правилам проекта отдельным решением.
- Перенос legacy палитры в LVGL как SoT.

---

## 9. Быстрый ответ «куда нажать»


| Задача                                           | Куда                                                                 |
| ------------------------------------------------ | -------------------------------------------------------------------- |
| Поменять цвета Dark/Light для всего UI по смыслу | `lv_theme_yoradio.cpp` → `kPaletteDark` / `kPaletteLight`            |
| Поменять только отображение на одном экране      | соответствующий `scr_*.cpp` / виджет — какой член `pal` используется |
| Поменять accent стандартных кнопок LVGL          | `accent`, `accent_soft` в палитре + `yoradio_theme_init`             |
| Поменять дефолтный шрифт LVGL                    | `lv_conf.h` → `LV_FONT_DEFAULT`                                      |
| Поменять шрифты по платам                        | `profiles/lv_profile_*.h` + `lv_profile_select.h`                    |
| Уточнить имя токена для новой зоны               | `docs/YoRadio_LVGL_Theme_Bible.txt` §4, §5.2                         |


---

*Документ описывает состояние после Stage 6.6 (LVGL-native theme). При добавлении экранов или токенов обновляйте разделы 4 и 7.*