Author: Witaliy76 - https://github.com/Witaliy76

# LVGL Screensaver Overlay — layout tree (SSCLK production)

> **Module:** `lv_screensaver.cpp` / `lv_screensaver.h`  
> **Stage slice:** SSCLK-A3B — production hands/caps + PSRAM background; **A3B1/A3B2** — LargeFS partition + deployment preflight; **A3E** — production cleanup; **A3F-C** — retained background cache (accepted); **A3F-D** — finalize production cache; **A4B1** — corrected Light/Custom runtime backgrounds from approved fitted sources
> **ScreenType:** Overlay on `lv_layer_top()` — **not** `ILvglScreen`, **not** PageChain

---

## 1. Purpose

Full-screen screensaver overlay when `displayMode_e == SCREENSAVER`.
Production analog clock: theme-mapped Flash sprites + one LittleFS background in PSRAM.

---

## 2. Ownership

| Owner | Objects / state |
|-------|-----------------|
| `lv_screensaver.cpp` | `s_ss_root`, `s_clock_layer`, `s_background`, three `lv_image` hands, `s_center_cap`, angle cache, `s_clock_center_x/y` |
| `lv_screensaver.cpp` (retained) | `SsclkBgCache s_bg_cache` — one 460800-byte PSRAM buffer + path key; survives hide/show |
| Flash (assets) | `ssclk_production_assets.cpp` — 12 approved sprites (Dark C, Light A, Custom C + cap17) |
| PSRAM (module) | **One retained** decoded background buffer (`s_bg_cache.data`, 460800 B) for the last successfully loaded theme; runtime `s_bg_runtime_dsc` per show |
| LittleFS | Three 480×480 RGB565 backgrounds under `/screensaver_clock/` |
| Core (unchanged) | Idle ticks, wake, `network.timeinfo`, `CLOCK` request |

All `lv_*` calls: **DspTask only**.

---

## 3. Production theme pack mapping

Via `yoradio_theme_active_preset()` → `ssclk_clock_assets_for_preset()` (`ssclk_production_asset_pack.h`):

| Theme | Hand family | Hour / min / sec front (px) | Cap |
|-------|-------------|-------------------------------|-----|
| **Dark** | C — Skeleton Needle | 118 / 162 / 184 | cap17 |
| **Light** | A — Instrument Baton | 118 / 162 / 184 | cap17 |
| **Custom** | C — Skeleton Needle | 105 / 145 / 166 | cap17 |

Tails (all themes): hour 8 px, minute 10 px, second 18 px.

---

## 4. Filesystem paths (LittleFS)

| Theme | Runtime path |
|-------|----------------|
| Dark | `/screensaver_clock/ssclk_dark_bg_480.bin` |
| Light | `/screensaver_clock/ssclk_light_bg_480.bin` |
| Custom | `/screensaver_clock/ssclk_custom_bg_480.bin` |

Disk format is unchanged: a 4-byte little-endian YoRadio header (legacy color-format value 4) followed by RGB565 LE pixels, 480×480, 460804 bytes/file. The loader translates that metadata to an LVGL 9 `lv_image_header_t` / `lv_image_dsc_t` in memory; the file itself is not an LVGL 9 binary-image container.

**SSCLK-A4B1 (accepted):** Light and Custom runtime `.bin` files replaced from approved fitted complete-source PNGs. Dark unchanged. Custom uses **current-size** fit (apparent outer radius **208.733 px @480**). Light uses approved fitted complete source. Both themes visually retain four minor ticks between major hour indices. No runtime code, hands, pivot, or cache changes. A4A/A4A-R1 inpainting experiments are **not** production sources (review history only).

Source tree: `data/screensaver_clock/*.bin` → `pio run -t buildfs`.

---

## 5. Object tree

```
lv_layer_top()
└── s_ss_root                    [lv_obj, full-screen, palette fallback bg]
    └── s_clock_layer            [lv_obj, full viewport W×H, transparent]
        ├── s_background         [lv_image, PSRAM RGB565 — first child, z-bottom]
        ├── s_hand_hour            [lv_image, production descriptor]
        ├── s_hand_minute          [lv_image, production descriptor]
        ├── s_hand_second          [lv_image, production descriptor]
        └── s_center_cap           [lv_image cap17, fixed center, topmost, no rotation]
```

**Z-order:** creation order = paint order (background → hour → minute → second → cap).

**One-cap rule:** exactly **one** `lv_image` cap17; hand sprites must not embed hub dots.

### Exact overlay geometry (A3C)

