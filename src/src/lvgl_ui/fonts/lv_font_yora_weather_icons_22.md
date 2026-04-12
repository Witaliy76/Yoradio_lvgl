# `lv_font_yora_weather_icons_*` — OWM weather glyphs (Tabler → LVGL)

**English:** Subset icon font for the Main bottom-row weather mini (OWM icon code → glyph + °C).  
**Русский:** Подмножество иконок для компактной погоды на Main (код OWM → глиф + °C).

| Generated file | Size | LVGL symbol |
|----------------|------|-------------|
| `lv_font_yora_weather_icons_18.c` | 18 px | `lv_font_yora_weather_icons_18` |
| `lv_font_yora_weather_icons_20.c` | 20 px | `lv_font_yora_weather_icons_20` |
| `lv_font_yora_weather_icons_22.c` | 22 px | `lv_font_yora_weather_icons_22` *(default in `scr_main.cpp` weather glyph)* |
| `lv_font_yora_weather_icons_24.c` | 24 px | `lv_font_yora_weather_icons_24` |
| `lv_font_yora_weather_icons_28.c` | 28 px | `lv_font_yora_weather_icons_28` |

Same Unicode codepoints in all sizes; only rasterization size differs.

---

## Source & license

- **Pack:** [@tabler/icons-webfont](https://www.npmjs.com/package/@tabler/icons-webfont) (MIT).
- **Pinned version used for PUA stability:** `3.26.0`  
  TTF URL: `https://unpkg.com/@tabler/icons-webfont@3.26.0/dist/fonts/tabler-icons.ttf`
- **Pipeline parity:** same strip + `lv_font_conv` flow as `lv_font_yora_status_icons_22.md` (Wi‑Fi row).

**PUA (BMP), Tabler 3.26 webfont — icons in this subset:**

| Tabler name | Unicode | OWM mapping (see `weather_owm_glyph.h`) |
|-------------|---------|----------------------------------------|
| cloud-rain | U+EA72 | `09d` / `09n` |
| cloud-storm | U+EA74 | `11d` / `11n` |
| cloud | U+EA76 | `03`/`04` (d/n), fallback |
| droplet | U+EA97 | `10d` / `10n` *(product text says “droplets”; Tabler 3.26 has `droplet` only)* |
| sun | U+EB30 | `01d` |
| snowflake | U+EC0B | `13d` / `13n` |
| cloud-fog | U+ECD9 | `50d` / `50n` |
| moon-stars | U+ECE7 | `01n` |
| haze | U+EFAA | `02d` |
| haze-moon | U+FAF8 | `02n` |

---

## Why strip the TTF?

Same as Wi‑Fi subset: `lv_font_conv` (v1.5.x) may fail on the stock Tabler TTF (*Coverage format must be 1 or 2*).  
Strip OpenType tables that confuse the converter (safe for a pure-icon PUA subset).

Strip script (Python, **fontTools**) — identical to `lv_font_yora_status_icons_22.md`:

```python
from fontTools.ttLib import TTFont

src = "tabler-icons.ttf"
dst = "tabler-stripped.ttf"
font = TTFont(src)
for tag in ("GSUB", "GPOS", "GDEF", "MVAR", "STAT", "morx", "feat", "meta", "avar"):
    if tag in font:
        del font[tag]
font.save(dst)
```

---

## Generate one LVGL font (repeat for 18 / 20 / 22 / 24 / 28)

Prerequisites: Node.js (`npx`), `tabler-stripped.ttf` in the working directory.

```bash
npx --yes lv_font_conv@1.5.2 ^
  --font tabler-stripped.ttf ^
  --size 22 ^
  --bpp 4 ^
  --format lvgl ^
  --no-compress ^
  -o src/src/lvgl_ui/fonts/lv_font_yora_weather_icons_22.c ^
  -r 0xEA72 -r 0xEA74 -r 0xEA76 -r 0xEA97 -r 0xEB30 -r 0xEC0B -r 0xECD9 -r 0xECE7 -r 0xEFAA -r 0xFAF8 ^
  --lv-include lvgl.h
```

*(On Unix shells, replace `^` with `\` or put the command on one line.)*

- **`--size`:** `18`, `20`, `22`, `24`, or `28`.
- **`-o`:** output path must match the size in the filename.
- **`--no-compress`:** matches project `LV_USE_FONT_COMPRESSED` expectations for these assets.

After generation, refresh the file header comment (source MIT + pipeline) if the tool overwrites it.

---

## Wiring in the project

1. **`lv_conf.h`:** `LV_FONT_YORA_WEATHER_ICONS_18` … `_28` = `1` for fonts you keep; add `extern` in `LV_FONT_CUSTOM_DECLARE`.
2. **`lv_fonts.h`:** `extern const lv_font_t lv_font_yora_weather_icons_*;`
3. **`scr_main.cpp`:** weather glyph label → chosen `lv_font_t`; temperature stays on Montserrat (`bottom_weather_text`).

---

## UTF‑8 in C++ (LVGL `const char*`)

C++20 `u8"…"` is `char8_t*`. Use `reinterpret_cast<const char*>(u8"\uEA76")` (or helpers in `weather_owm_glyph.h`) when calling `lv_label_set_text`.

---

## Do not commit

Temporary files: `tabler-icons.ttf`, `tabler-stripped.ttf` in the repo root (or any scratch path).
