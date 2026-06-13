# `lv_font_yora_station_icons_*` — Station Page icons (Tabler → LVGL)

**English:** Minimal Tabler subset: current-station marker + bottom swipe hint (same font group).  
**Русский:** Минимальный subset: маркер текущей станции и нижняя подсказка жеста (один шрифт).

| Generated file | Size | LVGL symbol |
|----------------|------|-------------|
| `lv_font_yora_station_icons_14.c` | 14 px | `lv_font_yora_station_icons_14` |
| `lv_font_yora_station_icons_15.c` | 15 px | `lv_font_yora_station_icons_15` |
| `lv_font_yora_station_icons_16.c` | 16 px | `lv_font_yora_station_icons_16` |
| `lv_font_yora_station_icons_17.c` | 17 px | `lv_font_yora_station_icons_17` |
| `lv_font_yora_station_icons_18.c` | 18 px | `lv_font_yora_station_icons_18` |
| `lv_font_yora_station_icons_19.c` | 19 px | `lv_font_yora_station_icons_19` |
| `lv_font_yora_station_icons_20.c` | 20 px | `lv_font_yora_station_icons_20` *(bottom hint icon on Station Page)* |
| `lv_font_yora_station_icons_21.c` | 21 px | `lv_font_yora_station_icons_21` |
| `lv_font_yora_station_icons_22.c` | 22 px | `lv_font_yora_station_icons_22` *(current-station volume marker)* |
| `lv_font_yora_station_icons_23.c` | 23 px | `lv_font_yora_station_icons_23` |
| `lv_font_yora_station_icons_24.c` | 24 px | `lv_font_yora_station_icons_24` |

Same two codepoints in all sizes; only rasterization size differs — for 320×480 and other profiles.

## Source & License

- **Pack:** `@tabler/icons-webfont` (MIT)
- **Pinned version:** `3.26.0`
- **Glyphs:** `volume-2` (current-station marker), `hand-click` (bottom swipe hint)
- **Unicode:** `U+EB4F`, `U+EF4F`

## Generate (batch)

From a directory containing `tabler-stripped.ttf` (see `lv_font_yora_status_icons_22.md` for strip script).  
**PowerShell** (paths relative to repo root):

```powershell
$font = (Resolve-Path ".fontwork\tabler-stripped.ttf").Path
$outBase = "src\src\lvgl_ui\fonts"
foreach ($s in 14..24) {
  $out = Join-Path $outBase "lv_font_yora_station_icons_$s.c"
  npx --yes lv_font_conv@1.5.2 `
    --font $font --size $s --bpp 4 --format lvgl --no-compress `
    -o $out -r 0xEB4F,0xEF4F --lv-include lvgl.h
}
```

Single file example:

```bash
npx --yes lv_font_conv@1.5.2 ^
  --font tabler-stripped.ttf ^
  --size 20 ^
  --bpp 4 ^
  --format lvgl ^
  --no-compress ^
  -o src/src/lvgl_ui/fonts/lv_font_yora_station_icons_20.c ^
  -r 0xEB4F,0xEF4F ^
  --lv-include lvgl.h
```

`tabler-stripped.ttf` is produced from Tabler's TTF with the same strip process documented in `lv_font_yora_status_icons_22.md`.

## Wiring

- `lv_conf.h`: `LV_FONT_YORA_STATION_ICONS_14` … `_24` = `1` and `LV_FONT_CUSTOM_DECLARE`
- `lv_fonts.h`: `extern const lv_font_t lv_font_yora_station_icons_*;`
- UTF-8: `station_glyph_utf8_volume_2()` (marker), `station_glyph_utf8_hand_click()` (hint); legacy `control_glyph_utf8_volume_2()` = same PUA as volume-2
- `scr_station.cpp`: marker `lv_font_yora_station_icons_22`, hint `lv_font_yora_station_icons_20`
