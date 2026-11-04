# lvgl_ui - LVGL UI subsystem for YoRadio

Author: Witaliy76 - https://github.com/Witaliy76

All LVGL-related product UI lives exclusively in `src/src/lvgl_ui/`.
Do not mix LVGL screen/widget code with legacy drivers in `src/src/displays/`.

This directory hosts the LVGL 9.5.0 UI for YoRadio: PageChain navigation, product
screens, shared widgets, UI profiles, theme, adapters, and assets.
All `lv_*` calls run only on **DspTask** (via `Display::loop` / `lvgl_ui` entry points).
Cross-task UI updates arrive through `display.putRequest()` and are applied on DspTask.

Primary bring-up board: **4848S040** (ST7701, 480x480, GT911).
The ST7701/LVGL path is mandatory for every supported profile and environment;
the obsolete `DUMMYDISPLAY` / headless fallback was removed in S05-3C-FIX1.

## PageChain topology

Horizontal carousel with six real pages (fixed slot order):

```
Info <-> Main <-> Visual <-> Station <-> Weather <-> Settings
```

| Index | Page | Role |
|------:|------|------|
| 0 | Info | Device / network / firmware / memory facts |
| 1 | Main | Now-playing: metadata, controls, volume, Presence Rail, optional bg/art |
| 2 | Visual | Beocord-style stereo PPM on real pre-Gain PCM + stream metadata |
| 3 | Station | Station list (default: simple paged renderer) |
| 4 | Weather | Current weather + forecast from shared `WeatherState` |
| 5 | Settings | Display, theme, Auto Dim, TIMERS (Radio Stop/Start + Deep Sleep/Wake), Music Rail, Wi-Fi service entry |

Special modes (not carousel slots):

- **Boot** - branded boot screen, then handoff to Main (or Wi-Fi recovery)
- **Wi-Fi flow / RebootRequired** - setup and recovery shell (AP `yoRadioAP` when needed)
- **Preset Temporary** - eight positional presets overlay (from Info/Main/Visual/Weather)
- **Screensaver** - analog clock on `lv_layer_top` (wake by touch)
- **Overlays** - LOST / UPDATING style layers on `lv_layer_top` when used

`screens/scr_stub.*` remains in-tree as retained Stage 5 compatibility code but is **not** registered in PageChain; all six carousel slots use real page classes.

## Directory structure

```
lvgl_ui/
├── lv_conf.h                 - LVGL 9.5.0 compile-time configuration
├── lvgl_ui.h / lvgl_ui.cpp   - Public entry points and lifecycle integration
├── lv_page_chain.*           - Carousel + Boot / Temporary / Wi-Fi special modes
├── lv_screen.h               - ILvglScreen contract
├── lv_overlay.*              - Top-layer modal overlays
├── lv_touch_indev.*          - Pointer indev (DspTask only)
├── lv_screensaver.*          - Analog clock screensaver
├── lv_fs_littlefs.*          - LittleFS bridge for LVGL images
├── lv_mem_pool_psram.*       - LVGL TLSF pool in PSRAM
├── profiles/                 - UI factory metrics and resolved viewport
├── screens/                  - Product screens + layout_tree notes
├── widgets/                  - status line, Presence Rail, footer pill
├── adapters/                 - preset store, station list boundary
├── theme/                    - Dark / Light / Custom + THEME.md
├── assets/                   - Boot / screensaver assets
└── fonts/                    - factory + Tabler TTF, emergency 16, see readme_fonts.md
```

## UI contract / UI-контракт

### English

The 4848S040 [`board_defaults.h`](../platform/board/profiles/esp32s3_4848s040/board_defaults.h)
sets `BoardDefaults.ui.profile` explicitly to `yoradio::UiProfile::Square480`.
[`resolved_ui_metrics.h`](profiles/resolved_ui_metrics.h) combines that choice
with `kResolvedDisplayConfig.panel.native_width/native_height` from the
[Display Contract](../platform/display/README.md). Its `kResolvedUiMetrics`
exposes the physical `viewport_width/viewport_height` and a `factory` reference
to UI metrics. The viewport is not a UI factory dimension, and resolution does
not automatically select a profile. Compatibility is checked at compile time.

