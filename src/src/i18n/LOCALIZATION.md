Author: Witaliy76 - https://github.com/Witaliy76

# YoRadio localization — project overview

This document is the user/developer entry point for the compile-time localization subsystem.

- Technical directory reference: [`README.md`](README.md)
- Change and verification procedures: [`WORKFLOW.md`](WORKFLOW.md)

---

## Русский

### 1. Поддерживаемые языки

YoRadio поддерживает четыре compile-time языка:

| Selector | Язык | Locale code | Weather API language |
|---|---|---|---|
| `EN` | English | `en` | `en` |
| `RU` | Русский | `ru` | `ru` |
| `PL` | Polski | `pl` | `pl` |
| `SK` | slovenčina | `sk` | `sk` |

В прошивку попадает ровно один выбранный пакет. Runtime-переключения языка, меню выбора языка и сохранённой настройки языка нет.

### 2. Где выбирается язык

Единственная пользовательская точка выбора — `src/myoptions.h`:

```cpp
#define L10N_LANGUAGE RU
```

Замените `RU` на `EN`, `PL` или `SK`. Не задавайте язык в `platformio.ini`, board-specific `myoptions_*.h`, NVS или SaveManager.

Если selector отсутствует, core использует guarded fallback `EN`. Любое явно заданное значение, отличное от `EN`, `RU`, `PL` или `SK`, останавливает сборку с `#error "Unsupported L10N_LANGUAGE"`.

### 3. Как собрать другой язык

После изменения только selector-строки выполните чистую сборку существующей среды:

```bash
pio run -e 4848S040 -t clean
pio run -e 4848S040
```

Отдельные PlatformIO environments или build flags для языков не нужны. Перед commit восстановите согласованный selector `RU`, выполните финальную чистую RU-сборку и убедитесь, что `src/myoptions.h` не имеет diff.

