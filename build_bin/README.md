## Русская часть

# build_bin — готовые прошивки YoRadio LVGL

- **Проект:** [YoRadio LVGL](https://github.com/Witaliy76/Yoradio_lvgl)
- **Версия:** `0.9.434m-r2-lvgl-beta.3`
- **Дата:** 17 сентября 2026
- **Source commit:** `eaeef984666499f47d006538841b355259b3352c`
- **Поддерживаемая плата:** ESP32-4848S040
- **Языки:** [`RU`](4848S040/RU/) / [`EN`](4848S040/EN/) / [`PL`](4848S040/PL/) / [`SK`](4848S040/SK/)
- **Файловая система:** LittleFS
- **Статус:** пакет-кандидат beta.3; проверка на устройстве (B3-P7) ещё впереди

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
- **Date:** 2026-09-17
- **Source commit:** `eaeef984666499f47d006538841b355259b3352c`
- **Supported board:** ESP32-4848S040
- **Languages:** [`RU`](4848S040/RU/) / [`EN`](4848S040/EN/) / [`PL`](4848S040/PL/) / [`SK`](4848S040/SK/)
- **Filesystem:** LittleFS
- **Status:** beta.3 release candidate package; device RC smoke (B3-P7) pending

Do not mix `firmware.bin` and `littlefs.bin` across language packages.

Full flash map, Espressif Flash Download Tool settings, and screenshot:

→ [`4848S040/README.md`](4848S040/README.md)
