# Sample user fonts / Примеры пользовательских шрифтов

These are **optional sample user fonts** for YoRadio. They are third-party
SIL Open Font License (OFL) families, not YoRadio-owned fonts.

Это **необязательные примеры** пользовательских шрифтов для YoRadio.
Шрифты сторонние, под SIL OFL; это не шрифты, принадлежащие YoRadio.

## How to use / Как использовать

Upload a `.ttf` through **WebUI → Appearance → User Text Font**.
The current live UI font does not change until you reboot.

Загрузите `.ttf` через **WebUI → Appearance → User Text Font**.
Текущий живой шрифт интерфейса не меняется, пока не будет reboot.

Maximum user-font size is **512 KiB**. Application requires a reboot after a
successful upload.

Максимум пользовательского шрифта — **512 КиБ**. Применение — после reboot.

## What these files are not / Чем эти файлы не являются

- They are **not** embedded into firmware.
- They are **not** automatically copied to LittleFS.
- They are **not** listed in `board_build.embed_files`.
- They do **not** affect firmware or BIN size.

- Они **не** встраиваются в прошивку.
- Они **не** копируются в LittleFS автоматически.
- Они **не** входят в `board_build.embed_files`.
- Они **не** влияют на размер прошивки / BIN.

Factory text and Tabler icon fonts remain the embedded production fonts.

Заводской текст и иконки Tabler по-прежнему встроены в прошивку.

## Included samples / Включённые примеры

| File | Family | License |
| --- | --- | --- |
| `Play-Regular.ttf` | Play | SIL Open Font License 1.1 |
| `PT_Sans-Web-Regular.ttf` | PT Sans | SIL Open Font License 1.1 |

Official license texts (copyright and Reserved Font Names preserved):

- [`LICENSES/Play-OFL.txt`](LICENSES/Play-OFL.txt) — [google/fonts/ofl/play](https://github.com/google/fonts/tree/main/ofl/play)
- [`LICENSES/PT-Sans-OFL.txt`](LICENSES/PT-Sans-OFL.txt) — [google/fonts/ofl/ptsans](https://github.com/google/fonts/tree/main/ofl/ptsans)

## Local authoring / Локальные эксперименты

`.fontwork` is local authoring and experiment storage. It is gitignored and
is not part of the repository.

`.fontwork` — локальное хранилище экспериментов; оно в `.gitignore` и не
входит в репозиторий.
