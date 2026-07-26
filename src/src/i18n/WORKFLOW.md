Author: Witaliy76 - https://github.com/Witaliy76

# YoRadio localization — maintenance workflow

- Project overview: [`LOCALIZATION.md`](LOCALIZATION.md)
- Technical reference: [`README.md`](README.md)

---

## Русский

### 1. Общие правила

Перед любой правкой локализации:

1. проверьте branch, HEAD и `git status --short --branch`;
2. сохраните единственный selector в `src/myoptions.h`;
3. не меняйте `platformio.ini`, board-specific selectors, SaveManager или `CONFIG_VERSION`;
4. не подключайте `locales/*` из consumer code — только `i18n.h`;
5. не добавляйте runtime language state, maps, heap lookup или Arduino `String`;
6. не переводите external/user/provider data;
7. сохраняйте native UTF-8, включая польскую и словацкую диакритику.

Catalog changes должны оставаться синхронными для EN, RU, PL и SK даже если правится только один экран.

### 2. Изменение существующей фразы

1. Найдите semantic ID и все consumers:

   ```bash
   rg -n "TextId::WifiActionConnect" src/src
   ```

2. Измените значение этого ID в нужном `locales/<lang>/strings.h`.
3. Не меняйте позицию строки и `TextId`.
4. Если меняются placeholders или byte limit, это уже format/schema change — следуйте §4.
5. Проверьте UTF-8 bytes, natural wording и фактический widget contract: font, width, padding, long mode, alignment, line count.
6. Выполните clean build выбранного языка и device text-fit smoke.

Если смысл фразы различается между экранами, не сокращайте общий ID ценой другого consumer. Создайте отдельный semantic ID по §3.

### 3. Добавление новой фразы и нового `TextId`

В одном patch выполните весь ordered update:

1. Добавьте semantic value перед `TextId::Count` в `text_ids.h`.
2. Добавьте соответствующий `TextSpec` в `kTextSpecs` в той же позиции.
3. Добавьте `TextEntry` в той же позиции во всех трёх catalogs:

   ```text
   locales/en/strings.h
   locales/ru/strings.h
   locales/pl/strings.h
   locales/sk/strings.h
   ```

4. Используйте новый ID в consumer через public API:

   ```cpp
   #include "../../i18n/i18n.h"

   lv_label_set_text_static(label, i18n::text(i18n::TextId::ExampleTitle));
   ```

5. Удалите заменённый inline literal только в owning scope.
6. Выполните четыре clean builds: unselected catalogs намеренно не проверяются одной сборкой.

Пример non-formatted spec:

```cpp
makeTextSpec("", 48),  // ExampleTitle
```

`maxBytes` включает завершающий NUL и измеряет bytes, не glyph count.

### 4. Добавление или изменение format string

1. Определите точный ordered argument contract.
2. Запишите его в `TextSpec` через schema format:

   ```cpp
   makeTextSpec("%s %u", 96),  // ExampleFormat: name, count
   ```

3. Во всех locale entries сохраните те же argument kinds, order и length modifiers:

   ```cpp
   {TextId::ExampleFormat, "%s: %u"},
   ```

4. Не используйте positional formats (`%1$s`) или floating point (`%f`): текущий parser их отклоняет.
5. Поддержанный subset включает `%d/%i`, `%u/%o/%x/%X`, `%s`, `%c`, numeric/dynamic width и precision, а также integer modifiers `hh`, `h`, `l`, `ll`, `z`.
6. Форматируйте только в рассчитанный fixed buffer и проверяйте результат:

   ```cpp
   const int written = snprintf(buffer, sizeof(buffer),
                                i18n::text(i18n::TextId::ExampleFormat),
                                name, count);
   if (written < 0 || static_cast<std::size_t>(written) >= sizeof(buffer)) {
     // Publish a complete fallback; never show partial UTF-8.
   }
   ```

7. External data всегда является argument, никогда format string.
8. Проверьте maximum representative values и pixel fit на всех четырёх языках.

### 5. Работа с calendar data

Calendar values находятся в `locales/<lang>/calendar.h`.

Текущая индексная семантика:

```text
monthsDate:      0..11
weekdaysFull:    0..6, Sunday first
weekdaysShort:   0..6, Sunday first
windDirections: 0..16
monthsFull:      empty / no current consumer
```

