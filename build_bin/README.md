## Русская часть

# build_bin — готовые прошивки YoRadio LVGL

- **Проект:** [YoRadio LVGL](https://github.com/Witaliy76/Yoradio_lvgl)
- **Версия:** `0.9.434m-r2-lvgl-beta.3`
- **Дата:** 21 сентября 2026
- **Source commit:** `7fe86c4fe9e37ff74fd9c048f78be4e8e0917441`
- **Поддерживаемая плата:** ESP32-4848S040
- **Языки:** [`RU`](4848S040/RU/) / [`EN`](4848S040/EN/) / [`PL`](4848S040/PL/) / [`SK`](4848S040/SK/)
- **Файловая система:** LittleFS
- **Статус:** финальный пакет beta.3; физическая проверка на устройстве после последних RC-изменений не выполнялась

Каждая языковая папка содержит согласованный комплект из пяти файлов
(`bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `firmware.bin`, `littlefs.bin`).
**Не смешивайте** `firmware.bin` и `littlefs.bin` из разных языковых пакетов.

Полная карта адресов, настройки Espressif Flash Download Tool и screenshot:

→ [`4848S040/README.md`](4848S040/README.md)

---

## English section

# build_bin — pre-built YoRadio LVGL firmware

- **Project:** [YoRadio LVGL](https://github.com/Witaliy76/Yoradio_lvgl)
- **Version:** `0.9.434m-r2-lvgl-beta.3`
- **Date:** 2026-09-21
- **Source commit:** `7fe86c4fe9e37ff74fd9c048f78be4e8e0917441`
- **Supported board:** ESP32-4848S040
- **Languages:** [`RU`](4848S040/RU/) / [`EN`](4848S040/EN/) / [`PL`](4848S040/PL/) / [`SK`](4848S040/SK/)
- **Filesystem:** LittleFS
- **Status:** final beta.3 package; physical device smoke after latest RC changes not performed (device unavailable)

Do not mix `firmware.bin` and `littlefs.bin` across language packages.

Full flash map, Espressif Flash Download Tool settings, and screenshot:

→ [`4848S040/README.md`](4848S040/README.md)
