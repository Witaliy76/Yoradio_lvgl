# `lv_font_yora_settings_icons_28` — Settings Page category icons (Tabler → LVGL)

**English:** Dedicated Tabler subset for Stage 6.7S Settings main page — five category-row icons only.
**Русский:** Отдельный Tabler-subset для главной страницы Settings — только пять иконок категорий.

| Generated file | Size | LVGL symbol |
|----------------|------|-------------|
| `lv_font_yora_settings_icons_28.c` | 28 px | `lv_font_yora_settings_icons_28` |

All five glyphs share one rasterization size and Tabler 3.26.0 PUA codepoints.

---

## Source & license

- **Pack:** [@tabler/icons-webfont](https://www.npmjs.com/package/@tabler/icons-webfont) (MIT).
- **Pinned version:** `3.26.0` — TTF: `https://unpkg.com/@tabler/icons-webfont@3.26.0/dist/fonts/tabler-icons.ttf`
- **Codepoint source:** `dist/tabler-icons.css` in the same package (`.ti-<name>:before { content: "\...." }`).
- **Do not** bump Tabler version without regenerating this font and re-verifying glyphs on device.

---

## Icons in this subset (5 glyphs only)

| Settings row | Tabler icon name | Unicode (PUA) | CSS (3.26.0) |
|--------------|------------------|---------------|--------------|
| DISPLAY | `device-desktop` | U+EA89 | `\ea89` |
| WI-FI | `router` | U+EB18 | `\eb18` |
| MUSIC RAIL | `wave-sine` | U+ECD4 | `\ecd4` |
| SLEEP TIMER | `moon-stars` | U+ECE7 | `\ece7` |
| AI LAYER | `sparkles` | U+F6D7 | `\f6d7` |

Glyph order in generated font (by codepoint): U+EA89, U+EB18, U+ECD4, U+ECE7, U+F6D7.

**Scope rule:** this subset must contain **only** these five Settings category icons. Do not merge with Info, Weather, or Control fonts.

**Expected visual size on Settings:** 28 px icon column (`lv_font_yora_settings_icons_28`); row height ~60–64 px on 480×480.

---

## Why strip the TTF?

Same as other YoRadio Tabler subsets: `lv_font_conv` may fail on stock Tabler TTF (*Coverage format must be 1 or 2*). Strip OpenType tables with **fontTools** (see `lv_font_yora_status_icons_22.md`). For the current LVGL 9 ABI use `lv_font_conv@1.5.3`; 1.5.2 can emit the removed v8 `.cache` field under LVGL 9.

---

## Generate (PowerShell, from repo root)

Prerequisites: Node.js (`npx`), `\.fontwork\tabler-stripped.ttf` (not committed).

```powershell
$f = (Resolve-Path ".fontwork\tabler-stripped.ttf").Path
npx --yes lv_font_conv@1.5.3 `
  --font $f `
  --size 28 `
  --bpp 4 `
  --format lvgl `
  --no-compress `
  -o src\src\lvgl_ui\fonts\lv_font_yora_settings_icons_28.c `
  -r 0xEA89 `
  -r 0xEB18 `
  -r 0xECD4 `
  -r 0xECE7 `
  -r 0xF6D7 `
  --lv-include lvgl.h
```

- **`lv_font_conv`:** `1.5.3` (LVGL 9 ABI)
- **`--no-compress`:** matches project `LV_USE_FONT_COMPRESSED` expectations.
- Refresh the file header comment after generation if the tool overwrites it.

---

## Wiring in the project

1. **`lv_conf.h`:** `LV_FONT_YORA_SETTINGS_ICONS_28` = `1`; add `extern` in `LV_FONT_CUSTOM_DECLARE`.
2. **`lv_fonts.h`:** `extern const lv_font_t lv_font_yora_settings_icons_28;`
3. **`settings_glyph_utf8.h`:** `YORA_SETTINGS_GLYPH_*` macros for `scr_settings.cpp`.
4. **`scr_settings.cpp`:** `lv_obj_set_style_text_font(..., &lv_font_yora_settings_icons_28, ...)`.

---

## Flash delta measurement

**Do not** estimate from `.c` source file size. Measure linked contribution:

1. `pio run -e 4848S040`
2. `xtensa-esp32s3-elf-nm -S .pio/build/4848S040/firmware.elf | grep settings_icons_28`
3. Sum `.rodata.glyph_bitmap` and descriptor sections for the font object (or diff `firmware.bin` before/after first runtime reference).

`extern` in `lv_conf.h` alone does **not** retain the font — a reachable reference from screen code is required (`--gc-sections`).

---

## Do not commit

Temporary files: `tabler-icons.ttf`, `tabler-stripped.ttf`, `.fontwork/` scratch. Only generated `.c` and this manifest belong in git.
