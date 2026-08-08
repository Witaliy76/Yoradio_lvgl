# `lv_font_yora_control_icons_*` — Main control band (Tabler → LVGL)

**English:** Subset icon font for Stage 6.1E Main transport + utility controls (prev/play/stop/next/list/settings + on-device alternates).  
**Русский:** Подмножество иконок для полосы управления Main (транспорт + list/settings + альтернативы для сравнения на устройстве).

| Generated file | Size | LVGL symbol |
|----------------|------|-------------|
| `lv_font_yora_control_icons_18.c` | 18 px | `lv_font_yora_control_icons_18` |
| `lv_font_yora_control_icons_20.c` | 20 px | `lv_font_yora_control_icons_20` |
| `lv_font_yora_control_icons_22.c` | 22 px | `lv_font_yora_control_icons_22` |
| `lv_font_yora_control_icons_24.c` | 24 px | `lv_font_yora_control_icons_24` |
| `lv_font_yora_control_icons_26.c` | 26 px | `lv_font_yora_control_icons_26` |
| `lv_font_yora_control_icons_28.c` | 28 px | `lv_font_yora_control_icons_28` |

Same Unicode codepoints in all sizes; only rasterization size differs.

---

## Source & license

- **Pack:** [@tabler/icons-webfont](https://www.npmjs.com/package/@tabler/icons-webfont) (MIT).
- **Pinned version:** `3.26.0` — TTF: `https://unpkg.com/@tabler/icons-webfont@3.26.0/dist/fonts/tabler-icons.ttf`

**PUA (BMP), Tabler 3.26 webfont — icons in this subset:**

| Tabler name | Unicode | Role |
|-------------|---------|------|
| player-skip-back | U+ED48 | primary prev |
| player-play | U+ED46 | play (stopped) |
| player-stop | U+ED4A | stop (playing) |
| player-skip-forward | U+ED49 | primary next |
| list | U+EB6B | primary list |
| settings | U+EB20 | primary settings |
| chevron-left | U+EA60 | alternate |
| chevron-right | U+EA61 | alternate |
| playlist | U+EEC0 | alternate |
| settings-2 | U+F5AC | alternate |

Codepoints are taken from `dist/tabler-icons.css` in the same package version.

---

## Why strip the TTF?

Same as Wi‑Fi subset: `lv_font_conv` may fail on stock Tabler TTF (*Coverage format must be 1 or 2*). Strip OpenType tables with **fontTools** (see `lv_font_yora_status_icons_22.md`). For the current LVGL 9 ABI use `lv_font_conv@1.5.3`; 1.5.2 can emit the removed v8 `.cache` field under LVGL 9.

---

## Generate one LVGL font (repeat for 18 / 20 / 22 / 24 / 26 / 28)

Prerequisites: Node.js (`npx`), `tabler-stripped.ttf` in the working directory.

```bash
npx --yes lv_font_conv@1.5.3 ^
  --font tabler-stripped.ttf ^
  --size 24 ^
  --bpp 4 ^
  --format lvgl ^
  --no-compress ^
  -o src/src/lvgl_ui/fonts/lv_font_yora_control_icons_24.c ^
  -r 0xEA60-0xEA61 -r 0xEB20 -r 0xEB6B -r 0xED46 -r 0xED48-0xED4A -r 0xEEC0 -r 0xF5AC ^
  --lv-include lvgl.h
```

- **`--size`:** `18`, `20`, `22`, `24`, `26`, or `28`.
- **`--no-compress`:** matches project `LV_USE_FONT_COMPRESSED` + generated bitmaps (same as status icons).
- **`-o`:** output path must match the size in the filename.

---

## Wiring in the project

1. **`lv_conf.h`:** `LV_FONT_YORA_CONTROL_ICONS_18` / `_20` / `_22` / `_24` / `_26` / `_28` = `1`; add `extern` in `LV_FONT_CUSTOM_DECLARE`.
2. **`lv_fonts.h`:** `extern const lv_font_t lv_font_yora_control_icons_*;`

---

## UTF‑8 in C++ (LVGL `const char*`)

PUA codepoints need proper 3‑byte UTF‑8 per glyph (see `wifi_signal_map.h` pattern); avoid raw `\x` mistakes — prefer a small name→bytes table or `u8"\uXXXX"` with `reinterpret_cast<const char*>(…)` if your toolchain allows.

---

## Do not commit

Temporary files: `tabler-icons.ttf`, `tabler-stripped.ttf` in the repo root (or any scratch path).
