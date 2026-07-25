# lvgl_ui - LVGL UI subsystem for YoRadio

Author: Witaliy76 - https://github.com/Witaliy76

All LVGL-related product UI lives exclusively in `src/src/lvgl_ui/`.
Do not mix LVGL screen/widget code with legacy drivers in `src/src/displays/`.

This directory hosts the LVGL 8.3 UI for YoRadio: PageChain navigation, product
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
├── lv_conf.h                 - LVGL 8.3 compile-time configuration
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
| Framebuffer / flush | Display pipeline (Canvas path until later rendering stages) |
| Theme preset / Custom file | Theme module + WebUI Appearance; live reapply on DspTask |
| Station art / Main backgrounds | LittleFS + Main reload hooks on DspTask |
| Weather data | Core `WeatherState` (fetch off UI); Weather page is read-only consumer |
| Presets | `adapters/preset_store` on LittleFS |

## Related tracked docs

- Theme / Custom file format: [`theme/THEME.md`](theme/THEME.md)
- Screensaver layout: [`lv_screensaver_layout_tree.md`](lv_screensaver_layout_tree.md)
- Per-screen layout trees: `screens/*_layout_tree.md`
- Boot logo notes: [`screens/bootlogo.md`](screens/bootlogo.md)
- Localization (compile-time RU/EN/PL): [`../i18n/LOCALIZATION.md`](../i18n/LOCALIZATION.md)
