[Back to the English project README](readme_english.md) — [Russian board guide](README_UEDX48480040.md)

# VIEWE UEDX48480040E-WB-A — wiring and flashing YoRadio

Hardware guide for the VIEWE UEDX48480040E-WB-A (4", 480×480): specifications, audio wiring, build, flashing, and board notes. Interface, themes, and Web UI are described in the [main English README](readme_english.md).

This is the second YoRadio LVGL board after the [ESP32-4848S040](README_4848S040_english.md). Support was added in stage S07_bis and verified on the device with build `0.9.434m-r2-lvgl-beta.3.09-S07bis.B014`: image, colours, touch, Wi-Fi, SD playback, and audio through an external PCM5102 DAC. There are no ready-to-flash packages for this board: build the firmware from source.

<!-- TODO: front photo of the board with the YoRadio interface (readme/english/viewe-front.jpg) -->

> A photo of the board will be added later.

## Board specifications

| Parameter | Value |
|---|---|
| Board | VIEWE UEDX48480040E-WB-A, verified revision V1.3 (2025.08) |
| MCU | ESP32-S3, 240 MHz, dual core |
| Flash | 16 MB |
| PSRAM | 8 MB (Octal) |
| Display | 4" IPS 480×480, GC9503 controller (panel UE040WV-RH40-A044C), RGB Panel |
| Touchscreen | FT6336U (I2C, address 0x38) |
| Memory card | microSD slot (SPI mode) |
| USB | two USB-C connectors: **UART** (through CH340) and **USB** (ESP32-S3 native USB) |
| Onboard audio | none |
| File system | LittleFS |

## What you need

- A VIEWE UEDX48480040E-WB-A board.
- A USB-C cable for the **UART** connector.
- An external I2S DAC, for example a PCM5102 module (required: the board has no audio of its own).
- Wires to the connector on the back of the board.
- Powered speakers or an amplifier on the DAC output.
- [PlatformIO](https://platformio.org/) to build from source.

## Important audio note

The board has no audio path, and the vendor did not reserve pins for audio. YoRadio sends I2S to an external DAC through three lines of the connector on the back of the board:

- **GPIO19 and GPIO20** are the ESP32-S3 native USB lines. They are free while nothing is plugged into the **USB** connector.
- **GPIO48** is the display command clock line. It passes to I2S after the panel is initialized.

Hence the main rule: **leave the USB connector empty**. Power, flashing, and the log go through the **UART** connector.

## Connecting an external DAC

The back of the board has a two-row 40-pin connector (J1 on the schematic).

> **Important.** The silkscreen next to the pins shows **GPIO numbers** ("19", "20", "48", "GND", "5V"), not connector pin numbers. Wire by these labels.

| I2S signal | PCM5102 pin | Connector label (GPIO) | J1 pin on the schematic |
|---|---|---|---|
| BCLK | BCK | **19** | 9 |
| LRCK / WS | LCK | **48** | 19 |
| DATA (DOUT) | DIN | **20** | 10 |
| Ground | GND | **GND** | 1 or 3 |
| Power | VIN | **5V** | 2 |

Do not connect the DAC to the labels **"9", "10", "17"**. Those are GPIO9 and GPIO10 (display data G1 and G0) and GPIO17 (display VSYNC). The DAC would then receive image data and stay silent.

Power the PCM5102 module from 5 V: it has its own 3.3 V regulators. Jumpers of the common module: FLT = L, DEMP = L, XSMT = H, FMT = L. Tie the module's SCK pin to GND if the module has no jumper for it; the module then derives its own clock from BCK.

All three signals are ESP32 outputs in I2S master mode. The firmware does not use an I2S data input and does not output MCLK. Pins 19 / 48 / 20 are the board profile defaults. You can change them in `src/myoptions.h` (`I2S_DOUT`, `I2S_BCLK`, `I2S_LRC`), but this board has almost no free GPIO. The used lines are listed in the GPIO reference table below.

After boot, Serial prints `i2s-dout: 20`, `i2s-bclk: 19`, `i2s-lrc: 48` in the `[HARDWARE CONFIG]` block. These are the values the firmware was built with.

<!-- TODO: photo of the back of the board with the connector and the DAC wired (readme/english/viewe-back-header.jpg) -->

> A photo of the back with the connector will be added later.

## Build and flash

1. Open `src/myoptions.h` and select the board:

   ```cpp
   #define YORADIO_BOARD YORADIO_BOARD_UEDX48480040
   ```

2. Set `L10N_LANGUAGE`: `RU`, `EN`, `PL`, or `SK`.
3. In PlatformIO select the `[env:S3_N16R8]` environment. It is the shared environment of the ESP32-S3 N16R8 family; only `YORADIO_BOARD` selects the board.
4. Connect the cable to the **UART** connector, then build and upload the firmware and the LittleFS file system (in PlatformIO: Upload and Upload Filesystem Image).
5. Wi-Fi and the station list are configured on first start and through the Web UI / files in `data/`.

Details of building from source are in the [main README](readme_english.md#note-on-building-from-source) and in `platformio.ini`.

`DISPLAY_BOOT_ORIENTATION_180` is not supported for GC9503: with `true` the build stops with a message. Custom Wiring (`YORADIO_CUSTOM_WIRING`) has not been tested on this board.

## Ready-to-flash packages

There are no packages for the VIEWE UEDX48480040E-WB-A. The packages in [build_bin/4848S040/](build_bin/4848S040/) are built for another board and do not fit this one.

## First start

After power-on YoRadio shows the boot screen and tries to connect to the saved network. If no network is configured or it is unavailable, Wi-Fi Setup / Recovery opens: scan, choose a network, enter the password (not requested for an open network), and save. After saving, the device restarts and opens Main.

If Recovery is left without input, the `yoRadioAP` access point (no password) comes up. Connect to it and continue the setup through the Web UI at the access point IP.

## YoRadio interface

Screens, gestures, and the Web UI are the same as on the ESP32-4848S040: both boards have a square 480×480 screen and the same interface profile. Screens are described in the main [project README](readme_english.md#interface), gestures in the [ESP32-4848S040 guide](README_4848S040_english.md#control-notes-for-esp32-4848s040).

## Board notes

- **Display clock.** The panel runs at 12 MHz (about 44 frames per second): 6 MHz at boot, one promotion to 12 MHz after Main opens, and 6 MHz during flash writes. The panel datasheet asks for at least 16 MHz, but without a bounce buffer 16 MHz shifts the image under load, and at 10 MHz the screen sometimes flickered visibly. 12 MHz is verified under load (radio, HTTPS, settings writes) and on cold start.
- **LED.** The board's RGB LED (WS2812) sits on the SD card line (GPIO42) and blinks on card access. This is expected; the firmware does not drive it.
- **Touch.** The FT6336U is polled: its interrupt line is not routed on the board.
- **BOOT button** is on GPIO0, which is also the display R4 line, as on the ESP32-4848S040. Wake from Deep Sleep by BOOT (`WAKE_PIN 0`) has not been tested on this board yet.

## GPIO reference table

Factory wiring of the VIEWE UEDX48480040E-WB-A (schematic V1.3). The full map and evidence are in the [board profile](src/src/platform/board/profiles/esp32s3_uedx48480040/README.md).

| Function | GPIO |
|---|---|
| Display data B0–B4 / G0–G5 / R0–R4 | 15–11 / 10–5 / 4–0 |
| Display HSYNC / VSYNC / DE / PCLK | 16 / 17 / 18 / 21 |
| Display commands CS / SCK / SDA (3-wire) | 39 / 48 / 47 |
| Backlight (PWM) | 38 |
| FT6336U touch SDA / SCL | 40 / 41 |
| microSD CS / CLK / CMD / DAT0 | 47 / 45 / 42 / 46 |
| I2S BCLK / LRCK / DATA | 19 / 48 / 20 |
| UART (CH340) TX / RX | 43 / 44 |
| Native USB D− / D+ | 19 / 20 (used by I2S) |
| BOOT button | 0 (shared with the R4 line) |

Two lines are used in turn: GPIO47 first carries panel commands and then serves as the card CS; GPIO48 first clocks panel commands and then serves as I2S LRCK / WS. SD is enabled by `YORADIO_SD_ENABLE` in `src/myoptions.h`; see the [Storage Contract](src/src/platform/storage/README.md).

## Closing

- Main project README → [readme_english.md](readme_english.md).
- ESP32-4848S040 guide → [README_4848S040_english.md](README_4848S040_english.md).
- Questions and bugs → [Issues](https://github.com/Witaliy76/Yoradio_lvgl/issues).
- Adaptation author: [Witaliy76](https://github.com/Witaliy76).
- License: GNU General Public License v3 or later — [LICENSE](LICENSE).