Правила:

- сохраняйте порядок и количество элементов;
- используйте date-form months, уже принятые UI;
- не исправляйте product wording/index mapping в cleanup patch;
- consumer вызывает `monthName`, `dayFull`, `dayShort` или `windDirection`, а не индексирует locale arrays;
- при добавлении реально нового table contract обновите `CalendarData`, `kCalendarCountsExpected`, все locale packages и public accessor только если он нужен consumer;
- после правки выполните RU/EN/PL/SK builds и screen-specific date/weather smoke.

### 6. Изменение locale metadata

Metadata задаётся в `locales/<lang>/locale.h`:

```cpp
inline constexpr LocaleMetadata kMetadata{"pl", "pl"};
```

`languageCode` и `weatherApiLanguage` должны быть валидными двухбуквенными lowercase codes. Перед изменением Weather code проверьте контракт провайдера. Не переносите общую policy `metric` в locale package.

### 7. Добавление нового языка

1. Выберите новый symbolic code и добавьте его в `language_codes.h`.
2. Создайте одну папку:

   ```text
   locales/<language>/
   ├── locale.h
   ├── strings.h
   └── calendar.h
   ```

3. Скопируйте structure существующего package, затем переведите каждую entry без изменения порядка.
4. Заполните native calendar data и `LocaleMetadata`, включая `weatherApiLanguage`.
5. Добавьте одну branch в `locale_select.h` и включите новый code в strict validation `core/options.h`.
6. Не добавляйте board-specific define или PlatformIO environment.
7. Выберите новый code только в `src/myoptions.h`.
8. Проверьте placeholder signatures, UTF-8 byte limits, calendar counts и font glyph coverage.
9. Выполните clean build, selected-package binary proof и полную device text-fit matrix для нового языка.
10. Восстановите agreed default selector перед commit.

Словацкий пакет использует selector `L10N_LANGUAGE SK`, путь `locales/sk` и Weather provider code `sk`.

Добавление языка считается полным только когда его catalog содержит ровно `TextId::Count` entries и не требует fallback на другой package.

### 8. Placeholder и buffer validation

Compile-time validators проверяют catalog literal, но caller остаётся владельцем formatted output buffer.

Для каждого format call зафиксируйте:

| Проверка | Требование |
|---|---|
| Signature | Совпадает с central `TextSpec` во всех locale |
| Literal bytes | `cStringBytes(value) <= maxBytes`, включая NUL |
| Runtime capacity | Вмещает longest translated format + maximum arguments + NUL |
| Return value | `< 0` — failure; `>= capacity` — truncation |
| UTF-8 | Partial output никогда не публикуется |
| Lifetime | Stack buffer копируется widget/API до выхода из scope |
| Pixel fit | Проверяется фактическим font и widget geometry |

`sizeof`/byte count не заменяет visual measurement. Строка может помещаться в buffer, но не помещаться в кнопку или WRAP region.

### 9. Source checks перед build

```bash
git diff --check
rg -n "TextId::NewId" src/src/i18n src/src/lvgl_ui src/src/core
rg -n "i18n::locales::(en|ru|pl|sk)" src/src --glob '!src/src/i18n/**'
```

Последний поиск должен быть пуст: consumers не обращаются к locale internals.

Для bridge regression дополнительно проверяйте:

```bash
rg -n "legacy_compat|tools/l10n\.h|\bmnths\b" src
```

`config.theme.dow` — поле цвета темы, не legacy localization alias.

### 10. RU/EN/PL/SK build matrix

Для каждой строки matrix меняйте только `L10N_LANGUAGE` в `src/myoptions.h`:

```text
RU → clean → build → record evidence
EN → clean → build → record evidence
PL → clean → build → record evidence
SK → clean → build → record evidence
RU → restore → final clean build
```

Команды для каждой строки:

```bash
pio run -e 4848S040 -t clean
pio run -e 4848S040
```

Запишите:

```text
language
result
RAM bytes / percent
Flash bytes / percent
firmware.elf bytes
firmware.bin bytes
warnings
delta versus same-language accepted checkpoint
```

После matrix:

```bash
git diff -- src/myoptions.h
```

