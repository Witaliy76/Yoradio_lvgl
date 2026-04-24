# `lv_font_yora_info_section_icons_*` — Info Page section rail (Tabler → LVGL)

**English:** Tiny subset icon font for Stage 6.2 Info Page **section rail** only (Network / System / Display / Memory+SD). Monochrome outline Tabler glyphs.  
**Русский:** Минимальный шрифт иконок для **левого rail** страницы Info (секции). Не Main status line.

| Generated file | Size | LVGL symbol |
|----------------|------|--------------|
| `lv_font_yora_info_section_icons_24.c` | 24 px | `lv_font_yora_info_section_icons_24` |
| `lv_font_yora_info_section_icons_28.c` | 28 px | `lv_font_yora_info_section_icons_28` |
| `lv_font_yora_info_section_icons_32.c` | 32 px | `lv_font_yora_info_section_icons_32` |
| `lv_font_yora_info_section_icons_36.c` | 36 px | `lv_font_yora_info_section_icons_36` |
| `lv_font_yora_info_section_icons_40.c` | 40 px | `lv_font_yora_info_section_icons_40` |
| `lv_font_yora_info_section_icons_44.c` | 44 px | `lv_font_yora_info_section_icons_44` |

Same Unicode codepoints in all six sizes; only rasterization size differs.

---

## Source & license

- **Pack:** [@tabler/icons-webfont](https://www.npmjs.com/package/@tabler/icons-webfont) (MIT).
- **Pinned version:** `3.26.0` — TTF: `https://unpkg.com/@tabler/icons-webfont@3.26.0/dist/fonts/tabler-icons.ttf`
- **Codepoint source:** `dist/tabler-icons.css` in the same package (`.ti-<name>:before { content: "\...." }`).

---

## Icons in this subset (4 glyphs only)

| Section on Info | Tabler icon name | Unicode (PUA) | CSS (3.26.0) |
|-----------------|------------------|---------------|--------------|
| Memory / SD | `database` | U+EA88 | `\ea88` |
| Display / UI | `device-desktop` | U+EA89 | `\ea89` |
| Network | `router` | U+EB18 | `\eb18` |
| System | `cpu` | U+EF8E | `\ef8e` |

Glyph order in generated font (by codepoint): U+EA88, U+EA89, U+EB18, U+EF8E.

---

## Why strip the TTF?

Same as Wi‑Fi / weather / control subsets: `lv_font_conv` (v1.5.x) may fail on stock Tabler TTF (*Coverage format must be 1 or 2*). Strip OpenType tables with **fontTools**:

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

## Generate one LVGL font (repeat for 24 / 28 / 32 / 36 / 40 / 44)

Prerequisites: Node.js (`npx`), `tabler-stripped.ttf` in the working directory.

**Windows (PowerShell), from directory containing `tabler-stripped.ttf`:**

```powershell
$sizes = 24,28,32,36,40,44
foreach ($s in $sizes) {
  npx --yes lv_font_conv@1.5.2 `
    --font tabler-stripped.ttf `
    --size $s `
    --bpp 4 `
    --format lvgl `
    --no-compress `
    -o path/to/repo/src/src/lvgl_ui/fonts/lv_font_yora_info_section_icons_$s.c `
    -r 0xEA88-0xEA89 -r 0xEB18 -r 0xEF8E `
    --lv-include lvgl.h
}
```

- **`--no-compress`:** align with project `LV_USE_FONT_COMPRESSED` + generated bitmaps (same as other YoRadio icon fonts).
- **`-o`:** output path must match the size in the filename.

---

## Wiring in the project

1. **`lv_conf.h`:** `LV_FONT_YORA_INFO_SECTION_ICONS_24` … `_44` = `1`; add matching `extern` lines in `LV_FONT_CUSTOM_DECLARE`.
2. **`lv_fonts.h`:** `extern const lv_font_t lv_font_yora_info_section_icons_24;` … `_44`.
3. **Stage 6.2 Patch C+:** `scr_info` picks one size from `LV_ACTIVE_PROFILE` (or fixed ladder) and sets rail label font + UTF‑8 string with `u8"\uEA88"` etc. (or a small name→UTF‑8 table).

---

## UTF‑8 in C++ (LVGL `const char*`)

Use `reinterpret_cast<const char*>(u8"\uEA88")` (or helpers) — same pattern as `wifi_signal_map.h`.

---

## Do not commit

Temporary files: `tabler-icons.ttf`, `tabler-stripped.ttf`, local scratch dirs (e.g. repo `.fontwork/`). Only generated `.c` under `src/src/lvgl_ui/fonts/` belong in git.

**Do not** import the full Tabler set, **do not** merge these glyphs into Montserrat or status/weather/control icon fonts.
