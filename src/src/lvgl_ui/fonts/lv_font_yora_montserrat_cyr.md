# `lv_font_yora_montserrat_*_cyr` — shared Montserrat text family (Latin + Cyrillic + Polish)

**English:** YoRadio LVGL shared text font family — Montserrat Medium subset with basic Latin, Cyrillic, degree/bullet/ellipsis, and full Polish diacritics. One set of C symbols for RU/EN/PL; no locale-specific font assets or runtime language routing.
**Русский:** Общая текстовая семья шрифтов YoRadio LVGL — подмножество Montserrat Medium: базовая латиница, кириллица, degree/bullet/ellipsis и полный польский алфавит с диакритикой. Одни и те же C-символы для RU/EN/PL; без отдельных PL-assets и без runtime-выбора языка для шрифтов.

| Generated file | Size | LVGL symbol | Typical role (4848S040) |
|----------------|------|-------------|-------------------------|
| `lv_font_yora_montserrat_12_cyr.c` | 12 px | `lv_font_yora_montserrat_12_cyr` | Active localized (Weather captions, LOST status / `font_small`) |
| `lv_font_yora_montserrat_14_cyr.c` | 14 px | `lv_font_yora_montserrat_14_cyr` | Active localized (Boot / `font_header`, Weather small, Preset footer) |
| `lv_font_yora_montserrat_16_cyr.c` | 16 px | `lv_font_yora_montserrat_16_cyr` | Active localized (Wi‑Fi body/status, Weather condition/messages, Station footer) |
| `lv_font_yora_montserrat_18_cyr.c` | 18 px | `lv_font_yora_montserrat_18_cyr` | Active localized + Main meta (Station count, Wi‑Fi list, Main artist/clock) |
| `lv_font_yora_montserrat_20_cyr.c` | 20 px | `lv_font_yora_montserrat_20_cyr` | Active localized (Station / Preset / Wi‑Fi titles) |
| `lv_font_yora_montserrat_22_cyr.c` | 22 px | `lv_font_yora_montserrat_22_cyr` | Active localized (overlays / `font_large`, Station list, Preset names, Main track / player status) |
| `lv_font_yora_montserrat_28_cyr.c` | 28 px | `lv_font_yora_montserrat_28_cyr` | Family ladder; currently unused / GC’d if unreferenced |
| `lv_font_yora_montserrat_32_cyr.c` | 32 px | `lv_font_yora_montserrat_32_cyr` | External letter surface (Main station name); may contain Polish in SSID/station names |
| `lv_font_yora_montserrat_40_cyr.c` | 40 px | `lv_font_yora_montserrat_40_cyr` | Numeric Weather hero temperature (`+NN°C`); included for family cmap consistency |
| `lv_font_yora_montserrat_48_cyr.c` | 48 px | `lv_font_yora_montserrat_48_cyr` | Profile `font_clock` slot; currently unused / GC’d if unreferenced |

All ten sizes share the **same Unicode coverage**; only rasterization size differs.

---

## Русский

### Назначение семьи / Family purpose

- Общая текстовая семья YoRadio на базе **Montserrat Medium**.
- Покрытие: basic Latin + Cyrillic + полный польский набор + `°` / `•` / `…`.
- RU/EN/PL используют **одни и те же** `lv_font_yora_montserrat_*_cyr` symbols.
- Нет PL-specific `.c`, нет compile-time/runtime font routing по языку.
- Экраны и profile slots не меняются при добавлении польских глифов.

### Unicode contract

**Базовые ranges (сохранены):**

| Range / codepoint | Meaning |
|-------------------|---------|
| `U+0020`–`U+007F` | Basic Latin / printable ASCII |
| `U+0400`–`U+04FF` | Cyrillic block (generated subset as produced by `lv_font_conv`) |
| `U+00B0` | DEGREE SIGN `°` |
| `U+2022` | BULLET `•` |
| `U+2026` | HORIZONTAL ELLIPSIS `…` |

**Полный польский набор (добавлен во все размеры):**

```text
Ąą Ćć Ęę Łł Ńń Óó Śś Źź Żż
```

| Glyph | Codepoint |
|-------|-----------|
| Ą ą | U+0104 U+0105 |
| Ć ć | U+0106 U+0107 |
| Ę ę | U+0118 U+0119 |
| Ł ł | U+0141 U+0142 |
| Ń ń | U+0143 U+0144 |
| Ó ó | U+00D3 U+00F3 |
| Ś ś | U+015A U+015B |
| Ź ź | U+0179 U+017A |
| Ż ż | U+017B U+017C |

**Правило семьи:** все размеры этой shared family должны иметь **одинаковый Unicode coverage**. Различается только raster size. Не расширять отдельный размер вручную и не добавлять широкий `U+0100`–`U+017F` без доказанной необходимости.

### Generation / Генерация

- **Source TTF:** `tools/fonts/Montserrat-Medium.ttf` (local tooling tree; not a runtime asset).
- **Tool:** `npx lv_font_conv@1.5.2`
- **bpp:** `4`
- **Compression:** enabled (default RLE; matches `LV_USE_FONT_COMPRESSED 1` in `lv_conf.h`)
- **Format:** LVGL C (`--format lvgl --lv-include lvgl.h`)

**Do not edit generated `.c` by hand.** / **Не редактировать generated `.c` вручную.** Re-run the command for every size and replace the tracked file.

PowerShell (from repo root; ensure UTF-8 for `--symbols`):