Вывод должен быть пуст, selector — `RU`.

### 11. Binary selected-package proof

Для каждого языка выберите существующую уникальную фразу, не добавляя test-only literals. Проверьте `firmware.bin` или ELF:

```bash
rg -a -F "Не удалось обновить пароль" .pio/build/4848S040/firmware.bin
rg -a -F "Could not update password" .pio/build/4848S040/firmware.bin
rg -a -F "Nie udało się zaktualizować hasła" .pio/build/4848S040/firmware.bin
rg -a -F "Heslo sa nepodarilo aktualizovať" .pio/build/4848S040/firmware.bin
```

В каждой build должна присутствовать только selected phrase.

Проверьте symbols через toolchain `nm`:

```bash
<toolchain>/xtensa-esp-elf-gcc-nm -C .pio/build/4848S040/firmware.elf
```

Ищите `i18n::locales::<selected>::`; namespaces двух других packages и legacy aliases должны отсутствовать. Не используйте общий substring `dow` как symbol proof — он может относиться к theme field или debug text.

### 12. Device smoke

Smoke формируется из реально изменённых consumers. Минимум:

- открыть затронутый экран/состояние;
- проверить RU/EN/PL/SK fixed text, maximum formatted sample и external data;
- проверить glyphs, clipping, wrapping, alignment и intended ellipsis;
- повторить navigation away/back и theme repaint, если они затронуты;
- проверить callbacks, timers и state transitions без изменения поведения;
- убедиться в отсутствии reboot, Guru Meditation, OOM и audio regression;
- сохранить concise observations и relevant logs.

Missing Polish/Slovak glyph — отдельный font defect. Он не разрешает transliteration и не скрывает buffer/layout/state regressions.

**SK DEVICE VISUAL ACCEPTANCE: PASS.** На `4848S040` / ST7701 проверены `125/125 TextId`, Weather mapping `sk`, SK `34/34`, сохранённое PL `18/18` и union `50/50` во всех десяти shared font sizes (12/14/16/18/20/22/28/32/40/48). Missing-glyph boxes, clipping/wrapping regressions и runtime/navigation regressions не наблюдались; selector после smoke восстановлен в `RU`. Техническая приёмка рендеринга и layout пройдена. Лингвистическая проверка словацкого текста носителями языка ожидается (`PENDING EXTERNAL REVIEW`).

### 13. Checklist перед commit

```text
[ ] Scope соответствует owning localization slice
[ ] Изменены только разрешённые files
[ ] EN/RU/PL/SK catalogs имеют одинаковый TextId order
[ ] TextSpec signatures и maxBytes обоснованы
[ ] Все snprintf results проверены
[ ] External data не переведены и не используются как format string
[ ] Direct locale-package access from consumers = 0
[ ] Legacy bridge refs/symbols = 0
[ ] RU/EN/PL/SK clean builds PASS
[ ] Selected-package binary proof PASS
[ ] Final selector RU; src/myoptions.h без diff
[ ] Device smoke принят для изменённых consumers
[ ] git diff --check PASS
[ ] git diff --cached --check PASS
[ ] Staging path-specific; unrelated/untracked files не staged
[ ] В Git входят только три approved i18n Markdown-файла; другие i18n Markdown остаются ignored
[ ] YOVERSION/CONFIG_VERSION изменены только при отдельном решении
[ ] Ignored implementation plan синхронизируется после acceptance
```

---

## English

### 1. General rules

Before any localization change:

1. record branch, HEAD, and `git status --short --branch`;
2. keep `src/myoptions.h` as the sole selector;
3. do not change `platformio.ini`, board selectors, SaveManager, or `CONFIG_VERSION`;
4. consumers include only `i18n.h`, never `locales/*`;
5. do not add runtime language state, maps, heap lookup, or Arduino `String`;
6. do not translate external/user/provider data;
7. preserve native UTF-8, including Polish and Slovak diacritics.

Catalog changes must stay synchronized across EN, RU, PL, and SK even when only one screen is being changed.

### 2. Editing an existing phrase

1. Find the semantic ID and all consumers:

   ```bash
   rg -n "TextId::WifiActionConnect" src/src
   ```

