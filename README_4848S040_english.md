[Back to the English project README](readme_english.md) — [Russian board guide](README_4848S040.md)

# YoRadio LVGL for ESP32-4848S040

This is the hardware guide for ESP32-4848S040, the board supported by the public YoRadio LVGL beta. It covers specifications, audio wiring, configuration, ready-to-flash packages, first start, and touch controls. For the interface, themes, and Web UI, see the [full English project README](readme_english.md).

Public package version: `0.9.434m-r2-lvgl-beta.2`. Current source: `0.9.434m-r2-lvgl-beta.2-s6.4`.

The supported module provides **16 MB Flash** and **8 MB PSRAM**.

<p align="center">
  <img src="readme/english/device-front.jpg" alt="YoRadio LVGL device" width="450">
</p>

## Hardware specifications

| Item | Value |
|---|---|
| MCU | ESP32-S3-WROOM-1-N16R8, 240 MHz dual core |
| Flash | 16 MB |
| PSRAM | 8 MB Octal |
| Display | ST7701S RGB panel, 480×480 |
| Touch | GT911 over I2C |
| USB / power | USB-C |
| Filesystem | LittleFS |
| Default audio | External I2S DAC or amplifier |

## What you need

- ESP32-4848S040 board.
- USB-C cable and a suitable power supply.
- External I2S DAC or amplifier with an I2S input, recommended for normal use.
- Wires for the rear connector.
- Powered speakers or an amplifier connected to the DAC output.
- [PlatformIO](https://platformio.org/) if you intend to build from source.

## Audio

The default YoRadio LVGL configuration for ESP32-4848S040 uses an **external I2S DAC** connected to the rear header. This is the primary audio path. A suitable external DAC or I2S amplifier can provide stereo output.

The board's built-in mono audio path is optional and is mainly useful for initial setup or a quick board check before an external DAC is connected. Enable it only when appropriate for your board revision by configuring the **R21 / R22 / R23** jumpers.

The configuration disables an external VS1053 decoder with `VS1053_CS=255`; decoded audio is sent through I2S.

### External DAC wiring

| Signal | GPIO | Purpose |
|---|---:|---|
| DATA / DOUT | 40 | I2S audio data |
| BCLK | 1 | Bit clock |
| LRCK / WS / LRC | 2 | Left/right word select |
| GND | — | Common ground |
| Power | — | Follow the DAC requirements |

Rear connector and I2S lines:

<p align="center">
  <img src="https://github.com/user-attachments/assets/a40160ec-31af-463c-88bb-d60b51b0e37b" alt="ESP32-4848S040 rear connector" width="450">
</p>

Built-in mono path jumpers R21, R22, and R23:

<p align="center">
  <img src="https://github.com/user-attachments/assets/d2fbc24c-9036-4c09-ace8-5323dc4e30fb" alt="ESP32-4848S040 audio jumpers" width="450">
</p>

Rear side of the board:

<p align="center">
  <img src="https://github.com/user-attachments/assets/34dd64a3-305e-4486-a07c-fedf7416baa0" alt="Rear side of ESP32-4848S040" width="450">
</p>

Check the markings and schematic for the exact electrical details of your board revision.

## Configuration

1. Copy `src/myoptions_4848S040.h` over `src/myoptions.h`.
2. In `src/myoptions.h`, set `L10N_LANGUAGE` to `RU`, `EN`, `PL`, or `SK`.
3. Select the PlatformIO environment `[env:4848S040]`.
4. Configure Wi-Fi and stations on first start and through the Web UI or files under `data/`.

The language selector belongs in `src/myoptions.h`; it is not a `platformio.ini` runtime setting. Source-build requirements, including the KnownGood ESP-IDF library set, are documented in the [English project README](readme_english.md#source-build-note) and `platformio.ini`.

## Ready-to-flash packages

Ready-to-flash packages for `0.9.434m-r2-lvgl-beta.2` are in [`build_bin/4848S040/`](build_bin/4848S040/) for **RU / EN / PL / SK**.

Each language folder contains:

- `bootloader.bin`;
- `partitions.bin`;
- `boot_app0.bin`;
- `firmware.bin`;
- `littlefs.bin`.

Use all files from one language folder. Never mix firmware and LittleFS from different language folders. LittleFS contains the matching Web UI, assets, and AI prompt.

The complete five-file address map, current package metadata, and Espressif Flash Download Tool instructions are in [`build_bin/4848S040/README.md`](build_bin/4848S040/README.md). They are intentionally not duplicated here.

## First start

On power-up, YoRadio shows the boot screen and tries the saved network. If no usable network is configured, Wi-Fi Setup / Recovery opens: scan, select a network, enter its password when required, and save. The device restarts and opens Main.

If Recovery remains idle, YoRadio starts the open `yoRadioAP` access point. Connect to it and continue configuration through the Web UI at the access-point IP address.

## Touch controls

ESP32-4848S040 uses touchscreen control; no encoder is connected in the default configuration.

| Gesture | Action |
|---|---|
| Horizontal swipe | Change Info / Main / Visual / Stations / Weather / Settings |
| Drag the Main volume slider | Adjust volume |
| Vertical swipe on Stations | Browse the station list |
| Downward swipe from the top edge on Info / Main / Visual / Weather | Open Preset Temporary |
| Tap / long press in Preset Temporary | Play the saved station / save the current station to the slot |
| Tap in Screensaver | Exit the Screensaver |

A generic left/right swipe changes pages; it does not control volume.

## GPIO reference

The values below follow `src/myoptions_4848S040.h`.

| Function | GPIO |
|---|---|
| Display CS / SCK / SDA for the software control bus | 39 / 48 / 47 |
| Display DE / VSYNC / HSYNC / PCLK | 18 / 17 / 16 / 21 |
| Display RGB data | 16-bit RGB bus; see `src/myoptions_4848S040.h` |
| Backlight PWM | 38 |
| GT911 touch SDA / SCL | 19 / 45 |
| I2S DATA / BCLK / LRCK | 40 / 1 / 2 |

The SD pins in this configuration are not used for playback in the public beta.

## Links

- [Full English project README](readme_english.md)
- [Russian board guide](README_4848S040.md)
- [Ready-to-flash package](build_bin/4848S040/)
- [Flash Download Tool guide](build_bin/4848S040/README.md)
- [Repository](https://github.com/Witaliy76/Yoradio_lvgl)
- [Issues](https://github.com/Witaliy76/Yoradio_lvgl/issues)
- [GNU GPL v3 or later](LICENSE)
