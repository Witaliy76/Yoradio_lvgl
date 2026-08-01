## Русская часть

# AI Layer — каталог prompt-файлов

Prompt задаёт поведение AI Layer: формат JSON-ответа, выбор `fact` / `listen`, пороги `confidence` и язык короткой строки на экране Main.

AI Layer может быть полностью отключён. Без загруженного prompt устройство молчит — это нормальное состояние, не ошибка.

## Поддерживаемые языки

| Файл | Язык ответа |
|------|-------------|
| [`ai_prompt_ru.txt`](ai_prompt_ru.txt) | русский (RU) |
| [`ai_prompt_en.txt`](ai_prompt_en.txt) | английский (EN) |
| [`ai_prompt_pl.txt`](ai_prompt_pl.txt) | польский (PL) |
| [`ai_prompt_sk.txt`](ai_prompt_sk.txt) | словацкий (SK) |
| [`ai_prompt.txt`](ai_prompt.txt) | копия активного шаблона по умолчанию (сейчас = RU) |

Язык интерфейса прошивки (`L10N_LANGUAGE` в `src/myoptions.h`) **не** выбирает prompt автоматически. Язык AI-вывода задаётся только текстом загруженного prompt-файла.

## Как выбирается prompt

На устройстве runtime всегда читает один файл LittleFS:

```text
/ai/ai_prompt.txt
```

Способы установки:

1. **Прошивка FS** — положите нужный шаблон в `data/ai/ai_prompt.txt` (скопируйте один из файлов этого каталога) и выполните `uploadfs`.
2. **Web UI** — Settings → AI → **Upload Prompt File**. Имя загружаемого `.txt` игнорируется; файл сохраняется как `/ai/ai_prompt.txt`.

Новый prompt заменяет предыдущий. Fallback-текстов нет: нет файла → AI Layer молчит.

## Формат и правила

Все языковые варианты сохраняют один и тот же технический контракт:

- ответ только JSON: `{"ok": true, "mode": "fact"|"listen", "text": "...", "confidence": 0.0-1.0}` или `{"ok": false}`;
- `mode` / `fact` / `listen` / `ok` / `text` / `confidence` — не переводятся;
- пороги `0.85+`, `0.95+`, `0.99`, диапазон listen `0.60-0.85`;
- одна короткая строка; без эмодзи; без пересказа станции/артиста/трека;
- кавычки `"` и дефис `-` (без длинного тире).

Менять безопасно: формулировки тона, строгость отбора фактов, язык ответа (выбором файла).

Не рекомендуется менять: JSON-контракт, правило одной строки, разделение `fact`/`listen`, правило `ok=false`.

Подробный разбор: [`readme_ai_prompt_explained_rus.md`](../readme_ai_prompt_explained_rus.md).

## Редактирование

- Кодировка: **UTF-8 without BOM**.
- Окончания строк для новых/правленых шаблонов: **LF**.
- Максимальный размер файла на устройстве задаётся `AI_PROMPT_MAX_LEN` в `src/src/ai/ai_prompt.cpp` (сейчас 20480 байт).
- Не вставляйте API-ключи, пароли и персональные данные в prompt.

---

## English section

# AI Layer — prompt file catalog

The prompt defines AI Layer behavior: JSON response format, `fact` / `listen` selection, `confidence` thresholds, and the language of the short Main-screen line.

AI Layer can be fully disabled. Without an uploaded prompt the device stays silent — that is a normal state, not an error.

## Supported languages

| File | Response language |
|------|-------------------|
| [`ai_prompt_ru.txt`](ai_prompt_ru.txt) | Russian (RU) |
| [`ai_prompt_en.txt`](ai_prompt_en.txt) | English (EN) |
| [`ai_prompt_pl.txt`](ai_prompt_pl.txt) | Polish (PL) |
| [`ai_prompt_sk.txt`](ai_prompt_sk.txt) | Slovak (SK) |
| [`ai_prompt.txt`](ai_prompt.txt) | copy of the default active template (currently = RU) |

Firmware UI language (`L10N_LANGUAGE` in `src/myoptions.h`) does **not** select the prompt automatically. AI output language is defined only by the text of the uploaded prompt file.

## How the prompt is selected

At runtime the device always reads one LittleFS file:

```text
/ai/ai_prompt.txt
```

Ways to install it:

1. **Filesystem flash** — put the chosen template into `data/ai/ai_prompt.txt` (copy one of the files from this directory) and run `uploadfs`.
2. **Web UI** — Settings → AI → **Upload Prompt File**. The uploaded `.txt` filename is ignored; the file is stored as `/ai/ai_prompt.txt`.

A new prompt replaces the previous one. There is no fallback text: no file → AI Layer stays silent.

## Format and rules

All language variants keep the same technical contract:

- JSON only: `{"ok": true, "mode": "fact"|"listen", "text": "...", "confidence": 0.0-1.0}` or `{"ok": false}`;
- `mode` / `fact` / `listen` / `ok` / `text` / `confidence` are not translated;
- thresholds `0.85+`, `0.95+`, `0.99`, listen range `0.60-0.85`;
- one short line; no emoji; no retelling of station/artist/track;
- straight quotes `"` and hyphen `-` (no em dash).

Safe to change: tone wording, fact-selection strictness, response language (by choosing the file).

Do not change: the JSON contract, the one-line rule, the `fact`/`listen` split, or the `ok=false` rule.

Detailed explanation: [`readme_ai_prompt_explained_eng.md`](../readme_ai_prompt_explained_eng.md).

## Editing

- Encoding: **UTF-8 without BOM**.
- Line endings for new/edited templates: **LF**.
- Maximum on-device file size is set by `AI_PROMPT_MAX_LEN` in `src/src/ai/ai_prompt.cpp` (currently 20480 bytes).
- Do not put API keys, passwords, or personal data into the prompt.
