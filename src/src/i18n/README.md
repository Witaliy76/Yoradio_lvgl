Author: Witaliy76 - https://github.com/Witaliy76

# `src/src/i18n/` — technical reference

- Project overview: [`LOCALIZATION.md`](LOCALIZATION.md)
- Practical workflow: [`WORKFLOW.md`](WORKFLOW.md)

---

## Русский

### 1. Ответственность каталога

`src/src/i18n/` реализует статическую compile-time локализацию YoRadio:

- один selector `EN` / `RU` / `PL` / `SK`;
- один linked locale package;
- semantic `TextId` вместо string keys;
- O(1) lookup без map, heap и Arduino `String`;
- compile-time проверка catalog shape, placeholders, byte limits, metadata и calendar tables;
- public bounds-safe API со static lifetime результатов.

Runtime language state здесь отсутствует. Locale headers не зависят от LVGL и не определяют layout.

### 2. Структура

```text
i18n/
├── LOCALIZATION.md
├── README.md
├── WORKFLOW.md
├── language_codes.h
├── text_ids.h
├── locale_types.h
├── locale_select.h
├── i18n.h
├── i18n.cpp
└── locales/
    ├── en/
    │   ├── locale.h
    │   ├── strings.h
    │   └── calendar.h
    ├── ru/
    │   ├── locale.h
    │   ├── strings.h
    │   └── calendar.h
    ├── pl/
        ├── locale.h
        ├── strings.h
        └── calendar.h
    └── sk/
        ├── locale.h
        ├── strings.h
        └── calendar.h
```

### 3. Назначение core-файлов

| Файл | Назначение |
|---|---|
| `LOCALIZATION.md` | Обзор локализации для пользователя и разработчика проекта. |
| `README.md` | Техническая карта i18n subsystem и назначение файлов/API. |
| `WORKFLOW.md` | Практические процедуры изменения строк, добавления ID/языка и проверки. |
| `language_codes.h` | Preprocessor codes `EN=1`, `RU=2`, `PL=3`, `SK=4`; безопасен до подключения `myoptions.h`; locale data не содержит. |
| `text_ids.h` | `TextId`, `TextSpec`, printf-signature parser, supported-format self-tests и central ordered specification table. |
| `locale_types.h` | `TextEntry`, `LocaleMetadata`, `CalendarData`, table views и constexpr validators. |
| `locale_select.h` | Fail-closed compile-time dispatch: подключает ровно один `locales/<lang>/locale.h` и создаёт alias `selected_locale`. |
| `i18n.h` | Единственный public header для application consumers. |
| `i18n.cpp` | Единственный владелец выбранного пакета и реализация O(1)/bounds-safe accessors. Только этот `.cpp` включает `locale_select.h`. |

Удалённого compatibility bridge больше нет. Consumers не должны добавлять aliases, подключать locale package напрямую или восстанавливать forwarding `l10n.h`.

#### Политика отслеживания Markdown

Git отслеживает ровно `src/src/i18n/LOCALIZATION.md`, `src/src/i18n/README.md` и `src/src/i18n/WORKFLOW.md`. Остальные `src/src/i18n/**/*.md` остаются ignored; новые Markdown-файлы нельзя добавлять без отдельного явного решения. `.cursor/plans` остаётся ignored и не входит в эту policy.

### 4. Назначение locale-папки

Каждая `locales/en`, `locales/ru`, `locales/pl` и `locales/sk` содержит одинаковые роли:

| Файл | Назначение |
|---|---|
| `strings.h` | Ordered `std::array<TextEntry, textCount()> kStrings`; значения идут строго в порядке `TextId`. |
| `calendar.h` | Месяцы, полные/короткие weekdays, направления ветра и собранный `CalendarData`. |
| `locale.h` | Собирает strings/calendar, задаёт `LocaleMetadata`, запускает все `static_assert` validators. |

`locale.h` выбирается только через `locale_select.h`. Application code не включает `locales/*`.

### 5. `TextId`, `TextSpec` и catalogs

`TextId` — semantic identifier пользовательской фразы:

```cpp
enum class TextId : uint16_t {
  BootStarting,
  WeatherUpdatedMinutesAgoFormat,
  WifiActionConnect,
  // ...
  Count
};
```

