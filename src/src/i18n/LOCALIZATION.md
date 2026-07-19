# YoRadio localization — project overview

This document is the user/developer entry point for the compile-time localization subsystem.

- Technical directory reference: [`README.md`](README.md)
- Change and verification procedures: [`WORKFLOW.md`](WORKFLOW.md)

---

## Русский

### 1. Поддерживаемые языки

YoRadio поддерживает три compile-time языка:

| Selector | Язык | Locale code | Weather API language |
|---|---|---|---|
| `EN` | English | `en` | `en` |
| `RU` | Русский | `ru` | `ru` |
| `PL` | Polski | `pl` | `pl` |

В прошивку попадает ровно один выбранный пакет. Runtime-переключения языка, меню выбора языка и сохранённой настройки языка нет.

### 2. Где выбирается язык

Единственная пользовательская точка выбора — `src/myoptions.h`:

```cpp
#define L10N_LANGUAGE RU
```

Замените `RU` на `EN` или `PL`. Не задавайте язык в `platformio.ini`, board-specific `myoptions_*.h`, NVS или SaveManager.

Если selector отсутствует, core использует guarded fallback `EN`. Любое явно заданное значение, отличное от `EN`, `RU` или `PL`, останавливает сборку с `#error "Unsupported L10N_LANGUAGE"`.

### 3. Как собрать другой язык

После изменения только selector-строки выполните чистую сборку существующей среды:

```bash
pio run -e 4848S040 -t clean
pio run -e 4848S040
```

Отдельные PlatformIO environments или build flags для языков не нужны. Перед commit восстановите согласованный selector `RU`, выполните финальную чистую RU-сборку и убедитесь, что `src/myoptions.h` не имеет diff.

Полная RU/EN/PL matrix и binary proof описаны в [`WORKFLOW.md`](WORKFLOW.md#10-ruenpl-build-matrix).

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
  └─ L10N_LANGUAGE = EN | RU | PL
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
             └─ locales/pl/locale.h
                  (выбирается ровно одна ветвь)
             │
             ▼
public i18n API → screens/core/display consumers
```

`i18n.cpp` — единственный translation unit, подключающий `locale_select.h`. Поэтому unselected locale packages не включаются в firmware. Public consumers подключают только `i18n.h` и используют `TextId` или bounds-safe calendar accessors.

### 7. Ограничения шрифтов

Польский каталог хранит правильный UTF-8 с диакритикой. Транслитерация запрещена. Текущие custom LVGL fonts не гарантируют наличие всех польских символов, включая:

```text
Ąą Ćć Ęę Łł Ńń Óó Śś Źź Żż
```

Отсутствующий glyph может отображаться как placeholder/box. Это отдельная font-coverage задача: нельзя исправлять её искажением перевода. После изменения fonts нужно повторить PL visual/text-fit matrix и оценить Flash delta.

Buffer safety и font coverage — разные проверки:

- `TextSpec::maxBytes` проверяет UTF-8 bytes вместе с завершающим NUL;
- device smoke проверяет фактические glyphs, pixel width, wrapping и clipping.

### 8. Куда идти дальше

- Структура файлов, public API и validators: [`README.md`](README.md)
- Изменение строк, новый `TextId`, новый язык и acceptance checks: [`WORKFLOW.md`](WORKFLOW.md)

---

## English

### 1. Supported languages

YoRadio supports three compile-time languages:

| Selector | Language | Locale code | Weather API language |
|---|---|---|---|
| `EN` | English | `en` | `en` |
| `RU` | Russian | `ru` | `ru` |
| `PL` | Polish | `pl` | `pl` |

Exactly one selected package is linked into the firmware. There is no runtime language switch, language menu, or persisted language setting.

### 2. Where to select a language

The sole user-facing selector is in `src/myoptions.h`:

```cpp
#define L10N_LANGUAGE RU
```

Replace `RU` with `EN` or `PL`. Do not define the language in `platformio.ini`, board-specific `myoptions_*.h` files, NVS, or SaveManager.

If the selector is absent, core uses the guarded `EN` fallback. Any explicit value other than `EN`, `RU`, or `PL` stops compilation with `#error "Unsupported L10N_LANGUAGE"`.

### 3. Building another language

After changing only the selector line, clean and build the existing environment:

```bash
pio run -e 4848S040 -t clean
pio run -e 4848S040
```

Language-specific PlatformIO environments and build flags are not required. Before committing, restore the agreed `RU` selector, run a final clean RU build, and verify that `src/myoptions.h` has no diff.

The full RU/EN/PL matrix and binary proof are documented in [`WORKFLOW.md`](WORKFLOW.md#10-ruenpl-build-matrix).

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
  └─ L10N_LANGUAGE = EN | RU | PL
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
             └─ locales/pl/locale.h
                  (exactly one branch is selected)
             │
             ▼
public i18n API → screens/core/display consumers
```

`i18n.cpp` is the only translation unit that includes `locale_select.h`, so unselected locale packages do not enter the firmware. Public consumers include only `i18n.h` and use `TextId` or bounds-safe calendar accessors.

### 7. Font limitations

The Polish catalog stores correct UTF-8 with native diacritics. Transliteration is forbidden. The current custom LVGL fonts do not guarantee coverage for every Polish character, including:

```text
Ąą Ćć Ęę Łł Ńń Óó Śś Źź Żż
```

A missing glyph may appear as a placeholder box. This is a separate font-coverage task and must not be hidden by changing the translation. After font changes, repeat the PL visual/text-fit matrix and measure the Flash delta.

Buffer safety and font coverage are separate checks:

- `TextSpec::maxBytes` validates UTF-8 bytes including the terminating NUL;
- device smoke validates actual glyphs, pixel width, wrapping, and clipping.

### 8. Next references

- File layout, public API, and validators: [`README.md`](README.md)
- Editing strings, adding IDs/languages, and acceptance checks: [`WORKFLOW.md`](WORKFLOW.md)
