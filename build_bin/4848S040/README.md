## Русская часть

# YoRadio 4848S040 — прошивка

Плата: **ESP32-4848S040** (ST7701S RGB, 480×480, 4.0"), окружение PlatformIO
`[env:4848S040]`, board `esp32s3_n16r8` (16 MB Flash / 8 MB PSRAM).

Версия: `0.9.434m-r2-lvgl-beta.1`. Файловая система: **LittleFS**.

Выберите ровно одну языковую папку — `RU/`, `EN/` или `PL/` — и прошивайте
только файлы из неё. Не смешивайте файлы из разных языковых папок или разных
версий.

### A. FULL INITIAL FLASH — полная прошивка с нуля

Карта адресов ниже подтверждена контрольной сборкой (`pio run -e 4848S040` +
`pio run -e 4848S040 -t buildfs`) и построителем платформы pioarduino
(`framework-arduinoespressif32/tools/pioarduino-build.py`,
`platform-espressif32/builder/main.py`), а не предположением.

| Address  | File              | Purpose |
|----------|-------------------|---------|
| `0x0000` | `bootloader.bin`  | Second-stage bootloader |
| `0x8000` | `partitions.bin`  | Partition table (`partition_16MB_ota_largefs.csv`) |
| `0xe000` | `boot_app0.bin`   | OTA boot-select data (marks `ota_0` as active app) |
| `0x10000`| `firmware.bin`    | Application firmware (`app0` partition) |
| `0x810000`| `littlefs.bin`   | LittleFS filesystem image (WebUI, backgrounds, AI prompt, playlist, screensaver assets) |

Пример команды esptool (адреса и файлы реальные; порт — placeholder):

```
esptool.py --chip esp32s3 --port <PORT> --baud 921600 \
  --before default_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0000 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 firmware.bin \
  0x810000 littlefs.bin
```

Замените `<PORT>` на фактический COM-порт (Windows, например `COM4`) или
`/dev/ttyUSB0`/`/dev/ttyACM0` (Linux/macOS).

### B. FIRMWARE-ONLY UPDATE — обновление только прошивки

- Основной файл прошивки: **`firmware.bin`**.
- Адрес записи: **`0x10000`**.
- Этот вариант **не обновляет** содержимое LittleFS (WebUI, фоны, AI prompt,
  плейлист и т.д.) — файловая система остаётся прежней.
- Если менялись assets файловой системы (WebUI, темы, фоны, screensaver,
  AI prompt) — требуется отдельное обновление LittleFS согласно этой же
  таблице адресов (`littlefs.bin` @ `0x810000`) или через полную прошивку (A).
- Сохранность существующих пользовательских настроек/плейлиста при таком
  обновлении не гарантируется этим документом — полная OTA-инструкция будет
  описана позже в основном README платы.

### C. Важные предупреждения / Important warnings

- Прошивайте файлы только из **одной** языковой папки (RU или EN, или PL).
- Не смешивайте файлы разных версий или языковых вариантов.
- RU/EN/PL — compile-time варианты; язык интерфейса не переключается после
  прошивки.
- Перед прошивкой убедитесь, что подключённая плата — именно **4848S040**.
  Бинарники других плат (JC3248W535C, UEDX48480021) в этом каталоге
  намеренно отсутствуют.

---

## English section

Board: **ESP32-4848S040** (ST7701S RGB, 480×480, 4.0"), PlatformIO environment
`[env:4848S040]`, board `esp32s3_n16r8` (16 MB Flash / 8 MB PSRAM).

Version: `0.9.434m-r2-lvgl-beta.1`. Filesystem: **LittleFS**.

Pick exactly one language folder — `RU/`, `EN/` or `PL/` — and flash only the
files from that folder. Do not mix files from different language folders or
different versions.

### A. FULL INITIAL FLASH

The address map above is confirmed by an actual validation build
(`pio run -e 4848S040` + `pio run -e 4848S040 -t buildfs`) and by the platform
builder scripts (`framework-arduinoespressif32/tools/pioarduino-build.py`,
`platform-espressif32/builder/main.py`), not guessed.

See the table and esptool example above — same for both languages of this
document.

### B. FIRMWARE-ONLY UPDATE

- Main firmware file: **`firmware.bin`**.
- Flash address: **`0x10000`**.
- This variant does **not** update the LittleFS contents (WebUI, backgrounds,
  AI prompt, playlist, etc.) — the filesystem stays as-is.
- If filesystem assets changed, a separate LittleFS update is required (same
  address table, `littlefs.bin` @ `0x810000`) or use the full flash (A).
- Preservation of existing user settings/playlist during this update is not
  guaranteed by this document — a full OTA instruction will be documented
  later in the main board README.

### C. Important warnings

- Flash files from **one** language folder only (RU, EN, or PL).
- Do not mix files from different versions or language variants.
- RU/EN/PL are compile-time variants; the UI language cannot be switched
  after flashing.
- Before flashing, confirm the connected board is **4848S040**. Binaries for
  other boards (JC3248W535C, UEDX48480021) are intentionally absent from
  this directory.
