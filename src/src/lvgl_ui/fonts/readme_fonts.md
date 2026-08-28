# Production fonts / Production-шрифты YoRadio

Author: Witaliy76 - https://github.com/Witaliy76

Normal firmware build embeds two ready TTF files and compiles one emergency
face. It does **not** run fontTools, subset fonts, or read full Montserrat/Tabler
sources.

Обычная сборка прошивки встраивает два готовых TTF и компилирует один аварийный
face. Она **не** запускает fontTools, не делает subset и не читает полные
источники Montserrat/Tabler.

Future optional user font (`L:/fonts/user.ttf`) is deferred. Intended policy:
absent → factory; valid → user; invalid → factory + warning; TinyTTF failure →
emergency 16.

Отложенный пользовательский шрифт (`L:/fonts/user.ttf`): нет файла → factory;
валидный → user; невалидный → factory + предупреждение; отказ TinyTTF → emergency 16.

---

## Factory text TTF / Заводской текстовый TTF

**English**

- File: `yoradio_factory_font.ttf`
- Role: embedded firmware factory text face; `FontProvider::text(px)` via TinyTTF
- Current source family: Montserrat Medium
- Production bytes: `28000`
- SHA256: `4474F1CBC068BBBA4496ACEF6142C9F8F7AD9741B66F7BBB3E74BE83E8EE62A8`
- Coverage: basic Latin, Cyrillic, degree/bullet/ellipsis, explicit Polish and
  Slovak UI letters, plus German/French letters and Western punctuation for
  station/artist/track metadata. Compile-time UI locales remain RU/EN/PL/SK;
  DE/FR locale packages are not implemented.
- Pixel sizes are **not** baked into the TTF. TinyTTF scales at runtime.
  Current 480×480 requests: 12, 14, 16, 18, 20, 22, 32, 40 (profile requests,
  not TTF limits).
- Normal build embeds this file unchanged (`board_build.embed_files`).

**Русский**

- Файл: `yoradio_factory_font.ttf`
- Роль: встроенный factory-текст; `FontProvider::text(px)` через TinyTTF
- Текущее семейство-источник: Montserrat Medium
- Размер: `28000` байт
- SHA256: `4474F1CBC068BBBA4496ACEF6142C9F8F7AD9741B66F7BBB3E74BE83E8EE62A8`
- Покрытие: базовая латиница, кириллица, degree/bullet/ellipsis, явные польские
  и словацкие буквы интерфейса, плюс немецкие/французские буквы и западная
  пунктуация для metadata станций. Локали UI — RU/EN/PL/SK; пакетов DE/FR нет.
- Размеры в пикселях **не** зашиты в TTF. TinyTTF масштабирует в runtime.
  Текущие запросы 480×480: 12, 14, 16, 18, 20, 22, 32, 40 (запросы профиля,
  не пределы TTF).
- Обычная сборка встраивает файл без изменений (`board_build.embed_files`).

---

## Tabler icon TTF / Иконочный TTF Tabler

**English**

- File: `yoradio_tabler.ttf`
- Role: embedded production Tabler subset; `FontProvider::icon(px)` via TinyTTF
- Provenance: `@tabler/icons-webfont` **3.26.0**
- Production subset bytes: `12772`
- SHA256: `F588E6ADA40FCF8DABA606061C90B492FDA64528B3687688F5580E68EA7CAB7C`
- cmap: **34** codepoints (glyph/codepoint subset, **not** pixel-size-specific)
- TinyTTF provides runtime sizes. Current 480×480 eager requests: 20, 22, 26,
  28, 36, 64; 24 px remains a supported lazy product request.

**Русский**

- Файл: `yoradio_tabler.ttf`
- Роль: встроенный production-subset Tabler; `FontProvider::icon(px)` через TinyTTF
- Происхождение: `@tabler/icons-webfont` **3.26.0**
- Размер subset: `12772` байт
- SHA256: `F588E6ADA40FCF8DABA606061C90B492FDA64528B3687688F5580E68EA7CAB7C`
- cmap: **34** codepoints (subset по глифам, **не** по размеру в пикселях)
- Размеры даёт TinyTTF. Текущие eager-запросы 480×480: 20, 22, 26, 28, 36, 64;
  24 px остаётся поддерживаемым lazy-запросом продукта.