[`ui_profile_square480.h`](profiles/ui_profile_square480.h) owns the compiled
`kUiFactoryMetricsSquare480` defaults. To change Main station-name text, edit
`main_station_font_px`; for the on-screen art slot edit
`main_station_art_slot_width`, `main_station_art_slot_height` and, if needed,
`main_station_art_gap`. These are presentation defaults: the existing Station
Art `.bin` upload/file contract remains 120x120 and is not automatically resized
or migrated. Main's 2 px frame leaves a 116x116 inner widget at current defaults.
Changing a compiled metric requires rebuilding and parity review; requested font
sizes do not by themselves determine font source or glyph metrics.
Production assertions reject an unsupported profile, incompatible viewport,
zero font sizes, padding that leaves no content, and an art slot too small for
Main's 2 px border. They do not lock the factory values to the S06 baseline.
The [S06 host proof](../../../test/s06_ui_profile_metrics/ui_profile_metrics.cpp)
intentionally asserts that baseline; after an intentional metric change, review
parity and update those test expectations separately.

The profile requests font sizes and shared geometry; the [Theme](theme/THEME.md)
owns colors and styling. [`FontProvider`](font_provider.h) loads/provides text
and icon fonts, including TinyTTF and the optional `/fonts/user.ttf`. Main JPEG
uses its own worker/cache and physical viewport; Station Art has its own loader
and file contract; the Beocord pack owns its native 480x480 assets and absolute
segment rectangles. Screen-local Flex composition and procedural geometry stay
in their screens/widgets; they are not automatically exported to a profile.
All LVGL operations and UI object lifetime remain DspTask-owned.

Only Square480 is implemented and build-verified here. Square720 and Wide do not
exist yet; neither a new panel resolution nor a board selection supplies their
layout, display/touch support or a Beocord transform. A future profile starts with
an identity in [`ui_profile_id.h`](../ui/ui_profile_id.h), factory data in
`profiles/`, and an explicit selection/compatibility path in the resolver and
board defaults, followed by backend, resource and device validation. The current
resolver and assertions support Square480 only. UiTuning and UI Editor are deferred.

