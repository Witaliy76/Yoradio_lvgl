# YoRadio Custom Theme Guide

How to create and deploy `theme_custom.txt` for the **Custom** appearance preset on YoRadio LVGL devices.

---

## What this file controls

- **`theme_custom.txt` controls Custom preset colors only.** It does not change Dark or Light factory themes.
- **Factory Dark** and **Factory Light (Cloud Ivory)** are built into firmware (`kPaletteDark`, `kPaletteLight`).
- **Background images are separate files** in LittleFS (`/bg/main_*.bin`). They are **not** part of `theme_custom.txt`.
- **Boot screen** always uses a fixed dark branded look. Your custom file does not change Boot at runtime.
- **Wi‑Fi setup / recovery flow** always uses the factory Dark service palette for readability.
- **`theme.dat`** stores only the **selected preset name** (`dark`, `light`, or `custom`). It does **not** contain colors.

---

## Where the file lives

| Location | Path | Purpose |
|----------|------|---------|
| **On device (runtime)** | `/data/theme_custom.txt` | Active Custom color file on LittleFS |
| **Repository seed** | `data/theme_custom.txt` | Shipped with `uploadfs` on fresh flash |
| **Example / template** | `src/src/lvgl_ui/theme/theme_custom.example.txt` | Copy this to start editing |

**How it gets onto the device:**

1. **`uploadfs`** — files from repo `data/` (including `data/theme_custom.txt`) are written to LittleFS.
2. **WebUI → Appearance → Custom theme upload** — overwrites `/data/theme_custom.txt` on the running device.
3. **WebUI → Remove from device** — deletes the runtime file. Custom colors then fall back to the built-in **Amber Hi-Fi** palette in firmware (`kPaletteCustomBuiltin`).

Uploading a custom file does **not** switch the active preset. Select **Custom** in Appearance after upload.

---

## File format

- **Encoding:** UTF-8 plain text.
- **One key per line:** `key=#RRGGBB`
- **`#` optional on colors:** `key=RRGGBB` is also accepted (parser strips an optional `#`).
- **Comments:** lines starting with `#` are ignored.
- **Empty lines:** ignored.
- **Unknown keys:** ignored (no error).
- **Invalid lines / bad colors:** skipped; other valid lines still apply.
- **Duplicate keys:** last valid value wins.
- **Maximum size:** **4096 bytes** (WebUI upload rejects larger files).

Colors are 24-bit RGB hex (`#RRGGBB`). Alpha is not supported.

---

## Metadata

```ini
theme_dark=true
```

or

```ini
theme_dark=false
```

| Value | Meaning |
|-------|---------|
| `true`, `1`, `yes`, `on` | Custom uses **dark** LVGL widget states (checkboxes, lists, etc.) |
| `false`, `0`, `no`, `off` | Custom uses **light** LVGL widget states |
| missing / invalid | defaults to **`true`** |

This is **metadata only** — not a color. It does not affect Dark or Light presets.

**Recommendations:**

- **`theme_dark=true`** — dark Custom themes (e.g. Amber Hi-Fi / tube-amp look).
- **`theme_dark=false`** — light Custom themes (warm ivory / paper-style UIs).

---

## Backgrounds

`theme_custom.txt` does **not** contain images. Backgrounds are separate LittleFS slots:

| Preset | Device path | Repo seed (4848S040) |
|--------|-------------|----------------------|
| Dark | `/bg/main_dark.bin` | `data/bg/main_dark.bin` |
| Light | `/bg/main_light.bin` | `data/bg/main_light.bin` |
| Custom | `/bg/main_custom.bin` | `data/bg/main_custom.bin` |

Format: LVGL v8 **RGB565** `.bin` (4-byte header + pixels). Resolution must match your board (480×480 on 4848S040). Upload via WebUI Appearance or place under `data/bg/` before `uploadfs`.

Converter (developers): `tools/lvgl_png_to_rgb565_bin.py`

---

## Editable color keys

Use the groups below when editing. The canonical full example is **`theme_custom.example.txt`** (Amber Hi-Fi / Tube Amp).

### Foundation

| Key | What it affects |
|-----|-----------------|
| `device_background` | Main screen base / device backdrop behind panels |
| `panel_background` | Cards, panels, list rows background |
| `panel_border` | Panel and card borders |
| `text_primary` | Primary text (titles, station name, main labels) |
| `text_secondary` | Secondary text (artist, hints, utility icons) |
| `text_meta` | Tertiary / meta captions |
| `accent` | Primary accent (highlights, selected accents, weather icon) |
| `accent_soft` | Softer accent fills (LVGL soft/checked states) |
| `divider` | Lines, separators, buffer meter baseline |
| `overlay_scrim` | Dimmed backdrop behind overlays |

### Main / player