2. Edit that ID's value in the required `locales/<lang>/strings.h`.
3. Do not move the row or change its `TextId`.
4. If placeholders or byte limits change, follow the format/schema workflow in §4.
5. Check UTF-8 bytes, natural wording, and the widget contract: font, width, padding, long mode, alignment, and line count.
6. Run a clean selected-language build and device text-fit smoke.

If two screens need different wording, do not shorten a shared ID at the expense of another consumer. Add a separate semantic ID.

### 3. Adding a phrase and `TextId`

Perform the complete ordered update in one patch:

1. Add the semantic value before `TextId::Count` in `text_ids.h`.
2. Add its `TextSpec` at the same position in `kTextSpecs`.
3. Add a same-position `TextEntry` to EN, RU, PL, and SK `strings.h`.
4. Include `i18n.h` explicitly in the consumer and use `i18n::text(TextId::...)`.
5. Remove the replaced inline literal only within the owning scope.
6. Run four clean builds because an unselected catalog is intentionally not compiled.

For a plain label:

```cpp
makeTextSpec("", 48),  // ExampleTitle
```

`maxBytes` includes the terminating NUL and measures bytes, not glyphs.

### 4. Adding or changing a format string

1. Define the exact ordered argument contract.
2. Encode it in the central spec:

   ```cpp
   makeTextSpec("%s %u", 96),  // ExampleFormat: name, count
   ```

3. Preserve the same argument kinds, order, and length modifiers in every locale.
4. Do not use positional (`%1$s`) or floating-point (`%f`) formats; the parser rejects them.
5. The supported subset includes integer/string/character conversions, numeric/dynamic width and precision, and integer modifiers `hh`, `h`, `l`, `ll`, `z`.
6. Format into a justified fixed buffer and reject errors/truncation:

   ```cpp
   const int written = snprintf(buffer, sizeof(buffer),
                                i18n::text(i18n::TextId::ExampleFormat),
                                name, count);
   if (written < 0 || static_cast<std::size_t>(written) >= sizeof(buffer)) {
     // Publish a complete fallback; never show partial UTF-8.
   }
   ```

7. External data is always an argument, never the format string.
8. Test maximum representative values and pixel fit in all four languages.

### 5. Calendar data

Calendar values live in `locales/<lang>/calendar.h`. Preserve the current index contract:

```text
monthsDate:      0..11
weekdaysFull:    0..6, Sunday first
weekdaysShort:   0..6, Sunday first
windDirections: 0..16
monthsFull:      empty / no current consumer
```

Keep ordering/counts stable, preserve accepted date forms, and use public accessors instead of direct locale-array indexing. A new table contract requires synchronized type/count/package updates and a public accessor only when a real consumer needs it.

After calendar edits, run RU/EN/PL/SK builds and the affected date/weather smoke.

### 6. Locale metadata

Metadata is defined in `locales/<lang>/locale.h`:

```cpp
inline constexpr LocaleMetadata kMetadata{"pl", "pl"};
```

Both fields must be valid two-letter lowercase codes. Verify the provider contract before changing `weatherApiLanguage`. Keep shared `metric` units outside locale packages.

### 7. Adding a language

1. Add one symbolic code to `language_codes.h`.
2. Create `locales/<language>/{locale.h,strings.h,calendar.h}`.
3. Copy an existing package structure and translate every ordered entry.
4. Add native calendar data and metadata, including `weatherApiLanguage`.
5. Add one `locale_select.h` branch and extend strict validation in `core/options.h`.
6. Do not add a board-specific define or PlatformIO environment.
7. Select the language only in `src/myoptions.h`.
8. Validate signatures, byte limits, counts, and font glyph coverage.
9. Run a clean build, selected-package proof, and full device text-fit matrix.
10. Restore the agreed default selector before committing.

The Slovak package uses selector `L10N_LANGUAGE SK`, path `locales/sk`, and Weather provider code `sk`.

A language is complete only when it owns exactly `TextId::Count` entries without falling back to another package.

### 8. Placeholder and buffer validation

Compile-time validators cover catalog literals; the caller still owns its formatted output buffer.