Bare `lv_obj_create()` inherits **lv_theme_default** card padding — children are laid out in the **content area**, not at screen (0,0). Nested `s_ss_root` + `s_clock_layer` without style reset caused the whole clock scene to shift right/down (~16 px on 4848S040).

**Required contract (both containers):**

- `lv_obj_remove_style_all()` then explicit geometry
- `pos = (0, 0)`, `size = 480×480` (profile width/height, not `LV_PCT`)
- `pad_all` / per-side pad = 0, `border` = 0, `outline` = 0, `radius` = 0
- `transform` / `translate` = 0, scroll off, scroll pos (0,0)
- `s_ss_root`: restore palette `screensaver_background` fallback after style reset
- `s_clock_layer`: transparent background
- `s_background`: `lv_obj_set_pos(0, 0)` — no align/center, **no manual asset offset**

**Expected absolute coords (480×480):**

| Object | abs coords | size |
|--------|------------|------|
| root | 0,0 .. 479,479 | 480×480 |
| clock_layer | 0,0 .. 479,479 | 480×480 |
| background | 0,0 .. 479,479 | 480×480 image |
| hand pivot (screen) | **240, 240** | — |
| center cap pivot (screen) | **240, 240** | — |

Hand placement formulas unchanged — coordinates are relative to `s_clock_layer` whose absolute origin is screen (0,0).

---

## 6. Canonical clock center

```
s_clock_center_x = viewport_width / 2
s_clock_center_y = viewport_height / 2
```

4848S040: **(240, 240)** on 480×480.

---

## 7. Hand placement (production pivots)

For each hand:

1. `lv_image_set_src(hand, descriptor)`
2. `lv_obj_set_pos(hand, 240 - pivot_x, 240 - pivot_y)`
3. `lv_image_set_pivot(hand, pivot_x, pivot_y)`
4. `lv_image_set_antialias(hand, true)`
5. Initial `lv_image_set_rotation(hand, 0)` — updated via angle cache

### Approved pivots (sprite space)

| Theme | Role | Pivot |
|-------|------|-------|
| Dark | hour | (15, 126) |
| Dark | minute | (14, 170) |
| Dark | second | (9, 192) |
| Light | hour | (12, 126) |
| Light | minute | (11, 170) |
| Light | second | (8, 192) |
| Custom | hour | (15, 113) |
| Custom | minute | (14, 153) |
| Custom | second | (9, 174) |
| All | cap17 | (8, 8) |

Hand format: LVGL 9 `LV_COLOR_FORMAT_RGB565A8`: the complete RGB565 LE color plane is followed by the complete A8 alpha plane, suitable for `lv_image_set_rotation()`. This two-plane representation applies to the static hands/cap, not to ordinary RGB565 background files.

---

## 8. PSRAM background ownership (SSCLK-A3F-C retained cache)

- **Capacity:** 460800 bytes RGB565 pixel payload (480×480×2); one buffer only.
- **Cache key:** LittleFS path string (`/screensaver_clock/ssclk_*_bg_480.bin`).
- **Allocate:** `ps_malloc` once on first miss; **reuse same allocation** on theme change (overwrite in place, no free+realloc).
- **Hit:** same path + `valid` → no LittleFS open/read; rebuild per-show `s_bg_runtime_dsc` only.
- **Miss / theme change:** after `screensaverHide()` removed LVGL tree → read new file into retained buffer → update key + `valid` only on full success.
- **Failure:** `valid = false`; partial buffer not used; palette fallback; error logs; retry allowed on next show.
- **Hide:** `lv_obj_delete(s_ss_root)` → `detachRuntimeBackgroundDescriptor()` — **does not** `free()` retained buffer; cache key/`valid` preserved.
- **Lifetime:** retained until reboot (no module deinit API); same pattern as Main `BG_CACHE` module retention.
- **Not in hot path:** no file read on `screensaverRefreshClock()` or same-theme re-entry.

### Hardware validation (A3F-C, accepted)

Measured on 4848S040 (not runtime logs — historical acceptance note):

| Scenario | total | read | buffer |
|----------|-------|------|--------|
| First entry / miss | ~155 ms | ~141 ms | `0x3c496780` (example) |
| Same-theme re-entry / hit | ~19–22 ms | 0 | same address |
| Theme change / miss | ~150–153 ms | ~137–140 ms | same address (no free/realloc) |

Same-theme re-entry ~7–8× faster than cold miss. Production code emits error/fallback logs only — no cache-hit or preload success logs.

---

## 9. Show / hide lifecycle

### `screensaverShow()`