`kTextSpecs` имеет ровно столько же элементов и в том же порядке. Каждый `TextSpec` задаёт:

```cpp
struct TextSpec {
  FormatSignature format;
  std::size_t maxBytes;
  bool allowEmpty;
};
```

- `format` — ordered printf argument signature;
- `maxBytes` — максимальный размер literal в bytes вместе с NUL;
- `allowEmpty` — разрешён ли пустой catalog value.

Каждый locale catalog содержит по одной `TextEntry` на каждый `TextId`:

```cpp
{TextId::WifiRemoveConfirmFormat, "Удалить \"%s\" навсегда?"},
```

Нельзя менять порядок только в одном locale. Добавление ID требует синхронного изменения enum, `kTextSpecs` и четырёх `kStrings`.

### 6. Calendar data

`CalendarData` содержит пять table views:

```cpp
struct CalendarData {
  StringTableView monthsFull;
  StringTableView monthsDate;
  StringTableView weekdaysFull;
  StringTableView weekdaysShort;
  StringTableView windDirections;
};
```

Текущий validated contract:

| Table | Count | Index semantics |
|---|---:|---|
| `monthsFull` | 0 | Не используется текущим UI |
| `monthsDate` | 12 | `0..11`, date-form months |
| `weekdaysFull` | 7 | `0..6`, Sunday first (`tm_wday`) |
| `weekdaysShort` | 7 | `0..6`, Sunday first (`tm_wday`) |
| `windDirections` | 17 | `0..16`, существующий weather direction mapping |

Consumer не индексирует arrays напрямую. Используются `dayFull`, `dayShort`, `monthName`, `windDirection`; invalid index возвращает stable empty string.

### 7. Locale metadata

```cpp
struct LocaleMetadata {
  const char* languageCode;
  const char* weatherApiLanguage;
};
```

Оба значения — двухбуквенные lowercase codes. Текущие packages используют `en/en`, `ru/ru`, `pl/pl`, `sk/sk`. `weatherApiLanguage` управляет locale запроса провайдера; единицы `metric` являются общей network policy и не хранятся в locale package.

### 8. Public typed API

Application consumers включают только:

```cpp
#include ".../i18n/i18n.h"
```

Public API:

```cpp
const char* text(TextId id) noexcept;
const LocaleMetadata& locale() noexcept;
const CalendarData& calendar() noexcept;
const char* dayFull(uint8_t index) noexcept;
const char* dayShort(uint8_t index) noexcept;
const char* monthName(uint8_t index) noexcept;
const char* windDirection(uint8_t index) noexcept;
```

Все возвращаемые данные принадлежат выбранному static package. Heap allocation и временные string objects не создаются. Invalid `TextId`, invalid table index или invalid table entry возвращают `""`, а не выполняют OOB access.

Для неизменённого catalog literal допустим `lv_label_set_text_static`. Форматированный stack buffer перед выходом из scope передаётся через copying API `lv_label_set_text`.

### 9. Selector и selected-locale-only linking

```text
language_codes.h
      │
      ▼
core/options.h → src/myoptions.h → strict validation
      │
      ▼
i18n.cpp → locale_select.h → one locales/<lang>/locale.h
```

`locale_select.h` содержит preprocessor dispatch и fail-closed `#error`. Поскольку только `i18n.cpp` подключает выбранный package, unselected `kStrings`, calendar arrays и metadata не компилируются в application translation units и не должны присутствовать в ELF.

### 10. Compile-time validation

Locale `static_assert` проверяет:

1. `TextId::Count`, `kTextSpecs.size()` и `kStrings.size()` совпадают;
2. `kStrings[i].id == static_cast<TextId>(i)`;
3. pointers не null, а запрещённые empty values отсутствуют;
4. UTF-8 literal вместе с NUL не превышает `maxBytes`;
5. каждый printf format принадлежит поддержанному subset;
6. ordered argument kinds и length modifiers совпадают с `TextSpec`;
7. metadata содержит валидные lowercase codes;
8. calendar table counts и entries валидны.