Полная RU/EN/PL/SK matrix и binary proof описаны в [`WORKFLOW.md`](WORKFLOW.md#10-ruenplsk-build-matrix).

### 4. Что локализуется

Compile-time каталоги владеют фиксированным пользовательским текстом и locale-зависимыми данными для принятого scope:

- player/boot status и системные overlay-сообщения;
- Boot, Wi-Fi Recovery и connection status;
- Weather UI, календарные формы и направления ветра;
- Station UI и application-owned fallback/error text;
- Preset Temporary UI и динамические feedback/countdown formats;
- Wi-Fi Home, Networks, Saved, Password и Hotspot UI;
- legacy Canvas weekday/month presentation через прямой typed API.

External values передаются как данные в проверенные format strings и не становятся частью каталога.

### 5. Что не локализуется

Текущая localization series намеренно не переводит:

- названия станций, SSID, IP-адреса и другие пользовательские/external values;
- текст погодных условий, уже локализованный провайдером;
- AI output, prompts и provider-owned ответы;
- Web UI, Telnet, serial/debug logs и технические diagnostics;
- Settings, Main, Info, Visual, Screensaver и другие экраны вне принятого scope;
- runtime language selector и persistent language setting.

Не переводите opaque provider/library errors механической заменой строк. Application-owned состояния должны получать отдельный `TextId`; external diagnostics остаются неизменными.

### 6. Общая схема подсистемы

```text
src/myoptions.h
  └─ L10N_LANGUAGE = EN | RU | PL | SK
             │
             ▼
core/options.h
  ├─ guarded EN fallback
  └─ strict selector validation
             │
             ▼
i18n.cpp → locale_select.h
             │
             ├─ locales/en/locale.h
             ├─ locales/ru/locale.h
             ├─ locales/pl/locale.h
             └─ locales/sk/locale.h
                  (выбирается ровно одна ветвь)
             │
             ▼
public i18n API → screens/core/display consumers
```

`i18n.cpp` — единственный translation unit, подключающий `locale_select.h`. Поэтому unselected locale packages не включаются в firmware. Public consumers подключают только `i18n.h` и используют `TextId` или bounds-safe calendar accessors.

### 7. Покрытие шрифтов

Польский и словацкий каталоги хранят правильный UTF-8 с диакритикой. Транслитерация запрещена. Общая custom LVGL font family содержит полный явный набор обоих языков:

```text
Ąą Ćć Ęę Łł Ńń Óó Śś Źź Żż
Áá Ää Čč Ďď Éé Íí Ĺĺ Ľľ Ňň Ôô Ŕŕ Šš Ťť Úú Ýý Žž
```

Все десять размеров shared family используют одинаковый Unicode contract; отдельные PL/SK fonts и locale-dependent routing отсутствуют. После изменения fonts нужно повторить PL/SK visual text-fit matrix и оценить Flash delta.

Buffer safety и font coverage — разные проверки:

- `TextSpec::maxBytes` проверяет UTF-8 bytes вместе с завершающим NUL;
- device smoke проверяет фактические glyphs, pixel width, wrapping и clipping.

**SK DEVICE VISUAL ACCEPTANCE: PASS.** На устройстве `4848S040` / ST7701 проверены словацкий каталог `125/125 TextId`, Weather provider language `sk`, покрытие SK `34/34`, сохранённое PL `18/18` и объединение PL+SK `50/50` во всех десяти shared font sizes (12/14/16/18/20/22/28/32/40/48). Missing-glyph boxes, clipping/wrapping regressions и runtime/navigation regressions не наблюдались; после smoke selector восстановлен в `RU`. Техническая приёмка рендеринга и layout пройдена. Лингвистическая проверка словацкого текста носителями языка ожидается (`PENDING EXTERNAL REVIEW`).

### 8. Куда идти дальше

- Структура файлов, public API и validators: [`README.md`](README.md)
- Изменение строк, новый `TextId`, новый язык и acceptance checks: [`WORKFLOW.md`](WORKFLOW.md)

---

## English

### 1. Supported languages

YoRadio supports four compile-time languages:

| Selector | Language | Locale code | Weather API language |
|---|---|---|---|
| `EN` | English | `en` | `en` |
| `RU` | Russian | `ru` | `ru` |
| `PL` | Polish | `pl` | `pl` |
| `SK` | Slovak | `sk` | `sk` |

Exactly one selected package is linked into the firmware. There is no runtime language switch, language menu, or persisted language setting.

### 2. Where to select a language

The sole user-facing selector is in `src/myoptions.h`:

```cpp
#define L10N_LANGUAGE RU
```

Replace `RU` with `EN`, `PL`, or `SK`. Do not define the language in `platformio.ini`, board-specific `myoptions_*.h` files, NVS, or SaveManager.

If the selector is absent, core uses the guarded `EN` fallback. Any explicit value other than `EN`, `RU`, `PL`, or `SK` stops compilation with `#error "Unsupported L10N_LANGUAGE"`.

### 3. Building another language

After changing only the selector line, clean and build the existing environment:

```bash
pio run -e 4848S040 -t clean
pio run -e 4848S040
```

Language-specific PlatformIO environments and build flags are not required. Before committing, restore the agreed `RU` selector, run a final clean RU build, and verify that `src/myoptions.h` has no diff.

The full RU/EN/PL/SK matrix and binary proof are documented in [`WORKFLOW.md`](WORKFLOW.md#10-ruenplsk-build-matrix).

### 4. What is localized

The compile-time catalogs own fixed user-visible text and locale-dependent data for the accepted scope:

- player/boot status and system overlays;
- Boot, Wi-Fi Recovery, and connection status;
- Weather UI, calendar forms, and wind directions;
- Station UI and application-owned fallback/error text;
- Preset Temporary UI and dynamic feedback/countdown formats;
- Wi-Fi Home, Networks, Saved, Password, and Hotspot UI;
- legacy Canvas weekday/month presentation through the direct typed API.

External values are passed as data to validated format strings and do not become catalog entries.

### 5. What is not localized

The current localization series intentionally excludes:

- station names, SSIDs, IP addresses, and other user/external values;
- weather condition text already localized by the provider;
- AI output, prompts, and provider-owned responses;
- Web UI, Telnet, serial/debug logs, and technical diagnostics;
- Settings, Main, Info, Visual, Screensaver, and other screens outside the accepted scope;
- a runtime selector or persistent language setting.

Do not mechanically translate opaque provider/library errors. Application-owned states need a dedicated `TextId`; external diagnostics remain unchanged.

### 6. Subsystem overview

```text
src/myoptions.h
  └─ L10N_LANGUAGE = EN | RU | PL | SK
             │
             ▼
core/options.h
  ├─ guarded EN fallback
  └─ strict selector validation
             │
             ▼
i18n.cpp → locale_select.h
             │
             ├─ locales/en/locale.h
             ├─ locales/ru/locale.h
             ├─ locales/pl/locale.h
             └─ locales/sk/locale.h
                  (exactly one branch is selected)
             │
             ▼
public i18n API → screens/core/display consumers
```

`i18n.cpp` is the only translation unit that includes `locale_select.h`, so unselected locale packages do not enter the firmware. Public consumers include only `i18n.h` and use `TextId` or bounds-safe calendar accessors.

### 7. Font coverage

The Polish and Slovak catalogs store correct UTF-8 with native diacritics. Transliteration is forbidden. The shared custom LVGL font family contains the complete explicit set for both languages:

```text
Ąą Ćć Ęę Łł Ńń Óó Śś Źź Żż
Áá Ää Čč Ďď Éé Íí Ĺĺ Ľľ Ňň Ôô Ŕŕ Šš Ťť Úú Ýý Žž
```

All ten shared-family sizes use the same Unicode contract; there are no separate PL/SK fonts or locale-dependent font routes. After font changes, repeat the PL/SK visual text-fit matrix and measure the Flash delta.

Buffer safety and font coverage are separate checks:

- `TextSpec::maxBytes` validates UTF-8 bytes including the terminating NUL;
- device smoke validates actual glyphs, pixel width, wrapping, and clipping.

**SK DEVICE VISUAL ACCEPTANCE: PASS.** On `4848S040` / ST7701, the Slovak catalog `125/125 TextId`, Weather provider language `sk`, SK coverage `34/34`, retained PL coverage `18/18`, and the PL+SK union `50/50` were checked across all ten shared font sizes (12/14/16/18/20/22/28/32/40/48). No missing-glyph boxes, clipping/wrapping regressions, or runtime/navigation regressions were observed; the selector was restored to `RU` after smoke. Technical rendering and layout acceptance passed. Native Slovak linguistic review is pending (`PENDING EXTERNAL REVIEW`).

### 8. Next references

- File layout, public API, and validators: [`README.md`](README.md)
- Editing strings, adding IDs/languages, and acceptance checks: [`WORKFLOW.md`](WORKFLOW.md)
