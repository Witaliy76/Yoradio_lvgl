# `lv_font_yora_wifi_flow_icons_24` / `_36` — Wi‑Fi Flow header (Tabler → LVGL)

**English:** Small Tabler-derived subset for the **Wi‑Fi Recovery / Networks** screen **header icon only** (one `lv_label` per header). Per-row icon labels were **rejected** (S6V11A-icons3): extra `lv_label` children per scan/saved row caused **LVGL heap OOM / WDT** on 48 KB `LV_MEM_SIZE`. Scan/saved rows stay **lightweight** `lv_list_add_btn` + Montserrat text (`Lock` / `Open` suffixes), no `LV_SYMBOL_*`.

**Русский:** Только иконка в заголовке; иконки в строках списка убраны из‑за давления на кучу LVGL.

## Kept sizes / files

| Profile width | File | LVGL symbol |
|---------------|------|---------------|
| ≤ 320 px | `lv_font_yora_wifi_flow_icons_24.c` | `lv_font_yora_wifi_flow_icons_24` |
| > 320 px (e.g. 480) | `lv_font_yora_wifi_flow_icons_36.c` | `lv_font_yora_wifi_flow_icons_36` |

Both fonts embed the **same** Tabler codepoint range (historical batch); runtime code uses **`wifi`** U+EB52 via `wifi_flow_glyph_utf8_wifi_full()`.

## Source & license

- **Pack:** [@tabler/icons-webfont](https://www.npmjs.com/package/@tabler/icons-webfont) **3.26.0**
- **TTF:** `dist/fonts/tabler-icons.ttf` (regenerate from your own checkout; do not commit `.fontwork` TTF here)

## Generate (24 and 36 only)

PowerShell from repo root; requires `\.fontwork\tabler-stripped.ttf` locally.

```powershell
$f = (Resolve-Path ".fontwork\tabler-stripped.ttf").Path
foreach ($sz in @(24, 36)) {
  $out = "src\src\lvgl_ui\fonts\lv_font_yora_wifi_flow_icons_$sz.c"
  npx --yes lv_font_conv@1.5.2 --font $f --size $sz --bpp 4 --format lvgl --no-compress `
    -o $out -r 0xEA61 -r 0xEA6B -r 0xEAE1-0xEAE2 -r 0xEB52 -r 0xEB6B -r 0xEBA3-0xEBA5 -r 0xECFA `
    --lv-include lvgl.h
}
```

## Wiring

`lv_conf.h`: `LV_FONT_YORA_WIFI_FLOW_ICONS_24` / `_36` = `1`, matching entries in `LV_FONT_CUSTOM_DECLARE`; `lv_fonts.h`: `extern` for both. `scr_wifi_flow.cpp` picks font via `LV_ACTIVE_PROFILE.width`.

## Follow-up (not in repo scope here)

- Grey RSSI / richer row chrome → needs **memory-safe** row layout (multi-part rows deferred, see S6V11B-stylemem notes).
- Smaller flash: regenerate fonts with **only** U+EB52 if tooling allows a single-glyph strip.