1. `screensaverHide()` — idempotent cleanup
2. Create `s_ss_root` with palette `screensaver_background` (fallback)
3. Create `s_clock_layer`
4. `loadBackgroundForActiveTheme()` — cache hit (no read) or miss (LFS read into retained buffer)
5. Create object tree (background → hands → cap)
6. `lv_obj_move_foreground(s_ss_root)` — z-order within `lv_layer_top()` only
7. `applyHandAnglesFromTimeinfo()`

### `screensaverHide()` — strict order

1. `lv_obj_delete(s_ss_root)` — destroy LVGL tree (no `lv_image` refs to cache buffer)
2. `detachRuntimeBackgroundDescriptor()` — reset per-show descriptor only
3. `nullAllHandles()` + `resetAngleCache()`
4. **Retained** `s_bg_cache.data` stays allocated; `path` + `valid` unchanged

**Not touched:** PageChain, `lv_screen_active()`, `lv_layer_sys()`.

---

## 9a. Accepted visual note (Light theme)

A faint thin line near the top edge (~2 px from top) on **Light** theme was investigated (A3D-R1/R2: asset decode, layer scan, cover-test, active-screen suppression). Hardware confirmed geometry and production assets; suppression did **not** remove the artifact. **Accepted** as a minor visual artifact; further investigation **deferred**. No active-screen hide/suppression in production code.

---

## 10. Failure fallback

If background load fails (missing file, bad header, `ps_malloc` fail):

- Log `[SSCLK] ...` diagnostic
- `s_background` hidden; `s_ss_root` palette remains visible
- Production hands + cap17 still created and rotated
- No reboot; lifecycle stays correct

---

## 11. Angle formulas (tenths of degree)

| Hand | Formula |
|------|---------|
| Hour | `(tm_hour % 12) * 300 + tm_min * 5 + tm_sec / 12` |
| Minute | `tm_min * 60 + tm_sec` |
| Second | `tm_sec * 60` |

Update: `lv_image_set_rotation()` only when angle changed.

---

## 12. Time / CLOCK path

```
network.ticks() → display.putRequest(CLOCK)
  → Display::loop() (_mode == SCREENSAVER)
  → screensaverRefreshClock()
  → applyHandAnglesFromTimeinfo()
```

No module-local timer.

---

## 13. Partition dependency (4848S040)

Environment **`4848S040`** uses **`partition_16MB_ota_largefs.csv`** (16 MB flash, dual OTA + large LittleFS).

Alternative layout **`partition_16MB_ota_spiffs.csv`** (6.25 MB OTA slots, ~3.38 MB FS) remains in repo for other use cases — **not** used by `4848S040`.

| Partition | Offset | Size | Notes |
|-----------|--------|------|-------|
| nvs | 0x9000 | 0x5000 | unchanged vs legacy |
| otadata | 0xE000 | 0x2000 | unchanged vs legacy |
| app0 (ota_0) | 0x10000 | **0x400000 (4 MB)** | OTA slot 0 |
| app1 (ota_1) | 0x410000 | **0x400000 (4 MB)** | OTA slot 1 |
| spiffs (LittleFS) | **0x810000** | **0x7E0000 (8,257,536 bytes)** | label/subtype `spiffs` |
| coredump | 0xFF0000 | 0x10000 | end of 16 MB |

**Filesystem build (current):** `data/` source tree includes three Screensaver backgrounds under `data/screensaver_clock/` (460804 bytes each) plus existing project assets — verified by `pio run -e 4848S040 -t buildfs`.

### Deployment warning (first migration)

- **Partition layout change** — **firmware-only OTA migration from any older partition table is prohibited.**
- **First transition** requires **controlled wired deployment**: full chip erase (recommended) → flash firmware with new table → **`uploadfs`**.
- LittleFS **offset moves** to **0x810000**; contents at the old FS offset are **not** usable after migration.
- NVS / otadata offsets unchanged (0x9000 / 0xE000), but **full erase removes their contents** — expect Wi‑Fi/station settings loss unless NVS is preserved by a deliberate non-erase migration (not recommended here).
- Do not flash firmware alone and expect backgrounds/WebUI assets to work without `uploadfs`.

---

## 14. Related files

| File | Role |
|------|------|
| `lv_screensaver.cpp` | Overlay implementation |
| `assets/ssclk_production_asset_pack.h` | Theme → paths + descriptors |
| `assets/ssclk_production_assets.h/.cpp` | Flash sprite pixel maps |
| `data/screensaver_clock/*.bin` | LittleFS background sources |

**Removed (A3B):** `ssclk_hand_probe.c/.h` — probe sprites replaced by production assets.

---

## 15. Deferred

Dim backlight, drift, encoder wake, date, text below hands, origin-page restore, JPEG/compression.
