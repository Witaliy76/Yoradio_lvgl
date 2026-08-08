# `lv_font_yora_montserrat_*_cyr` — shared Montserrat text family (UI locales + Western metadata)

**English:** YoRadio LVGL shared text font family — Montserrat Medium subset with basic Latin, Cyrillic, degree/bullet/ellipsis, complete explicit Polish + Slovak UI coverage, and German/French letters plus Western punctuation for external station/artist/track metadata. One C-symbol family is shared by RU/EN/PL/SK; German and French UI locales are **not** implemented.
**Русский:** Общая текстовая семья шрифтов YoRadio LVGL — подмножество Montserrat Medium: базовая латиница, кириллица, degree/bullet/ellipsis, полные явные польский + словацкий наборы интерфейса, а также немецкие/французские буквы и западноевропейская типографика для внешних station/artist/track metadata. Одна семья C-символов используется RU/EN/PL/SK; немецкая и французская локализации интерфейса **не** реализованы.

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
- **Compile-time UI locale coverage:** RU, EN, PL, SK; RU/EN/PL/SK используют одни и те же `lv_font_yora_montserrat_*_cyr` symbols.
- **External metadata coverage:** немецкие и французские названия станций, исполнители, треки, Icecast/Shoutcast metadata и Western European punctuation.
- Покрытие: basic Latin + Cyrillic + полные явные PL/SK/DE/FR letter sets + metadata typography + `°` / `•` / `…`.
- DE/FR glyphs включены для station names, artists и track metadata; `L10N_LANGUAGE DE/FR` и locale packages DE/FR отсутствуют.
- Нет language-specific `.c`, compile-time/runtime font routing или fallback chain; экраны и profile slots не меняются.

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

**Полный словацкий набор (34 символа / 17 пар, во всех размерах):**

```text
Áá Ää Čč Ďď Éé Íí Ĺĺ Ľľ Ňň Óó Ôô Ŕŕ Šš Ťť Úú Ýý Žž
```

```text
U+00C1 U+00E1 U+00C4 U+00E4 U+010C U+010D U+010E U+010F
U+00C9 U+00E9 U+00CD U+00ED U+0139 U+013A U+013D U+013E
U+0147 U+0148 U+00D3 U+00F3 U+00D4 U+00F4 U+0154 U+0155
U+0160 U+0161 U+0164 U+0165 U+00DA U+00FA U+00DD U+00FD
U+017D U+017E
```

**Полный deduplicated PL+SK union (50 codepoints):**

```text
ĄąĆćĘęŁłŃńÓóŚśŹźŻżÁáÄäČčĎďÉéÍíĹĺĽľŇňÔôŔŕŠšŤťÚúÝýŽž
```

**External metadata letters:**

```text
German 8: ÄäÖöÜüẞß
French 32: ÀàÂâÆæÇçÉéÈèÊêËëÎîÏïÔôŒœÙùÛûÜüŸÿ
```

Overlap DE/FR с существующим PL+SK: `ÄäÉéÔô` (`6`). Новых буквенных codepoints: `32`. Полный deduplicated PL+SK+DE+FR union: **82/82**.

**External metadata typography (10 codepoints):**

```text
U+00A0 NBSP   U+00AB «   U+00BB »   U+2013 –   U+2014 —
U+2018 ‘      U+2019 ’   U+201C “   U+201D ”   U+201E „
```

Примеры покрываемого metadata: `München`, `Straße`, `Groß`, `Été`, `Cœur`, `François`, `L’amour`, `«Musique»`, `Künstler — Titel`.

**Правило семьи:** все размеры этой shared family должны иметь **одинаковый Unicode coverage**. Различается только raster size. Не расширять отдельный размер вручную и не добавлять широкий `U+0100`–`U+017F` без доказанной необходимости.

### Generation / Генерация

- **Source TTF:** `tools/fonts/Montserrat-Medium.ttf` (local tooling tree; not a runtime asset).
- **Tool:** `npx lv_font_conv@1.5.3` (required for the current LVGL 9 ABI)
- **bpp:** `4`
- **Compression:** enabled (default RLE; matches `LV_USE_FONT_COMPRESSED 1` in `lv_conf.h`)
- **Format:** LVGL C (`--format lvgl --lv-include lvgl.h`)

**Do not edit generated `.c` by hand.** / **Не редактировать generated `.c` вручную.** Re-run the command for every size and replace the tracked file.

PowerShell (from repo root; ensure UTF-8 for `--symbols`):

