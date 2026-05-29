# YoRadio LVGL theme — developer map (Stage 6.6R)

Canonical token semantics: **`docs/YoRadio_LVGL_Theme_Bible.txt`** (v1.2).  
This file documents **where code lives**, **runtime behavior**, and **boundaries** after Stages **6.6R-F2 / GA / GB / GB2** — not legacy Canvas / `config.theme`.

> **6.6R-G track summary:** GA tokenized Main shelf/glow/pressed chrome (5 new `main_chrome_*` tokens, live glow reapply); GB set factory Light to **Cloud Ivory / Warm Cloudscape**; GB1 tuned Light shelf opacity + pressed feedback; GB2 made the debug perf-monitor text theme-aware. No `CONFIG_VERSION` / parser / WebUI changes in that track.

---

## 1. Theme pipeline (current)

```text
LittleFS                          DspTask (display queue)
──────────                        ───────────────────────
/data/theme.dat        ──load──►  ThemePreset (Dark|Light|Custom)
  preset name only                yoradio_theme_set_preset()

/data/theme_custom.txt ──load──►  s_customPalette (mutable)
  color overrides + theme_dark      s_customThemeDark (metadata)
                                  yoradio_palette() / yoradio_theme_is_dark()

lv_theme_yoradio.cpp              lv_theme_default_init + yoradio_theme_reinit
  kPaletteDark   (const factory)  accent, accent_soft, dark flag for LVGL widgets
  kPaletteLight  (const factory)
  kPaletteCustomBuiltin (const fallback baseline)
  s_customPalette (runtime, file-backed)

Screens / widgets                 PageChain runtime switch (6.6R-C)
  yoradio_palette()               liveReapplyTheme() per page
  pal.<token>                     PageChain::reapplyThemeToCreatedPages()
```

| Component | Role |
|-----------|------|
| **`lv_theme_yoradio.h`** | `ThemePreset`, `YoRadioPalette`, public APIs (`yoradio_palette`, `yoradio_theme_init/reinit`, custom file load, `yoradio_theme_is_dark`, `yoradio_palette_service`) |
| **`lv_theme_yoradio.cpp`** | Factory Dark/Light tables; built-in Custom fallback; parser for `/data/theme_custom.txt`; `lv_theme_default_init` bridge |
| **`lvgl_ui.cpp`** | `onThemePresetChanged`, `onCustomThemeFileUpdated` — **DspTask only** |
| **`display.cpp`** | `SET_THEME_PRESET`, `CUSTOM_THEME_FILE_UPDATED` queue handlers |
| **`netserver.cpp`** | WebUI: `/set_theme`, `/upload_theme`, `/remove_theme`, `/bg_status` — **no `lv_*`** |

Init entry (once): **`yoradio_theme_init(s_disp)`** after `lv_disp_drv_register` in `initDisplayDriver` (`lvgl_ui.cpp`).

**Not LVGL source of truth:** `config.theme`, `mytheme.h`, legacy `COLOR_*` macros (Canvas path).

---

## 2. Two files — two meanings (do not merge)

### `/data/theme.dat`

| | |
|---|---|
| **Purpose** | Selected **preset name only** (which of Dark / Light / Custom is active) |
| **Values** | `dark`, `light`, or `custom` (one token, trimmed) |
| **Does NOT contain** | Colors, `theme_dark`, or layout |
| **Written when** | User picks Dark/Light/Custom in WebUI (`yoradio_theme_save_persisted_preset`) |
| **Loaded when** | Boot, before first `yoradio_theme_init` |

### `/data/theme_custom.txt`

| | |
|---|---|
| **Purpose** | **Color overrides** for **Custom preset only** (+ optional metadata) |
| **Format** | `key=#RRGGBB` (or `RRGGBB` without `#`); `#` comments; empty lines ignored |
| **Max size** | **4096 bytes** (upload rejected above) |
| **Does NOT select** | Active preset — use `theme.dat` + WebUI preset row |
| **Upload does NOT** | Auto-switch to Custom; user must select **Custom** in Appearance |