### 34 production codepoints / 34 production-codepoints

| Codepoint | Name | Use |
|-----------|------|-----|
| U+EBA3 | wifi-0 | status Wi-Fi |
| U+EBA4 | wifi-1 | status Wi-Fi |
| U+EBA5 | wifi-2 | status Wi-Fi |
| U+EB52 | wifi | status / Wi-Fi header |
| U+ECFA | wifi-off | status Wi-Fi |
| U+ED48 | player-skip-back | Main controls |
| U+ED46 | player-play | Main controls |
| U+ED4A | player-stop | Main controls |
| U+ED49 | player-skip-forward | Main controls |
| U+EB6B | list | Main controls |
| U+EB20 | settings | Main controls |
| U+EA61 | chevron-right | Settings / chevron |
| U+EA60 | chevron-left | Main controls alternate |
| U+EB4F | volume-2 | Station current marker |
| U+EF4F | hand-click | Station hint |
| U+EA72 | cloud-rain | weather OWM |
| U+EA74 | cloud-storm | weather OWM |
| U+EA76 | cloud | weather OWM |
| U+EB30 | sun | weather OWM |
| U+EC0B | snowflake | weather OWM |
| U+ECD9 | cloud-fog | weather OWM |
| U+ECE7 | moon-stars | weather OWM |
| U+EFAA | haze | weather OWM |
| U+FAF8 | haze-moon | weather OWM |
| U+EC34 | wind | weather metric |
| U+FC12 | droplets | weather metric |
| U+EAB1 | gauge | weather metric |
| U+EBF1 | umbrella | weather metric / PoP |
| U+EA88 | database | Info rail |
| U+EF8E | cpu | Info rail |
| U+EA89 | device-desktop | Settings category |
| U+EB18 | router | Settings category |
| U+ECD4 | wave-sine | Settings category |
| U+F6D7 | sparkles | Settings category |

PUA strings live in product helper headers (`control_glyph_utf8.h`,
`weather_owm_glyph.h`, `settings_glyph_utf8.h`, …), not in compiled C fonts.

Строки PUA живут в helper-заголовках продукта, не в compiled C-шрифтах.

---

## Emergency compiled font / Аварийный compiled-шрифт

**English**

- Symbol/file: `lv_font_yora_montserrat_16_cyr` / `lv_font_yora_montserrat_16_cyr.c`
- Compiled 16 px multilingual hard-recovery face (same coverage family as the
  factory TTF: Latin + Cyrillic + PL/SK UI + DE/FR metadata letters)
- Used **only** when primary TinyTTF/backend cannot initialize
- Not a third normal backend. Icon init failure may use this face as a stable
  non-null placeholder (it has no Tabler PUA glyphs)

**Русский**

- Символ/файл: `lv_font_yora_montserrat_16_cyr` / `lv_font_yora_montserrat_16_cyr.c`
- Compiled 16 px многоязычный hard-recovery face (то же покрытие, что factory TTF)
- Используется **только** если primary TinyTTF/backend не поднимается
- Это не третий нормальный backend. При отказе icon-init этот face может остаться
  стабильной non-null заглушкой (Tabler PUA в нём нет)

---

## Rare authoring / update / Редкое обновление состава

**English**

If factory coverage or the Tabler 34-codepoint vocabulary must change later,
that is a **separate manual authoring operation**. Full Montserrat Medium /
Tabler 3.26.0 sources and fontTools are authoring inputs only. A historical
reference script may exist at `tools/fonts/font_source_build.py`; it is **not**
invoked by the ordinary firmware build.

After authoring, replace the ready files in this directory and rebuild. Do not
add a per-size compiled C ladder.

**Русский**

Если нужно изменить покрытие factory или словарь 34 codepoints Tabler, это
**отдельная ручная authoring-операция**. Полные Montserrat Medium / Tabler 3.26.0
и fontTools — только входы authoring. Исторический скрипт может лежать в
`tools/fonts/font_source_build.py`; обычная сборка прошивки его **не** вызывает.

После authoring замените готовые файлы в этом каталоге и пересоберите. Не
возвращайте per-size compiled C-лестницу.