| Check | Requirement |
|---|---|
| Signature | Matches central `TextSpec` in every locale |
| Literal bytes | Includes terminating NUL and stays within `maxBytes` |
| Runtime capacity | Fits longest translation, maximum arguments, and NUL |
| Return value | `< 0` is failure; `>= capacity` is truncation |
| UTF-8 | Never publish partial output |
| Lifetime | Copy a stack buffer before it leaves scope |
| Pixel fit | Validate actual font and widget geometry |

Byte count does not replace visual measurement.

### 9. Source checks before building

```bash
git diff --check
rg -n "TextId::NewId" src/src/i18n src/src/lvgl_ui src/src/core
rg -n "i18n::locales::(en|ru|pl|sk)" src/src --glob '!src/src/i18n/**'
rg -n "legacy_compat|tools/l10n\.h|\bmnths\b" src
```

Direct locale-package consumer access and active legacy bridge matches must remain zero. `config.theme.dow` is a theme-color field, not a localization alias.

### 10. RU/EN/PL/SK build matrix

Change only `L10N_LANGUAGE` in `src/myoptions.h`:

```text
RU → clean/build/evidence
EN → clean/build/evidence
PL → clean/build/evidence
SK → clean/build/evidence
RU → restore/final clean build
```

For each row:

```bash
pio run -e 4848S040 -t clean
pio run -e 4848S040
```

Record result, RAM, Flash, ELF, BIN, warnings, and same-language delta. At the end, `git diff -- src/myoptions.h` must be empty and the selector must be `RU`.

### 11. Binary selected-package proof

Use existing unique phrases; do not add test-only UI strings:

```bash
rg -a -F "Не удалось обновить пароль" .pio/build/4848S040/firmware.bin
rg -a -F "Could not update password" .pio/build/4848S040/firmware.bin
rg -a -F "Nie udało się zaktualizować hasła" .pio/build/4848S040/firmware.bin
rg -a -F "Heslo sa nepodarilo aktualizovať" .pio/build/4848S040/firmware.bin
```

Only the selected phrase should be present in each build. Then inspect symbols:

```bash
<toolchain>/xtensa-esp-elf-gcc-nm -C .pio/build/4848S040/firmware.elf
```

The selected locale namespace must be present; the other locale namespaces and legacy aliases must be absent.

### 12. Device smoke

Derive smoke coverage from the actual consumers changed:

- open every affected screen/state;
- check fixed text, maximum formatted samples, and external data;
- verify glyphs, clipping, wrapping, alignment, and intended ellipsis;
- repeat navigation and theme repaint where relevant;
- verify callbacks, timers, and state transitions remain unchanged;
- confirm no reboot, Guru Meditation, OOM, or audio regression;
- record concise observations and relevant logs.

A missing Polish/Slovak glyph is a separate font defect. It does not permit transliteration or hide buffer/layout/state regressions.

**SK DEVICE VISUAL ACCEPTANCE: PASS.** On `4848S040` / ST7701, `125/125 TextId`, Weather mapping `sk`, SK `34/34`, retained PL `18/18`, and union `50/50` passed across all ten shared font sizes (12/14/16/18/20/22/28/32/40/48). No missing-glyph boxes, clipping/wrapping regressions, or runtime/navigation regressions were observed; the selector was restored to `RU` after smoke. Technical rendering and layout acceptance passed. Native Slovak linguistic review is pending (`PENDING EXTERNAL REVIEW`).

### 13. Pre-commit checklist

```text
[ ] Patch stays inside the owning localization scope
[ ] Only explicitly allowed files changed
[ ] EN/RU/PL/SK catalogs have identical TextId order
[ ] TextSpec signatures and maxBytes are justified
[ ] Every snprintf result is checked
[ ] External data remains untranslated and is never a format string
[ ] Direct locale-package consumer access = 0
[ ] Legacy bridge refs/symbols = 0
[ ] RU/EN/PL/SK clean builds PASS
[ ] Selected-package binary proof PASS
[ ] Final selector RU; no src/myoptions.h diff
[ ] Device smoke accepted for changed consumers
[ ] git diff --check PASS
[ ] git diff --cached --check PASS
[ ] Staging is path-specific; unrelated/untracked files are not staged
[ ] Only the three approved i18n Markdown files are tracked; all other i18n Markdown remains ignored
[ ] YOVERSION/CONFIG_VERSION changed only by an explicit decision
[ ] Ignored implementation plan synchronized after acceptance
```