**Rules:** partial file OK; unknown keys ignored; invalid colors/lines counted in stats; duplicate keys → last valid wins; missing file → built-in Custom fallback (`kPaletteCustomBuiltin`).

---

## 3. Preset behavior

| Preset | Palette source | Affected by `theme_custom.txt`? |
|--------|----------------|----------------------------------|
| **Dark** | `kPaletteDark` (const, compiled-in) | **No** |
| **Light** | `kPaletteLight` (const, compiled-in) — **Cloud Ivory / Warm Cloudscape** (6.6R-GB) | **No** |
| **Custom** | `s_customPalette` = builtin fallback + file overrides | **Yes** (colors only) |

- `yoradio_palette()` with active **Custom** → **`s_customPalette`** (runtime), not a bare const copy of Dark.
- If file missing / invalid → safe **builtin Custom fallback** (same baseline as early Custom factory table).
- **Dark/Light** are never modified by upload/remove of custom file.

---

## 4. Runtime switching & persistence (6.6R)

| Action | Path |
|--------|------|
| User selects Dark/Light/Custom in WebUI | `POST /set_theme` → `SET_THEME_PRESET` → DspTask → `onThemePresetChanged` |
| Save preset for reboot | `yoradio_theme_save_persisted_preset` → `/data/theme.dat` |
| Upload custom file | `POST /upload_theme` → FS commit → `CUSTOM_THEME_FILE_UPDATED` → DspTask → `onCustomThemeFileUpdated` |
| Remove custom file | `POST /remove_theme` → same queue |

**Live apply:**

- **Custom active** + upload/remove custom file → reload palette + `yoradio_theme_reinit` + `reapplyThemeToCreatedPages()` (Main, Info, Station, stubs).
- **Dark/Light active** + upload custom file → file stored; UI stays on Dark/Light until user selects **Custom**.

**Threading:** NetServer must **not** call LVGL. Parsing palette for UI effect is **authoritative on DspTask** (after F2 dedupe). `/bg_status` may read file size from disk; `applied_keys` come from last DspTask parse.

---

## 5. `theme_dark` metadata (Custom only)

In `theme_custom.txt` (not a `YoRadioPalette` field):

```ini
theme_dark=true
# or
theme_dark=false
```

| Value | Meaning |
|-------|---------|
| `true`, `1`, `yes`, `on` | Custom uses **dark** LVGL default widget states (`lv_theme_default_init` dark flag) |
| `false`, `0`, `no`, `off` | Custom uses **light** LVGL widget states |
| missing / invalid | Defaults to **`true`** (same as F1) |

Case-insensitive. Does **not** change Dark/Light. Does **not** count as a color key in `applied_keys`. When `theme_dark` changes while Custom is active, reload triggers `yoradio_theme_reinit` like color changes.

---

## 5b. Main chrome tokens (6.6R-GA) & Cloud Ivory Light (6.6R-GB)

### Main chrome tokens

GA added 5 semantic tokens so Main control-shelf colors are no longer local hardcode and are **Custom-overridable**:

| Token | Used for |
|-------|----------|
| `main_chrome_bg` | control band body **and** rim glow edge stops |
| `main_chrome_border` | control band border **and** art-slot frame (reused) |
| `main_chrome_glow_top` | rim glow top peak |
| `main_chrome_glow_bottom` | rim glow bottom peak |
| `main_chrome_pressed_bg` | control-button pressed background |

**Reuse / not tokenized (by design):**
- Icons reuse `text_primary` (transport) / `text_secondary` (list, settings).
- Separators / buffer line reuse `divider`.
- Shelf glow **edge** reuses `main_chrome_bg` (no separate edge token).
- Shadow stays **local black + opacity**.
- **Geometry / radius / padding / opacity stay local** in `scr_main.cpp` (e.g. theme-aware shelf `bg_opa`/`border_opa` and pressed opa from GB1) — **not** theme tokens.

**Live reapply:** since GA the rim glow gradient stops are refreshed in `LvglMainScreen::liveReapplyTheme()` (descriptors are file-scope static, objects stored as members) → **no stale glow** after a runtime theme switch; shelf body/border/opacity, art frame, pressed bg/opa update together.