| Key | What it affects |
|-----|-----------------|
| `status_line_text` | Top status line primary text |
| `status_line_meta` | Top status line secondary / meta text |
| `status_weather_icon` | Weather glyph on status line |
| `status_weather_temp` | Temperature on status line |
| `status_line_bg` | Status line strip background |
| `clock_text` | Clock digits on Main |
| `live_indicator_text` | “LIVE” / on-air indicator |
| `station_name_text` | Current station title |
| `artist_text` | Artist line |
| `track_text` | Track title line |
| `meta_row_text` | Bitrate / codec meta row |
| `volume_bar_track` | Volume bar empty track |
| `volume_bar_fill` | Volume bar filled portion |
| `buffer_meter_fill` | Buffer line under bottom divider |
| `bottom_weather_text` | Bottom bar weather text |
| `bottom_ai_text` | Bottom bar AI interpretation text |

### Lists

| Key | What it affects |
|-----|-----------------|
| `list_row_text` | Station list row text |
| `list_row_selected_bg` | Selected row background |
| `list_row_selected_text` | Selected row text |
| `list_row_separator` | Row dividers |

### Overlay

| Key | What it affects |
|-----|-----------------|
| `overlay_card_bg` | Popup / overlay card background |
| `overlay_title_text` | Overlay title |
| `overlay_body_text` | Overlay body text |

### Screensaver

| Key | What it affects |
|-----|-----------------|
| `screensaver_background` | Screensaver backdrop |
| `screensaver_clock_text` | Screensaver clock color |

### Boot (parser / fallback only)

Parsed if present; **Boot screen runtime stays fixed dark** and does not read this file.

| Key | What it affects |
|-----|-----------------|
| `boot_background` | Boot fallback background token |
| `boot_status_text` | Boot fallback status text |
| `boot_progress_track` | Boot progress bar track |
| `boot_progress_fill` | Boot progress bar fill |

### Main chrome

Control shelf, rim glow, art frame, and pressed button feedback on Main.

| Key | What it affects |
|-----|-----------------|
| `main_chrome_bg` | Control band body and rim-glow edge color |
| `main_chrome_border` | Control band border and art-slot frame |
| `main_chrome_glow_top` | Top glow peak on control shelf |
| `main_chrome_glow_bottom` | Bottom glow peak on control shelf |
| `main_chrome_pressed_bg` | Pressed state on control-bar icon buttons |

Shelf opacity, radius, and padding are **not** in this file — they are fixed in firmware layout code.

### Presence Rail (Stage 8 E22M)

Decorative procedural line effects on the Main control band use these tokens (not `accent` / `buffer_meter_fill` mix).

| Key | What it affects |
|-----|-----------------|
| `rail_accent` | Primary line color for all Presence Rail profiles |
| `rail_opa_scale` | Global opacity multiplier in **percent** (`100` = 1.0). Applied to every profile opacity. Range `0`–`200`. |

**Built-in factory values (firmware, not editable via this file):**

| Preset | `rail_accent` | `rail_opa_scale` |
|--------|---------------|------------------|
| Dark | `#5AA7E8` (icy cyan-blue) | `100` |
| Light (Cloud Ivory) | `#7A8F9A` (soft cloud blue-gray) | `60` |
| Custom builtin (Amber Hi-Fi) | `#B06A24` (burnt copper) | `90` |

**Backward compatibility:** older `theme_custom.txt` files without these keys keep the Custom **builtin** `rail_accent` / `rail_opa_scale` already loaded from firmware. Missing keys do not fail parsing.

`rail_opa_scale` is numeric metadata (like `theme_dark`), not a color — use `rail_opa_scale=90`, not `#...`.

---

## Minimal example

```ini
theme_dark=true
device_background=#120D09
panel_background=#1F1711
text_primary=#F4E7D2
accent=#D89A2B
```

Add more keys as needed. Stay under 4096 bytes.

---

## Full example

Copy and edit either:

- **`src/src/lvgl_ui/theme/theme_custom.example.txt`** — Amber Hi-Fi / Tube Amp (all supported keys)
- **`data/theme_custom.txt`** — same content, used as LittleFS seed on `uploadfs`

Both files in the repository are kept **byte-identical**.

---

## Safe workflow

1. Copy `theme_custom.example.txt` to a new file on your PC.
2. Edit colors (`key=#RRGGBB`). Keep `theme_dark` appropriate for your palette.
3. Check size ≤ **4096 bytes**.
4. Deploy:
   - **WebUI:** Appearance → choose file → Upload → select **Custom** preset.
   - **Or** save as `data/theme_custom.txt` in the repo and run `uploadfs` (overwrites whole LittleFS image from `data/`).
5. Reboot and verify Main, lists, and overlays.
6. To revert colors only: WebUI **Remove** (falls back to firmware Amber builtin) or upload a new file.

**Note:** `uploadfs` replaces the entire LittleFS partition from repo `data/`. User files on the device that are not in `data/` will be lost. Back up playlists / Wi‑Fi CSV if needed.

---

## Presets at a glance

| Preset | Colors from | Background slot |
|--------|-------------|-----------------|
| **Dark** | Firmware | `/bg/main_dark.bin` |
| **Light** | Firmware (Cloud Ivory) | `/bg/main_light.bin` |
| **Custom** | `theme_custom.txt` + Amber builtin fallback | `/bg/main_custom.bin` |

Selected preset is stored in `/data/theme.dat` (name only). Default after fresh flash with no `theme.dat`: **Dark**.
