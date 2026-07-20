## Русская часть

# build_bin — готовые прошивки YoRadio

Этот каталог содержит готовые (pre-built) прошивки YoRadio RGB Panel для официально
поддерживаемых плат публичной LVGL beta.

- **Поддерживаемая плата beta:** только `4848S040`. Другие board-профили (JC3248W535C,
  UEDX48480021) остаются в исходниках проекта, но их готовые бинарники в этот каталог
  не включаются.
- **Языковые варианты интерфейса:** прошивки собираются в трёх compile-time вариантах —
  `RU`, `EN`, `PL`. Язык фиксируется на этапе компиляции (`L10N_LANGUAGE` в
  `src/myoptions.h`) и не переключается во время работы устройства.
- **Текущая release-версия:** `0.9.434m-r2-lvgl-beta.1`.
- Каждая языковая папка (`4848S040/RU/`, `4848S040/EN/`, `4848S040/PL/`) будет содержать
  полный самостоятельный набор файлов, необходимых для прошивки платы с нуля (bootloader,
  таблица партиций, firmware, образ файловой системы) — без зависимости от файлов из
  других языковых папок.
- **До появления реальных `.bin`-файлов** README-файлы в этом каталоге являются только
  структурным placeholder'ом и не считаются готовым релизом.
- Финальные бинарники будут собраны и добавлены отдельным release-build этапом. На этом
  этапе точный commit сборки (source commit) будет записан в соответствующий README.
- **Файловая система: LittleFS**. Образ файловой системы, поставляемый в этом
  каталоге, — LittleFS image.

Инструкция по прошивке конкретной платы — в `4848S040/README.md`.

---

## English section

# build_bin — pre-built YoRadio firmware

This directory holds pre-built YoRadio RGB Panel firmware for the boards officially
supported by the public LVGL beta.

- **Supported beta board:** `4848S040` only. Other board profiles (JC3248W535C,
  UEDX48480021) remain in the project sources, but their pre-built binaries are not
  included in this directory.
- **UI language variants:** firmware is built in three compile-time variants — `RU`,
  `EN`, `PL`. The language is fixed at compile time (`L10N_LANGUAGE` in
  `src/myoptions.h`) and cannot be switched at runtime.
- **Current release version:** `0.9.434m-r2-lvgl-beta.1`.
- Each language folder (`4848S040/RU/`, `4848S040/EN/`, `4848S040/PL/`) will contain a
  complete, self-contained set of files needed to flash the board from scratch
  (bootloader, partition table, firmware, filesystem image) — independent of files in
  other language folders.
- **Until real `.bin` files are added**, the README files in this directory are a
  structural placeholder only and do not represent a ready release.
- Final binaries will be built and added by a separate release-build stage. At that
  point, the exact source commit used for the build will be recorded in the
  corresponding README.
- **Filesystem: LittleFS**. The filesystem image shipped in this directory
  is a LittleFS image.

See `4848S040/README.md` for board-specific flashing instructions.