### Cloud Ivory / Warm Cloudscape (factory Light, 6.6R-GB)

Light direction: warm ivory device background, warm beige panels/borders, graphite primary text, taupe secondary/meta, restrained amber accent, warm cream shelf glow. Screensaver **intentionally stays dark** on Light (night/device behavior).

| Token | Light value |
|-------|-------------|
| `device_background` | `#F8EFE3` |
| `panel_background` | `#F9EFE2` |
| `panel_border` | `#D3C4B3` |
| `text_primary` | `#2F2926` |
| `text_secondary` | `#6F6459` |
| `accent` | `#C8942E` |
| `accent_soft` | `#E0CBA0` (softened — raw gold too active for LVGL soft/checked states) |
| `buffer_meter_fill` | `#8A7D70` (quiet taupe — not amber) |
| `volume_bar_fill` | `#E7AF58` |
| `main_chrome_bg` | `#F9EFE2` |
| `main_chrome_border` | `#D3C4B3` |
| `main_chrome_glow_top` | `#FEF6E6` |
| `main_chrome_glow_bottom` | `#F4E3CC` |
| `main_chrome_pressed_bg` | `#D8C3A2` (GB1 — darker so press reads on ivory) |

---

## 6. Boundaries & exceptions (F2 / GA / GB2)

| Area | Policy |
|------|--------|
| **Boot** (`scr_boot.cpp`) | **Fixed branded dark** (`#000000` bg, `#CCCCCC` status). Does **not** use `yoradio_palette()` or user custom file. Saved Light/Custom preset does **not** whiten Boot. Shuttle/track/glow = local chrome in `scr_boot.cpp`. |
| **Wi‑Fi Flow** (`scr_wifi_flow.cpp`) | **`yoradio_palette_service()`** → always factory **Dark**. Arbitrary Custom palette cannot break recovery UI. No `liveReapplyTheme`. |
| **Main shelf / glow / control band** | **Tokenized** since GA (`main_chrome_*`) — **Custom-overridable** via `theme_custom.txt`. Geometry/opacity stay local in `scr_main.cpp`. |
| **Perf monitor** (`lvgl_ui.cpp`) | **Debug overlay**, not product UI (`LV_USE_PERF_MONITOR`). Background **transparent**; text color follows `yoradio_palette().text_primary` (GB2, theme-aware). Disabled when perf monitor is off. |
| **Background images** | Separate LittleFS slots: `/bg/main_dark.bin`, `main_light.bin`, `main_custom.bin` — not `theme_custom.txt`. |
| **Fonts / layout** | Profiles (`lv_profile_*.h`), `lv_conf.h`, per-widget code — not theme file. |
| **Station art** | Performance follow-up is separate; not a theme concern. |

`boot_*` keys exist in `YoRadioPalette` and parser for completeness; **Boot screen ignores them at runtime** after F2.

---

## 7. WebUI workflow (Appearance)

1. **Preset row:** Dark / Light / Custom → immediate apply + save `theme.dat`.
2. **Custom file block:**
   - **Choose file** — pick `.txt` on PC.
   - **Upload** — write `/data/theme_custom.txt` (atomic temp → commit).
   - **Remove from device** — delete file, reset runtime Custom palette to fallback.
3. **Status (two lines):**
   - **On device:** file name, size, color keys loaded, ignored line count (from `/bg_status`).
   - **Action hint:** ready to upload / applying on device / file on device.

**Note:** Normal user theme upload does **not** require `uploadfs`. Hard refresh / `uploadfs` only when **WebUI assets** (`appearance.html`, `bg.js`) change during development.

Example file for upload: **`src/src/lvgl_ui/theme/theme_custom.example.txt`** (canonical).  
Mirror (identical): **`docs/examples/theme_custom.example.txt`**.

---

## 8. Where to change what