```powershell
$ttf = (Resolve-Path "tools\fonts\Montserrat-Medium.ttf").Path
$sym = "ĄąĆćĘęŁłŃńÓóŚśŹźŻż"
$range = "0x20-0x7F,0x400-0x4FF,0xB0,0x2022,0x2026"
foreach ($sz in 12,14,16,18,20,22,28,32,40,48) {
  npx --yes lv_font_conv@1.5.2 `
    --font $ttf `
    -r $range `
    --symbols $sym `
    --size $sz `
    --bpp 4 `
    --format lvgl `
    --lv-include lvgl.h `
    -o "src\src\lvgl_ui\fonts\lv_font_yora_montserrat_${sz}_cyr.c"
}
```

C symbol name is taken from the `-o` basename (`lv_font_yora_montserrat_<N>_cyr`). Keep filenames and symbols stable.

### Architecture

- Existing C symbols preserved (`lv_fonts.h` / `LV_FONT_CUSTOM_DECLARE` unchanged).
- Existing font pointers and profile slots preserved.
- No PL-specific font assets.
- No runtime language selection for fonts.
- No screen routing changes for this font update.
- Built-in `lv_font_montserrat_*` and all icon fonts are out of scope.

### Coverage policy

| Class | Policy |
|-------|--------|
| Fixed RU/EN/PL UI catalog | Must render without missing-glyph boxes |
| Provider Weather condition text (`lang=pl`) | Full Polish alphabet required on display fonts (esp. 16 px condition line) |
| Station names / SSID / artist-title metadata | Best-effort; Polish alphabet covered; arbitrary Unicode outside this set is not guaranteed |
| Missing arbitrary Unicode | Does **not** justify expanding to all of Latin Extended or full Unicode |

### Verification (PLFONT-IMPLEMENTATION)

| Check | Result |
|-------|--------|
| Polish cmap `18/18` on all 10 sizes | PASS |
| Fixed catalog Polish glyphs `13/13` (+ bullet already in base) | PASS |
| Base ASCII / Cyrillic / `°` / `•` / `…` retained | PASS |
| Regenerated sizes | 12, 14, 16, 18, 20, 22, 28, 32, 40, 48 |
| Screen / locale / routing source changes | NONE |
| RU/EN/PL clean builds | PASS |
| Final clean RU build (after smoke) | PASS — RAM 81 512; Flash **2 884 759** |
| Measured Flash delta (linked fonts) | RU **+18 368** (2 866 391 → 2 884 759); PL **+18 368** (2 864 123 → 2 882 491) |
| Measured RAM delta | **0** |
| Partition | 4 194 304; final RU Flash 2 884 759 (68.8%); remaining **1 309 545** |
| Device visual smoke | **DEVICE VISUAL ACCEPTANCE: PASS** |

**Device acceptance (2026-07-19):** board `4848S040` / ST7701; smoke selector `PL`. All 18 Polish glyphs available; Boot / Recovery / Weather / Station / Preset / Wi‑Fi visually checked. Missing-glyph boxes not observed; no new clipping/wrapping or navigation/runtime regressions. After smoke, selector restored to `RU`. Final clean RU build PASS.

Рендеринг глифов, целостность текста и layout визуально приняты; лингвистическая проверка носителем польского языка в этот smoke **не входила**.

Do not treat `.c` source size as Flash. Linked contribution is what matters (`pio` size / `nm`; `--gc-sections`).

`28` and `48` remain GC’d / unreferenced after regeneration (not present in `firmware.elf` symbols); regenerating them keeps family cmap sync without current Flash cost.

Общие польские glyph bitmaps присутствуют в font assets независимо от `L10N_LANGUAGE`; это **не** означает линковку PL locale package в RU-сборке.

### Maintenance rule

When adding a new language or a new mandatory alphabet set:

1. Check **all** sizes of this family.
2. Regenerate the **entire** family with the same Unicode contract.
3. Do not hand-edit a single size.
4. Measure Flash / RAM (`pio` size) and update this Markdown.
5. Re-run PL (and RU/EN) visual text-fit smoke for affected screens.

---

## English

### Family purpose

Shared YoRadio Montserrat Medium text family used by LVGL UI. Latin + Cyrillic + full Polish diacritics live in the same generated assets. RU/EN/PL share the same symbols; language selection does not switch fonts.

### Unicode contract

Keep the base ranges listed above and the exact Polish set `ĄąĆćĘęŁłŃńÓóŚśŹźŻż` on **every** size. Do not add the whole `U+0100`–`U+017F` block without a measured need. All sizes must keep identical coverage; only pixel size changes.

### Generation

Use `tools/fonts/Montserrat-Medium.ttf` with `lv_font_conv@1.5.2`, `--bpp 4`, default compression, `--format lvgl`. Never hand-edit generated `.c` files — regenerate and replace.

### Architecture

Symbols, pointers, screens, and locale catalogs stay unchanged. No separate PL fonts and no language-dependent font routing.

### Coverage policy

Fixed UI and Polish provider Weather text must be covered. Station names / SSID / metadata remain best-effort beyond the defined Polish alphabet.

### Verification

See the RU verification table above for cmap, build and Flash/RAM numbers.

**DEVICE VISUAL ACCEPTANCE: PASS** (2026-07-19) on `4848S040` / ST7701 with smoke selector `PL`. Boot / Recovery / Weather / Station / Preset / Wi‑Fi checked; all 18 Polish glyphs available; missing-glyph boxes not observed; no new clipping/wrapping or navigation/runtime regressions. Selector restored to `RU` after smoke; final clean RU build PASS.

Glyph rendering, text integrity and layout were visually accepted; linguistic review by a native Polish speaker was not part of this smoke test.

Shared Polish glyph bitmaps exist in the font assets regardless of `L10N_LANGUAGE`; that does **not** mean the PL locale package is linked into a RU build.

### Maintenance

Always regenerate the full ten-size family together; measure Flash; update this document.