Поддерживаются integer/string/character conversions, numeric/dynamic width и precision, а также `hh`, `h`, `l`, `ll`, `z` для integer formats. Positional arguments и floating-point conversions fail closed.

Compile-time `maxBytes` не доказывает pixel fit. Runtime caller обязан использовать рассчитанный fixed buffer, проверить результат `snprintf`, а UI acceptance — проверить фактический font/layout на устройстве.

**SK DEVICE VISUAL ACCEPTANCE: PASS.** Историческая приёмка SK на `4848S040` / ST7701: каталог `125/125 TextId`, Weather code `sk`, SK glyphs `34/34`, сохранённые PL glyphs `18/18` и union `50/50` на тогдашней compiled-лестнице десяти размеров (12/14/16/18/20/22/28/32/40/48). Missing-glyph boxes, clipping/wrapping и runtime/navigation regressions не наблюдались; selector восстановлен в `RU`. Текущий factory TTF сохраняет тот же Unicode-контракт. Лингвистическая проверка словацкого текста носителями языка ожидается (`PENDING EXTERNAL REVIEW`).

---

## English

### 1. Directory responsibility

`src/src/i18n/` implements YoRadio's static compile-time localization:

- one `EN` / `RU` / `PL` / `SK` selector;
- exactly one linked locale package;
- semantic `TextId` values instead of string keys;
- O(1) lookup without maps, heap allocation, or Arduino `String`;
- compile-time validation of catalog shape, placeholders, byte limits, metadata, and calendar tables;
- a public bounds-safe API whose results have static lifetime.

There is no runtime language state. Locale headers do not depend on LVGL and do not own layout.

### 2. Layout

```text
i18n/
├── LOCALIZATION.md
├── README.md
├── WORKFLOW.md
├── language_codes.h
├── text_ids.h
├── locale_types.h
├── locale_select.h
├── i18n.h
├── i18n.cpp
└── locales/
    ├── en/{locale.h,strings.h,calendar.h}
    ├── ru/{locale.h,strings.h,calendar.h}
    ├── pl/{locale.h,strings.h,calendar.h}
    └── sk/{locale.h,strings.h,calendar.h}
```

### 3. Core files

| File | Responsibility |
|---|---|
| `LOCALIZATION.md` | Localization overview for project users and developers. |
| `README.md` | Technical map of the i18n subsystem and its files/API. |
| `WORKFLOW.md` | Practical procedures for changing strings, adding IDs/languages, and verification. |
| `language_codes.h` | Preprocessor codes `EN=1`, `RU=2`, `PL=3`, `SK=4`; safe before `myoptions.h`; owns no locale data. |
| `text_ids.h` | `TextId`, `TextSpec`, printf-signature parser, supported-format self-tests, and the central ordered specification table. |
| `locale_types.h` | `TextEntry`, `LocaleMetadata`, `CalendarData`, table views, and constexpr validators. |
| `locale_select.h` | Fail-closed compile-time dispatch that includes exactly one `locales/<lang>/locale.h` and defines `selected_locale`. |
| `i18n.h` | The only public header for application consumers. |
| `i18n.cpp` | Sole owner of the selected package and implementation of O(1)/bounds-safe accessors. The only `.cpp` that includes `locale_select.h`. |

The compatibility bridge has been removed. Consumers must not add aliases, include locale packages directly, or restore a forwarding `l10n.h`.

#### Markdown tracking policy

Git tracks exactly `src/src/i18n/LOCALIZATION.md`, `src/src/i18n/README.md`, and `src/src/i18n/WORKFLOW.md`. All other `src/src/i18n/**/*.md` files remain ignored; new Markdown files require a separate explicit decision. `.cursor/plans` remains ignored and is outside this policy.

### 4. Locale folders

Every `locales/en`, `locales/ru`, `locales/pl`, and `locales/sk` folder has the same roles:

| File | Responsibility |
|---|---|
| `strings.h` | Ordered `std::array<TextEntry, textCount()> kStrings`; entries strictly follow `TextId` order. |
| `calendar.h` | Months, full/short weekdays, wind directions, and the assembled `CalendarData`. |
| `locale.h` | Assembles strings/calendar, defines `LocaleMetadata`, and instantiates all `static_assert` validators. |

