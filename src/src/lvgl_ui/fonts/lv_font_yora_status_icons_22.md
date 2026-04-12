# `lv_font_yora_status_icons_*` — Wi‑Fi glyphs (Tabler → LVGL)

**English:** Subset icon font for the Main status line (Wi‑Fi level + disconnected).  
**Русский:** Подмножество иконок для верхней строки Main (уровень сигнала + offline).

| Generated file | Size | LVGL symbol |
|----------------|------|-------------|
| `lv_font_yora_status_icons_18.c` | 18 px | `lv_font_yora_status_icons_18` |
| `lv_font_yora_status_icons_20.c` | 20 px | `lv_font_yora_status_icons_20` |
| `lv_font_yora_status_icons_22.c` | 22 px | `lv_font_yora_status_icons_22` *(default in `wgt_status_line.cpp`)* |

Same Unicode codepoints in all three; only rasterization size differs.

---

## Source & license

- **Pack:** [@tabler/icons-webfont](https://www.npmjs.com/package/@tabler/icons-webfont) (MIT).
- **Pinned version used for PUA stability:** `3.26.0`  
  TTF URL: `https://unpkg.com/@tabler/icons-webfont@3.26.0/dist/fonts/tabler-icons.ttf`
- **Icons in subset:** `wifi-0`, `wifi-1`, `wifi-2`, `wifi` (full arc), `wifi-off` — see [Tabler Icons](https://tabler.io/icons).

**PUA (BMP), Tabler 3.26 webfont:**

| Icon     | Unicode |
|----------|---------|
| wifi-0   | U+EBA3  |
| wifi-1   | U+EBA4  |
| wifi-2   | U+EBA5  |
| wifi     | U+EB52  |
| wifi-off | U+ECFA  |

There is **no** separate `wifi-3` / `wifi-4` in Tabler; stronger levels reuse the full `wifi` glyph — see `wifi_signal_map.h`.

---

## Why strip the TTF?

`lv_font_conv` (v1.5.x) may fail on the stock Tabler TTF with errors such as *Coverage format must be 1 or 2*.  
**Fix:** remove OpenType tables that confuse the converter (safe for a pure-icon PUA subset).

Strip script (Python, **fontTools**):

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

## Generate one LVGL font (repeat for 18 / 20 / 22)

Prerequisites: Node.js (`npx`), `tabler-stripped.ttf` in the working directory.

```bash
npx --yes lv_font_conv@1.5.2 ^
  --font tabler-stripped.ttf ^
  --size 22 ^
  --bpp 4 ^
  --format lvgl ^
  --no-compress ^
  -o src/src/lvgl_ui/fonts/lv_font_yora_status_icons_22.c ^
  -r 0xEBA3-0xEBA5 -r 0xEB52 -r 0xECFA ^
  --lv-include lvgl.h
```

*(On Unix shells, replace `^` with `\` or put the command on one line.)*

- **`--size`:** `18`, `20`, or `22`.
- **`-o`:** output path must match the size in the filename.
- **`--no-compress`:** matches project `LV_USE_FONT_COMPRESSED` expectations for these assets.

After generation, refresh the file header comment (source MIT + pipeline) if the tool overwrites it.

---

## Wiring in the project

1. **`lv_conf.h`:** `LV_FONT_YORA_STATUS_ICONS_18` / `_20` / `_22` = `1` for fonts you keep; add `extern` in `LV_FONT_CUSTOM_DECLARE`.
2. **`lv_fonts.h`:** `extern const lv_font_t lv_font_yora_status_icons_*;`
3. **`wgt_status_line.cpp`:** `k_wifi_icon_font` → chosen `lv_font_t`.

---

## UTF‑8 in C++ (LVGL `const char*`)

C++20 `u8"…"` is `char8_t*`. Use `reinterpret_cast<const char*>(u8"\uEBA3")` (or helpers in `wifi_signal_map.h`) when calling `lv_label_set_text`.

---

## Do not commit

Temporary files: `tabler-icons.ttf`, `tabler-stripped.ttf` in the repo root (or any scratch path).
