# lvgl_ui - LVGL UI subsystem for YoRadio

Author: Witaliy76 - https://github.com/Witaliy76

All LVGL-related product UI lives exclusively in `src/src/lvgl_ui/`.
Do not mix LVGL screen/widget code with legacy drivers in `src/src/displays/`.

This directory hosts the LVGL 9.5.0 UI for YoRadio: PageChain navigation, product
screens, shared widgets, display profiles, theme, adapters, and assets.
All `lv_*` calls run only on **DspTask** (via `Display::loop` / `lvgl_ui` entry points).
Cross-task UI updates arrive through `display.putRequest()` and are applied on DspTask.

Primary bring-up board: **4848S040** (ST7701, 480x480, GT911). Layout constants
come from `LV_ACTIVE_PROFILE` (`profiles/`), not scattered `#ifdef DSP_MODEL` in screens.

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
| 5 | Settings | Display, theme, Auto Dim, Sleep Timer, Music Rail, Wi-Fi service entry |

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
├── profiles/                 - Display profile constants (4848S040, ...)
├── screens/                  - Product screens + layout_tree notes
├── widgets/                  - status line, Presence Rail, footer pill
├── adapters/                 - preset store, station list boundary
├── theme/                    - Dark / Light / Custom + THEME.md
├── assets/                   - Boot / screensaver assets
└── fonts/                    - Generated LVGL fonts (do not hand-edit .c)
```

## Runtime ownership

| Concern | Owner |
|---------|--------|
| `lv_*` API | DspTask only |
| Display / draw buffer | LVGL 9 `lv_display_t`; RGB565; one 480x160 PSRAM buffer (153600 B) in `LV_DISPLAY_RENDER_MODE_PARTIAL` on 4848S040 |
| Flush | Synchronous LVGL -> DisplayPort -> direct `esp_lcd`/ST7701 path; CPU rectangular blit from the 480x160 draw buffer into one 480x480 PSRAM physical framebuffer; `lv_display_flush_ready()` exactly once on every callback path |
| Touch | GT911 registered as an LVGL 9 pointer indev; its read callback runs from `lv_timer_handler()` on DspTask |
| LVGL heap | Fixed 128 KiB built-in TLSF pool backed by one process-lifetime PSRAM allocation |
| LVGL task stack | DspTask, 12288 B |
| Theme preset / Custom file | Theme module + WebUI Appearance; live reapply on DspTask |
| Station art / Main backgrounds | LittleFS + Main reload hooks on DspTask |
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

File-backed Main, Visual, and screensaver RGB565 backgrounds keep the existing YoRadio
4-byte little-endian disk header followed by RGB565 bytes. Loaders translate that header
to an in-memory LVGL 9 `lv_image_header_t` / `lv_image_dsc_t`; the files were not converted
to a new disk format. Static RGB565A8 screensaver sprites are different: they use the
LVGL 9 color-plane-then-alpha-plane representation. All 48 generated fonts use the LVGL 9
ABI and must be regenerated with `lv_font_conv@1.5.3`; the 1.5.2 output can retain the
removed v8 `.cache` field, while the v9 `fallback` field is valid.

## Related tracked docs

- Theme / Custom file format: [`theme/THEME.md`](theme/THEME.md)
- Screensaver layout: [`lv_screensaver_layout_tree.md`](lv_screensaver_layout_tree.md)
- Per-screen layout trees: `screens/*_layout_tree.md`
- Boot logo notes: [`screens/bootlogo.md`](screens/bootlogo.md)
- Localization (compile-time RU/EN/PL): [`../i18n/LOCALIZATION.md`](../i18n/LOCALIZATION.md)
