 [Русская версия](README.md)

### Yoradio — Wi-Fi internet radio for ESP32 RGB Panel displays, where music is primary and the AI layer is a quiet experiment in “bringing the device to life”

- See: [readme_ai_layer_eng.md](readme_ai_layer_eng.md)

- Based on `e2002/yoradio` (`https://github.com/e2002/yoradio`). This fork adds support for ESP32‑S3 boards with RGB Panel displays using `Arduino_GFX` and `.pioarduino`/PlatformIO.

### Supported dev boards

#### 4848S040 (ST7701S, 480x480, 4.0" square)
- [AliExpress link](https://aliexpress.ru/item/1005008214872438.html?)
- [How to connect this board to the project →](README_4848S040_english.md)
- ![photo_3_2026-01-29_21-02-50](https://github.com/user-attachments/assets/f3b3b624-8d00-484f-b70f-bf865368d511)


#### UEDX48480021-MD80ET (ST7701S, 480x480, 2.1" round)
- [AliExpress link](https://aliexpress.ru/item/1005007576008287.html?)
- [How to connect this board to the project →](README_UEDX48480021_english.md)
- ![photo_11_2026-01-29_21-02-50](https://github.com/user-attachments/assets/a3c049f6-1511-42ab-9d7b-3fdadbe5cd9e)


#### JC3248W535C (AXS15231B, 320x480, 3.5")
- [AliExpress link](https://aliexpress.ru/item/1005007566332450.html)
- [How to connect this board to the project →](README_JC3248W535C_english.md)
- ![photo_13_2026-01-29_21-02-50](https://github.com/user-attachments/assets/76bca77d-ac8f-4338-9f02-8da2146a6668)


### Project Features

Differences from original Yoradio:

1. **Refactored to Arduino_GFX** — using modern Arduino_GFX library with RGB Panel support and latest ESP-IDF 5.4/5.5.
2. **U8g2 fonts support** — Arduino_GFX library supports U8g2 fonts, you can use them in the project.
3. **Spectrum Analyzer** — added spectrum analyzer with VU ↔ SA switching directly from web interface (Settings). VU-meter now has scale (adjustable in `widgets.cpp`).
4. **SD ↔ Radio switching** — added ability to switch via touchscreen (simultaneous two-finger tap).
5. **CPU Load widget** — shows load of both processor cores.
6. **Format support** — OGG, OPUS, VORBIS, FLAC streams.
7. **Auto-dimming** — widget in `main.cpp` (AUTOBACKLIGHT settings in `myoptions.h`).
8. **Battery** — code built into display files, activated by uncommenting `#define BATTERY_OFF` (for UEDX48480021 requires free pins).
9. **Updated libraries** — AudioI2S from Wolle (schreibfaul1) & Maleksm, Version 3.4.2p.
10. **Many bugfixes** — stability and performance improvements.
11. **AI_layer**

## AI Layer (optional)

This version of Yoradio optionally includes an AI Layer —
a quiet semantic layer that may add meaning to background music.

The AI Layer is not an assistant and does not interact with the user.
It may remain silent and does not affect system behavior when disabled.

The device remains a Wi‑Fi internet radio and an object of presence,
where music is primary and meaning appears only when appropriate.

See: [readme_ai_layer_eng.md](readme_ai_layer_eng.md)

### Changelog

- 07.03.2026
  - Filesystem migration from SPIFFS to LittleFS: config, web server, playlists, AI prompt, OTA. First boot after update will format the FS partition (re-upload via “Upload filesystem image”).
  - AI prompt size limit increased to 20 KB; fixed prompt upload rejecting valid files.
  - Fixed weather crash on DNS failure (api.openweathermap.org): resolve hostname before connect.
- 29.01.2026
  - Added AI Layer (core architecture and integration).
  - Fixed text rendering, scrolling, and display optimizations.
  - Added LwIP libs for ESP‑IDF 5.5.2 / Arduino 3.3.6 (`b2159fa`).
  - Updated audioI2S for stable operation (`0d81d2c`).
- 25.10.2025
  - Fixed: Wi‑Fi boot screen status updates and robust multi‑SSID iteration.
  - Added support for JC3248W535C board (AXS15231B QSPI, 320x480, 3.5").
  - Updated liblwip.a and libesp_netif.a for ESP‑IDF 5.5 (stable), LwIP optimizations.
  - Updated audioI2S to 3.4.2p (logging improvements, NetworkClient, decoders updates).
  - Improved touchscreen handling: DEBUG_TOUCH via web interface, false click protection after swipes, proper multi-touch SD card detection.
  - Added SPECTRUM_GRADIENT option: QSPI displays use solid colors, RGB Panels use smooth gradients.
- 12.10.2025 — Project created. Added boards 4848S040 and UEDX48480021‑MD80ET.

### Important notes

- English font: replace `.pio/libdeps/<env>/GFX Library for Arduino/src/font/glcdfont.h` with the file from `fonts/glcdfont_EN.c` (where `<env>` is the PlatformIO environment).

- Language switch (RU/EN): in `myoptions.h`, change `L10N_LANGUAGE` from `RU` to `EN`.

- High bitrate radio stability: replace IDF libs with prebuilt ones from `library!/esp32s3_5_5_2__3_3_6/`:
  - `libesp_netif.a`
  - `liblwip.a`
  - Copy to (Windows): `%USERPROFILE%\.platformio\packages\framework-arduinoespressif32-libs\esp32s3\lib\`
  - Version: ESP-IDF 5.5.2 / Arduino 3.3.6. Restart PlatformIO and rebuild after replacing.
  
- First boot / after erase flash: screen can stay black for ~60 seconds (FS init). This is normal — just wait.

- How to work with the project: follow the original `e2002/yoradio` docs and examples.

### Acknowledgments

Special thanks to:
- **e2002** — author of the original Yoradio project
- **Wolle (schreibfaul1)** — for the excellent AudioI2S library
- **Maleksm** (4pda.to) — for AudioI2S improvements and enhancements
- **moononournation** — for the Arduino_GFX library

### License

This project is derived from [YoRadio](https://github.com/e2002/yoradio) (e2002) and is licensed under the **GNU General Public License v3 or later** — see [`LICENSE`](LICENSE).

Modifications (RGB Panel support, LVGL UI, AI layer, etc.): Copyright **Witaliy76** (2025–2026).  
Upstream YoRadio: **e2002** and YoRadio contributors.

Third-party components (LVGL, AsyncWebServer, libfaad, Tabler Icons, etc.): see [`NOTICE`](NOTICE).

If you distribute compiled firmware (.bin), GPL v3 requires making the corresponding source available (this repository + build instructions in `platformio.ini`).