Only `locale_select.h` selects a `locale.h`. Application code never includes `locales/*`.

### 5. `TextId`, `TextSpec`, and catalogs

`TextId` is the semantic identifier for a user-visible phrase. The same-position entry in `kTextSpecs` defines its printf signature, maximum literal bytes including NUL, and empty-value policy.

Each locale catalog has one ordered `TextEntry` per ID:

```cpp
{TextId::WifiRemoveConfirmFormat, "Remove \"%s\" permanently?"},
```

Do not reorder only one locale. Adding an ID requires synchronized updates to the enum, `kTextSpecs`, and all four `kStrings` arrays.

### 6. Calendar data

The validated contract is:

| Table | Count | Index semantics |
|---|---:|---|
| `monthsFull` | 0 | Not consumed by the current UI |
| `monthsDate` | 12 | `0..11`, date-form month names |
| `weekdaysFull` | 7 | `0..6`, Sunday first (`tm_wday`) |
| `weekdaysShort` | 7 | `0..6`, Sunday first (`tm_wday`) |
| `windDirections` | 17 | `0..16`, existing weather direction mapping |

Consumers do not index locale arrays directly. They use `dayFull`, `dayShort`, `monthName`, or `windDirection`; an invalid index returns a stable empty string.

### 7. Locale metadata

`LocaleMetadata` stores `languageCode` and `weatherApiLanguage`. Both are validated two-letter lowercase codes. Current packages use `en/en`, `ru/ru`, `pl/pl`, and `sk/sk`. Shared `metric` weather units remain network policy, not locale data.

### 8. Public typed API

Application code includes only `i18n.h` and uses:

```cpp
const char* text(TextId id) noexcept;
const LocaleMetadata& locale() noexcept;
const CalendarData& calendar() noexcept;
const char* dayFull(uint8_t index) noexcept;
const char* dayShort(uint8_t index) noexcept;
const char* monthName(uint8_t index) noexcept;
const char* windDirection(uint8_t index) noexcept;
```

All results belong to the selected static package. No heap allocation or temporary string is created. Invalid IDs, indexes, or entries return `""` instead of reading out of bounds.

Use `lv_label_set_text_static` only for an unchanged catalog literal. Pass formatted stack buffers through the copying `lv_label_set_text` API before they leave scope.

### 9. Selector and selected-locale-only linking

```text
language_codes.h
      │
      ▼
core/options.h → src/myoptions.h → strict validation
      │
      ▼
i18n.cpp → locale_select.h → one locales/<lang>/locale.h
```

`locale_select.h` uses fail-closed preprocessor dispatch. Because only `i18n.cpp` includes the selected package, unselected strings, calendar arrays, and metadata must not appear in the final ELF.

### 10. Compile-time validation

Locale `static_assert` checks prove:

1. `TextId::Count`, spec count, and catalog count match;
2. every catalog row has the expected positional ID;
3. pointers and required non-empty values are valid;
4. UTF-8 literal bytes including NUL stay within `maxBytes`;
5. every printf format belongs to the supported subset;
6. ordered argument kinds and length modifiers match `TextSpec`;
7. metadata contains valid lowercase codes;
8. calendar counts and entries are valid.

The parser supports integer/string/character conversions, numeric/dynamic width and precision, plus `hh`, `h`, `l`, `ll`, and `z` integer modifiers. Positional arguments and floating-point conversions fail closed.

Compile-time byte validation does not prove pixel fit. The caller must use a justified fixed buffer, check `snprintf`, and validate the actual font/layout during device acceptance.

**SK DEVICE VISUAL ACCEPTANCE: PASS.** Historical SK acceptance on `4848S040` / ST7701: the `125/125 TextId` catalog, Weather code `sk`, SK glyphs `34/34`, retained PL glyphs `18/18`, and union `50/50` were recorded against the compiled ten-size ladder (12/14/16/18/20/22/28/32/40/48). No missing-glyph boxes, clipping/wrapping regressions, or runtime/navigation regressions were observed; the selector was restored to `RU`. The current factory TTF keeps that Unicode contract. Native Slovak linguistic review is pending (`PENDING EXTERNAL REVIEW`).