For hardware work see [external I2S and Custom Wiring](../platform/README.md#four-common-tasks)
and [adding a board profile](../platform/board/profiles/README.md#add-a-profile).

### Русский

В [`board_defaults.h`](../platform/board/profiles/esp32s3_4848s040/board_defaults.h)
4848S040 явно задаёт `BoardDefaults.ui.profile = yoradio::UiProfile::Square480`.
[`resolved_ui_metrics.h`](profiles/resolved_ui_metrics.h) объединяет этот выбор
с `kResolvedDisplayConfig.panel.native_width/native_height` из
[Display Contract](../platform/display/README.md). `kResolvedUiMetrics` содержит
физические `viewport_width/viewport_height` и ссылку `factory` на UI-метрики.
Размер панели не является заводской UI-метрикой и сам не выбирает профиль;
совместимость проверяется при компиляции.

Заводские значения `kUiFactoryMetricsSquare480` находятся в
[`ui_profile_square480.h`](profiles/ui_profile_square480.h). Для размера названия
станции Main измените `main_station_font_px`; для экранного слота обложки —
`main_station_art_slot_width`, `main_station_art_slot_height` и при необходимости
`main_station_art_gap`. Это размеры представления: существующий контракт загрузки
и файла Station Art `.bin` остаётся 120x120 и автоматически не меняется. При
текущей рамке 2 px внутренний widget имеет размер 116x116. После изменения
скомпилированных значений нужны пересборка и проверка parity; запрошенный размер
шрифта сам по себе не определяет его источник и фактические метрики глифов.
Production assertions отвергают неподдерживаемый профиль, несовместимый viewport,
нулевые размеры шрифтов, padding без места для содержимого и art slot меньше,
чем требуется рамке Main 2 px. Они не фиксируют метрики на baseline S06.
[Host-proof S06](../../../test/s06_ui_profile_metrics/ui_profile_metrics.cpp)
намеренно проверяет этот baseline: при осознанном изменении метрики отдельно
проверьте parity и обновите ожидания теста.

Профиль задаёт запросы размеров шрифтов и общую геометрию; [Theme](theme/THEME.md)
отвечает за цвета и оформление. [`FontProvider`](font_provider.h) загружает и
предоставляет текстовые и icon-шрифты, включая TinyTTF и необязательный
`/fonts/user.ttf`. У Main JPEG собственные worker/cache и физический viewport;
у Station Art — собственный loader и формат файла; Beocord pack владеет
нативными assets 480x480 и абсолютными прямоугольниками сегментов. Flex-композиция
и процедурная геометрия остаются в экранах и widgets. Все LVGL-операции и время
жизни UI-объектов принадлежат DspTask.

Сейчас реализован и проверен сборкой только Square480. Square720 и Wide не
реализованы: другое разрешение панели или выбор платы не создают layout,
display/touch поддержку или преобразование Beocord. Для будущего профиля нужны
identity в [`ui_profile_id.h`](../ui/ui_profile_id.h), заводские данные в
`profiles/`, явный выбор и проверка совместимости в resolver и board defaults,
затем проверка backend, ресурсов и устройства. Текущий resolver и assertions
поддерживают только Square480. UiTuning и UI Editor отложены.

Для аппаратных задач см. [внешний I2S и Custom Wiring](../platform/README.md#четыре-частых-задачи)
и [добавление профиля платы](../platform/board/profiles/README.md#добавление-profile).

## Runtime ownership

| Concern | Owner |
|---------|--------|
| `lv_*` API | DspTask only |
| Display / draw buffer | LVGL 9 `lv_display_t`; RGB565; one 480x160 PSRAM buffer (153600 B) in `LV_DISPLAY_RENDER_MODE_PARTIAL` on 4848S040 |
| Flush | Synchronous LVGL -> DisplayPort -> direct `esp_lcd`/ST7701 path; CPU rectangular blit from the 480x160 draw buffer into one 480x480 PSRAM physical framebuffer; `lv_display_flush_ready()` exactly once on every callback path |
| Touch | Official `esp_lcd_touch_gt911` → `TouchPort` → typed LVGL adapter with safe fault rearm; its pointer read callback runs from `lv_timer_handler()` on DspTask |
| LVGL heap | Fixed 256 KiB built-in TLSF pool backed by one process-lifetime PSRAM allocation |
| LVGL task stack | DspTask, 12288 B |
| Theme preset / Custom file | Theme module + WebUI Appearance; coalesced live reapply on DspTask |
| Station art / Main backgrounds | Main: LittleFS JPEG decoded on the `MainBgJpeg` worker into an RGB565 PSRAM cache; cache install and LVGL apply on DspTask. Station art: LittleFS RGB565 `.bin` |
| Weather data | Core `WeatherState` (fetch off UI); Weather page is read-only consumer |
| Presets | `adapters/preset_store` on LittleFS |

`lvgl_ui::initRuntime()` initializes LVGL and registers the custom `lv_fs_drv_t`
LittleFS bridge as drive `L:`. `initDisplayDriver()` creates the display, installs the
buffer and flush callback, initializes the theme, and registers the pointer indev.
`taskHandler()` is the DspTask-owned LVGL pump.

PageChain loads the next carousel screen with LVGL 9 screen auto-delete semantics.
Before a previous tree is deleted, `prepareForAutoDelete()` stops page-owned resources
that could still touch LVGL objects; after deletion, `releaseAfterAutoDelete()` clears
handles and releases non-LVGL resources. A gesture callback installed on an active screen
root must not synchronously load another screen: `carousel_gesture_event_cb` records the
direction, and `taskHandler()` consumes it only after `lv_timer_handler()` returns and the
LVGL event-dispatch stack has unwound. Some child callbacks still navigate synchronously;
they are safe in the current object/event layout and remain a defensive hardening follow-up.

Runtime theme changes follow the same request-now / apply-later rule, for the same reason. A
Theme tap (or a WebUI `SET_THEME_PRESET` message) only records the requested preset;
`taskHandler()` then runs the heavy transaction - LVGL default-theme reinit, global style
refresh, page reapply and `theme.dat` persistence - at most once, for the newest request, on a
quiet iteration. Intermediate presets in a rapid burst are discarded, so the burst costs one
apply instead of one per tap. Settings reads the requested preset (`themeRequestedPreset()`) so
the row follows the user immediately, while `yoradio_theme_active_preset()` keeps meaning the
preset actually in effect for every other consumer.

Main backgrounds are factory/user JPEGs decoded by TJPGD, FIT/ScaleOnce into an RGB565
PSRAM cache. Both Boot preparation and runtime theme changes go through the same single
`MainBgJpeg` worker (core 0, priority 1) with its latest-wins generation counter and
user→factory fallback; DspTask never decodes inline. That matters on Boot: `lv_timer_handler()`
has one call site, so a synchronous decode would hold DspTask outside it and freeze the Boot
shuttle animation — with the worker, DspTask keeps servicing LVGL and the indicator stays
animated for the whole decode/scale. The indicator remains genuinely indeterminate; there is no
progress metric to report. Boot still hands off to Main only once the preparation attempt has
settled, and settlement means *attempted*, not *succeeded* — a decode failure with no usable
fallback lets Main continue exactly as before, showing the theme-colored screen.
`mainBgCachePollApply()` (via `mainBgPollRuntimeApply()`) is the sole consumer of a finished
worker result and the only place the cache is installed. Visual and screensaver RGB565 `.bin` files keep the existing YoRadio
4-byte little-endian disk header followed by RGB565 bytes. Loaders translate that header
to an in-memory LVGL 9 `lv_image_header_t` / `lv_image_dsc_t`; those families were not
converted to a new disk format. Static RGB565A8 screensaver sprites are different: they use the
LVGL 9 color-plane-then-alpha-plane representation. Normal text and icons are two
embedded TTF assets opened by TinyTTF (`fonts/readme_fonts.md`), plus an optional
LittleFS `/fonts/user.ttf` for normal text only. The compiled
`lv_font_yora_montserrat_16_cyr` emergency face remains LVGL 9 `fmt_txt` ABI.

## Fonts

Ordinary firmware uses TinyTTF, not a per-size compiled C ladder.

| Asset | Role |
|-------|------|
| `fonts/yoradio_factory_font.ttf` | multilingual factory text; `FontProvider::text(px)` when no valid user file |
| LittleFS `/fonts/user.ttf` | optional user text TTF (runtime, PSRAM copy, 512 KiB cap, reboot to apply) |
| `fonts/yoradio_tabler.ttf` | 34-glyph Tabler subset; `FontProvider::icon(px)` |
| `fonts/lv_font_yora_montserrat_16_cyr.c` | emergency 16 px only |

Pixel size is a runtime request. `FontProvider::begin()` eagerly creates factory
text instances at 12, 14, 16, 18, 20, 22, 32 and 40 px and icon instances at
20, 22, 26, 28, 36 and 64 px; an accepted user TTF gets instances for the same
text sizes. Creating instances is not glyph-cache prewarming: no glyph prewarm
is performed. Current S3 glyph cache policy is text **P64** and icons **P8**.

With `/fonts/user.ttf` active, a user instance at a new size requires an existing
factory instance of the same size as fallback. For a size outside the initial
set, the first `FontProvider::text(px)` request can therefore return a newly
created factory font; a later request can create and return the user font. Both
text instance tables have 16 slots and allocation can fail; the emergency face
is another fallback. Do not assume arbitrary new sizes consistently use the user
font on their first request. Review the FontProvider lifecycle and device parity
before adopting them; changing the factory metric alone does not alter it.

A normal PlatformIO build embeds the two TTF files (`board_build.embed_files`)
and does **not** run fontTools. Optional user text is **not** embedded: it is
uploaded through WebUI Appearance to `/fonts/user.ttf`. SHA256 and coverage:
[`fonts/readme_fonts.md`](fonts/readme_fonts.md).

**Русский:** `FontProvider::begin()` заранее создаёт factory text instances для
12, 14, 16, 18, 20, 22, 32 и 40 px и icon instances для 20, 22, 26, 28, 36
и 64 px; принятый пользовательский TTF получает instances для тех же текстовых
размеров. Создание instances не является прогревом glyph cache: отдельного
прогрева глифов нет. Политика кеша S3 — text **P64**, icons **P8**.

При активном `/fonts/user.ttf` для нового размера user instance требует уже
существующий factory instance того же размера как fallback. Поэтому первый
`FontProvider::text(px)` вне начального набора может вернуть только что созданный
factory font; последующий запрос может создать и вернуть user font. В каждой
таблице текстовых instances 16 слотов, allocation может не удаться; emergency
font — ещё один fallback. Нельзя рассчитывать на user font с первого запроса
для произвольного нового размера: перед его применением проверьте lifecycle
FontProvider и parity на устройстве. Простая правка factory metric этот механизм
не меняет.

## Related tracked docs

- Theme / Custom file format: [`theme/THEME.md`](theme/THEME.md)
- Screensaver layout: [`lv_screensaver_layout_tree.md`](lv_screensaver_layout_tree.md)
- Per-screen layout trees: `screens/*_layout_tree.md`
- Boot logo notes: [`screens/bootlogo.md`](screens/bootlogo.md)
- Localization (compile-time RU/EN/PL/SK): [`../i18n/LOCALIZATION.md`](../i18n/LOCALIZATION.md)