```powershell
$ttf = (Resolve-Path "tools\fonts\Montserrat-Medium.ttf").Path
$range = "0x20-0x7F,0x400-0x4FF,0xB0,0x2022,0x2026,<explicit PL+SK+DE+FR codepoints>,0xA0,0xAB,0xBB,0x2013-0x2014,0x2018-0x2019,0x201C-0x201E"
foreach ($sz in 12,14,16,18,20,22,28,32,40,48) {
  npx --yes lv_font_conv@1.5.3 `
    --font $ttf `
    -r $range `
    --size $sz `
    --bpp 4 `
    --format lvgl `
    --lv-include lvgl.h `
    -o "src\src\lvgl_ui\fonts\lv_font_yora_montserrat_${sz}_cyr.c"
}
```

Передавайте non-ASCII letter contract как ASCII-only explicit codepoints в `-r`, чтобы PowerShell encoding не мог повредить Unicode. Не объединяйте его в широкие Latin-1 / Latin Extended ranges. Разбиение `0x2018-0x2019,0x201C-0x201E` намеренно исключает незапрошенные U+201A/U+201B и сохраняет точный typography set `10/10`.

C symbol name is taken from the `-o` basename (`lv_font_yora_montserrat_<N>_cyr`). Keep filenames and symbols stable.

All ten files were regenerated as part of the LVGL 9 migration. `lv_font_conv@1.5.2`
is not valid for that regeneration because its version guard can keep the removed v8
`.cache` field when compiling against LVGL 9. Version 1.5.3 excludes that field and emits
the valid v9 `fallback` member; the current generated family keeps it `NULL`.

### Architecture

- Existing C symbols preserved (`lv_fonts.h` / `LV_FONT_CUSTOM_DECLARE` unchanged).
- Existing font pointers and profile slots preserved.
- No PL/SK/DE/FR-specific font assets.
- No runtime language selection for fonts.
- No screen routing changes for this font update.
- Built-in `lv_font_montserrat_*` and all icon fonts are out of scope.

### Coverage policy

| Class | Policy |
|-------|--------|
| Fixed RU/EN/PL/SK UI catalog | Must render without missing-glyph boxes |
| Provider Weather condition text (`lang=pl` / `lang=sk`) | Full explicit Polish + Slovak sets required on display fonts (esp. 16 px condition line) |
| Station names / artist-title / Icecast/Shoutcast metadata | Explicit PL + SK + German + French letters and Western punctuation are covered |
| German/French UI catalogs | **Not implemented**; glyph coverage does not create DE/FR interface localization |
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

### Verification (SK implementation — historical pre-LVGL9 record)

| Check | Result |
|-------|--------|
| Source TTF | `tools/fonts/Montserrat-Medium.ttf` |
| `lv_font_conv` | **1.5.2** |
| Slovak explicit set in source TTF | **34/34 PASS** |
| Slovak-only additions beyond existing `Óó` | **32/32 PASS** |
| Slovak cmap on all 10 generated sizes | **34/34 PASS** |
| Polish cmap retained on all 10 sizes | **18/18 PASS** |
| Deduplicated PL+SK union | **50/50 PASS** |
| Base ASCII / Cyrillic / `°` / `•` / `…` retained | **PASS** |
| Regenerated sizes | 12, 14, 16, 18, 20, 22, 28, 32, 40, 48 |
| Filenames / C symbols / bpp / compression / fallback | **UNCHANGED** |
| Generated C source-size delta | **+181,568 B** total (not a Flash measurement) |
| SK device status | **DEVICE VISUAL ACCEPTANCE: PASS** (`4848S040` / ST7701) |
| Native Slovak linguistic review | **PENDING EXTERNAL REVIEW** |

Shared Slovak glyph bitmaps exist in font assets for every `L10N_LANGUAGE`; this does **not** mean the SK locale package is linked into other language builds.

На `4848S040` / ST7701 технически приняты словацкий каталог `125/125 TextId`, Weather mapping `sk`, SK `34/34`, сохранённое PL `18/18` и union `50/50` во всех десяти shared sizes (12/14/16/18/20/22/28/32/40/48). Missing-glyph boxes, clipping/wrapping regressions и runtime/navigation regressions не наблюдались; selector после smoke восстановлен в `RU`. Техническая приёмка рендеринга и layout пройдена. Лингвистическая проверка словацкого текста носителями языка ожидается (`PENDING EXTERNAL REVIEW`).

### Verification (Western European metadata implementation)

| Check | Result |
|-------|--------|
| Source TTF DE / FR / typography | **8/8 / 32/32 / 10/10 PASS** |
| PL retained / SK retained | **18/18 / 34/34 PASS** |
| Deduplicated PL+SK+DE+FR union | **82/82 PASS** |
| Metadata typography | **10/10 PASS** |
| Base ASCII / Cyrillic / `°` / `•` / `…` retained | **PASS** |
| Regenerated shared sizes | 12, 14, 16, 18, 20, 22, 28, 32, 40, 48 |
| Filenames / C symbols / bpp / compression / fallback | **UNCHANGED** |
| DE/FR UI locales or locale packages | **NOT ADDED** |
| Device status | **WESTERN EUROPEAN METADATA VISUAL ACCEPTANCE: PASS** (`4848S040` / ST7701) |

На `4848S040` / ST7701 с selector `RU` визуально приняты German `8/8`, French `32/32` и metadata typography `10/10`; сохранены PL `18/18`, SK `34/34`, letter union `82/82` и одинаковый cmap всех десяти shared sizes (12/14/16/18/20/22/28/32/40/48). Немецкие и французские station/track metadata отображались корректно; missing-glyph boxes, clipping и scrolling regressions не наблюдались. Audio, navigation и metadata updates оставались стабильными. Редкие `ẞ`, NBSP и отдельные quotation marks приняты по static cmap proof.

Это font-only расширение: немецкие и французские glyphs предоставлены только для внешних station, artist и track metadata. DE/FR UI locales не реализованы; selector остаётся `RU`, locale catalogs, Weather mapping, screens и layout не меняются.

### Maintenance rule

When adding a new language or a new mandatory alphabet set:

1. Check **all** sizes of this family.
2. Regenerate the **entire** family with the same Unicode contract.
3. Do not hand-edit a single size.
4. Measure Flash / RAM (`pio` size) and update this Markdown.
5. Re-run PL/SK (and RU/EN) visual text-fit smoke for affected screens.

---

## English

### Family purpose

Shared YoRadio Montserrat Medium text family used by LVGL UI. **Compile-time UI locale coverage** is RU/EN/PL/SK. **External metadata coverage** adds explicit German/French letters and Western punctuation for station names, artists, track titles, and Icecast/Shoutcast metadata. German and French UI locales are **not implemented**. All coverage lives in the same generated assets; language selection does not switch fonts.

### Unicode contract

Keep the base ranges listed above, exact Polish `18/18`, Slovak `34/34`, German `8/8`, French `32/32`, and metadata typography `10/10` on **every** size. The deduplicated PL+SK+DE+FR letter union is `82/82`; the German/French overlap with PL+SK is six codepoints. Preserve bullet and ellipsis. Do not add broad Latin-1 or `U+0100`–`U+017F` ranges. All sizes must keep identical coverage; only pixel size changes.

Metadata examples: `München`, `Straße`, `Groß`, `Été`, `Cœur`, `François`, `L’amour`, `«Musique»`, and `Künstler — Titel`.

### Generation

Use `tools/fonts/Montserrat-Medium.ttf` with `lv_font_conv@1.5.3`, `--bpp 4`, default compression, `--format lvgl`. Version 1.5.3 is required for the current LVGL 9 font ABI; 1.5.2 can retain the removed v8 `.cache` member. Pass non-ASCII coverage as ASCII-only explicit codepoints, never as broad Latin ranges. Never hand-edit generated `.c` files — regenerate and replace the full ten-size family.

### Architecture

Symbols, pointers, screens, and locale catalogs stay unchanged. No separate PL/SK/DE/FR fonts, fallback chain, or language-dependent font routing. DE/FR glyphs do not imply DE/FR locale packages.

### Coverage policy

Fixed RU/EN/PL/SK UI and Polish/Slovak provider Weather text remain covered. External station/artist/track metadata is guaranteed for the explicit German/French sets and ten punctuation codepoints; arbitrary Unicode outside the contract remains best-effort.

### Verification

See the RU verification table above for cmap, build and Flash/RAM numbers.

**DEVICE VISUAL ACCEPTANCE: PASS** (2026-07-19) on `4848S040` / ST7701 with smoke selector `PL`. Boot / Recovery / Weather / Station / Preset / Wi‑Fi checked; all 18 Polish glyphs available; missing-glyph boxes not observed; no new clipping/wrapping or navigation/runtime regressions. Selector restored to `RU` after smoke; final clean RU build PASS.

Glyph rendering, text integrity and layout were visually accepted; linguistic review by a native Polish speaker was not part of this smoke test.

Shared Polish glyph bitmaps exist in the font assets regardless of `L10N_LANGUAGE`; that does **not** mean the PL locale package is linked into a RU build.

For the SK implementation, all ten sizes (12/14/16/18/20/22/28/32/40/48) statically retain Polish `18/18`, add Slovak `34/34`, and contain the exact deduplicated PL+SK union `50/50`. The source TTF covers every required codepoint. **SK DEVICE VISUAL ACCEPTANCE: PASS** on `4848S040` / ST7701 for the `125/125 TextId` catalog and Weather mapping `sk`; no missing-glyph boxes, clipping/wrapping regressions, or runtime/navigation regressions were observed. The selector was restored to `RU` after smoke. Technical rendering and layout acceptance passed. Native Slovak linguistic review is pending (`PENDING EXTERNAL REVIEW`).

For the Western metadata extension, all ten sizes (12/14/16/18/20/22/28/32/40/48) statically retain PL `18/18` and SK `34/34`, add DE `8/8`, FR `32/32`, the exact letter union `82/82`, and metadata typography `10/10`. Base cmaps, symbols, bpp, compression, and implicit NULL fallback are retained. **WESTERN EUROPEAN METADATA VISUAL ACCEPTANCE: PASS** on `4848S040` / ST7701 with selector `RU`. German and French station/track metadata rendered correctly; missing-glyph boxes and clipping/scrolling regressions were not observed. Audio, navigation, and metadata updates remained stable. Rare `ẞ`, NBSP, and individual quotation marks were accepted through static cmap proof.

German and French glyphs are provided for external station, artist and track metadata only. German and French UI locales were not added.

### Maintenance

Always regenerate the full ten-size family together with the exact PL+SK+DE+FR and metadata-typography contract; measure Flash; update this document.