| Task | Where |
|------|--------|
| Factory **Dark/Light** colors | `lv_theme_yoradio.cpp` → `kPaletteDark` / `kPaletteLight` |
| **Custom** user colors | `/data/theme_custom.txt` via WebUI, or edit `theme_custom.example.txt` and upload |
| Built-in **Custom fallback** (no file) | `kPaletteCustomBuiltin` in `lv_theme_yoradio.cpp` |
| Add/rename palette token | `YoRadioPalette` in `.h` + factory palettes + `kPaletteKeyTable` in `.cpp` + **both** example files + Bible + this doc |
| **Main chrome visual colors** | `main_chrome_*` tokens in `lv_theme_yoradio.cpp` (per preset) |
| **Main chrome geometry / opacity** | `scr_main.cpp` (local: radius/padding, theme-aware `bg_opa`/`border_opa`, pressed opa) |
| **Boot** look (fixed dark) | `scr_boot.cpp` → `kBootFixedBackground` / `kBootFixedStatusText` |
| **Wi‑Fi** service colors | `scr_wifi_flow.cpp` uses `yoradio_palette_service()` only |
| **Perf monitor** debug overlay | `lvgl_ui.cpp` (transparent bg; text = `yoradio_palette().text_primary`) |
| LVGL default font | `lv_conf.h` → `LV_FONT_DEFAULT` |
| Board fonts / layout | `profiles/lv_profile_*.h` |
| Runtime preset switch wiring | `netserver.cpp`, `display.cpp`, `lvgl_ui.cpp` (do not call LVGL from NetServer) |

---

## 9. API quick reference (`lv_theme_yoradio.h`)

```text
yoradio_palette()                    → active preset palette (Custom → s_customPalette)
yoradio_palette_service()            → factory Dark (Wi‑Fi Flow)
yoradio_theme_active_preset()
yoradio_theme_set_preset(p)
yoradio_theme_is_dark(p)             → LVGL dark flag; Custom uses theme_dark metadata
yoradio_theme_init(disp)             → once after disp register; loads theme.dat + theme_custom.txt
yoradio_theme_reinit(disp)           → DspTask only; after preset/custom file change
yoradio_theme_load/save_persisted_preset()
yoradio_theme_load_custom_palette_file()
yoradio_theme_custom_parse_stats()   → for /bg_status
```

Screen pattern:

```cpp
const YoRadioPalette& pal = yoradio_palette();
lv_obj_set_style_text_color(label, pal.station_name_text, LV_PART_MAIN);
```

Runtime reapply: override `liveReapplyTheme()` on `ILvglScreen` pages; call chain from `onThemePresetChanged` / `onCustomThemeFileUpdated`.

---

## 10. Who uses which tokens (repository map)

Search: `yoradio_palette`, `yoradio_palette_service`, `pal.` in `lvgl_ui/`.

| Area | File | Notes |
|------|------|--------|
| Main | `screens/scr_main.cpp` | Palette tokens + **local** shelf chrome |
| Info | `screens/scr_info.cpp` | `liveReapplyTheme` |
| Station | `screens/scr_station.cpp` | `liveReapplyTheme` |
| Stubs | `screens/scr_stub.cpp` | Visual / Weather / Settings |
| Status line | `widgets/wgt_status_line.cpp` | `reapplyTheme` |
| Overlays | `lv_overlay.cpp` | scrim, titles |
| Screensaver | `lv_screensaver.cpp` | saver tokens |
| Boot | `screens/scr_boot.cpp` | **Fixed dark** — not `yoradio_palette()` |
| Wi‑Fi Flow | `screens/scr_wifi_flow.cpp` | **`yoradio_palette_service()`** only |

---

## 11. Do not change without plan

- `yoradio_theme_init` placement (after display register only).
- LVGL flush / buffer model (Stage 8A topic).
- `CONFIG_VERSION` / `config_t` / NVS for theme (LittleFS only for 6.6R).
- PlatformIO / LVGL dependency versions for theme work.

---

*Updated for Stage **6.6R-GC** (docs). Runtime behavior reflects **GA** (Main chrome tokens + live glow), **GB/GB1** (Cloud Ivory Light + shelf/pressed tuning), **GB2** (perf-monitor text theme-aware).*
