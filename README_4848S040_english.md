[Back to the English project README](readme_english.md) — [Russian board guide](README_4848S040.md)

# ESP32-4848S040 — wiring and flashing YoRadio

Hardware guide for ESP32-4848S040, the board supported in the public YoRadio LVGL beta: specifications, audio, configuration, ready-to-flash packages, and wiring notes. Interface, themes, and Web UI are described in the [main English README](readme_english.md).

Public project version: `0.9.434m-r2-lvgl-beta.3`. Current source: `0.9.434m-r2-lvgl-beta.3`.

<p align="center">
  <img src="readme/english/device-front.jpg" alt="ESP32-4848S040 with the YoRadio interface" width="450">
</p>

## Board specifications

| Item | Value |
|---|---|
| MCU | ESP32-S3-WROOM-1-N16R8 (240 MHz, dual core) |
| Flash | 16 MB |
| PSRAM | 8 MB (Octal) |
| Display | ST7701S, RGB Panel, 480×480 |
| Touch | GT911 (I2C) |
| USB / power | USB-C |
| Filesystem | LittleFS |

## What you need

- ESP32-4848S040 board.
- USB-C cable.
- External I2S DAC or amplifier with an I2S input (recommended).
- Wires for the rear connector.
- Powered speakers or an amplifier on the DAC output.
- [PlatformIO](https://platformio.org/) — if you build from source.

## Important audio note

The default YoRadio LVGL configuration for ESP32-4848S040 is designed for an **external I2S DAC** connected to the board’s rear connector. That is the primary working path: the stock output is the I2S bus (`DOUT` / `BCLK` / `LRCK`), and with a matching DAC or amplifier the path works as stereo.

The board’s built-in mono audio path is **not** the default output option. You can enable it with jumpers **R21 / R22 / R23** for an initial check and setup — for example, to confirm that the board works before connecting an external DAC.

The DAC wiring on the board has not changed — the verified photos below are kept as-is.

## Connecting an external DAC

Lines required for an external DAC:

| Signal | GPIO | Purpose |
|---|---|---|
| DATA (DOUT) | 40 | I2S data |
| BCLK | 1 | Bit Clock |
| LRCK / WS (LRC) | 2 | Word Select |
| GND | — | Common ground |
| Power | — | Per DAC requirements |

Rear connector with the wiring lines:

<p align="center">
  <img src="https://github.com/user-attachments/assets/a40160ec-31af-463c-88bb-d60b51b0e37b" alt="ESP32-4848S040 rear connector" width="450">
</p>

Built-in mono DAC jumpers (R21, R22, R23):

<p align="center">
  <img src="https://github.com/user-attachments/assets/d2fbc24c-9036-4c09-ace8-5323dc4e30fb" alt="ESP32-4848S040 built-in DAC jumpers" width="450">
</p>

Board rear side:

<p align="center">
  <img src="https://github.com/user-attachments/assets/34dd64a3-305e-4486-a07c-fedf7416baa0" alt="ESP32-4848S040 rear side" width="450">
</p>

Confirm the exact electrical details of your board revision against its marking and schematic.

## Preparing the configuration

1. Copy `src/myoptions_4848S040.h` over `src/myoptions.h`.
2. In `src/myoptions.h` set `L10N_LANGUAGE`: `RU`, `EN`, `PL`, or `SK`.
3. In PlatformIO select the `[env:4848S040]` environment.
4. Wi-Fi and the station list are configured on first start and through the Web UI / files in `data/`.

Build-from-source details, including the matched ESP-IDF library set, are in the [main English README](readme_english.md#note-on-building-from-source) and in `platformio.ini`.

## Ready-to-flash packages

Ready packages for `0.9.434m-r2-lvgl-beta.3` are in [build_bin/4848S040/](build_bin/4848S040/) for **RU**, **EN**, **PL**, and **SK**. Exact checksums and the exact source commit for each package are listed in the README inside the corresponding language directory.

Each language folder contains:

- `bootloader.bin`
- `partitions.bin`
- `boot_app0.bin`
- `firmware.bin`
- `littlefs.bin`

`firmware.bin` and `littlefs.bin` must come from the **same** language folder.
LittleFS holds the Web UI, assets, and AI prompt for that language.

A full flash uses the five-file set; the detailed address map,
Espressif Flash Download Tool settings, and a current screenshot are in
[build_bin/4848S040/README.md](build_bin/4848S040/README.md).

## First start

After power-on YoRadio shows the boot screen and tries to connect to a saved network. If the network is not configured or unavailable, Wi-Fi Setup / Recovery opens: scan, choose a network, enter the password (not asked for an open network), and save. After saving, the device reboots and opens Main.

If Recovery is left idle, the `yoRadioAP` access point comes up (no password). Connect to it and continue setup through the Web UI at the AP IP address.

## YoRadio interface

Descriptions of Main, Info, Visual, Station, Weather, and Settings, as well as Preset Temporary, Screensaver, Deep Sleep / TIMERS, and the Web UI, are in the [main project README](readme_english.md#interface).

## Control notes for ESP32-4848S040

On this board control is touch-only; no encoder is wired.

| Gesture | Action |
|---|---|
| Horizontal swipe | Switch Info / Main / Visual / Station / Weather / Settings |
| Volume slide on Main | Adjust volume |
| Vertical swipe on Station | Page through the station list |
| Downward swipe from the top edge (Info / Main / Visual / Weather) | Open Preset Temporary |
| Short / long press in Preset | Play a station / save the current one to the slot |
| Tap in Screensaver | Exit the screensaver |
| Short BOOT press in Deep Sleep | Wake the device (see section below) |

## Waking from Deep Sleep

Deep Sleep puts the ESP32-S3 into its low-power sleep state. The GT911 touchscreen is not a wake source, but wake through the configurable `WAKE_PIN` is supported; in the stock configuration that is the BOOT button on GPIO0, active LOW.

Default configuration (`src/myoptions.h`, `src/myoptions_4848S040.h`):

```cpp
#define WAKE_PIN      0     // stock BOOT button
#define WAKE_LEVEL    LOW   // BOOT shorts GPIO0 to GND
```

The stock **BOOT** button sits on **GPIO0** and shorts it to GND when pressed, so wake works without extra soldering and without an external pull-up — the pull-up is already on the board.

GPIO0 is also used by the RGB panel as `ST7701_R4` (red-channel data line). To avoid a conflict, the firmware calls `rtc_gpio_deinit()` at the very start of `setup()` — before configuration and display init — and returns GPIO0 from RTC IO mode to ordinary digital GPIO. The log then shows:

```text
[WAKE] cause=EXT0
[WAKE] pin=0 restored to digital GPIO
```

**Operating sequence:**

1. Enter Deep Sleep: through Settings → TIMERS → DEEP SLEEP (`SLEEP NOW`, see below), or with the `deepsleep` Telnet/Serial command. Wait until the screen and backlight are fully off.
2. Briefly press BOOT and release immediately.
3. The board boots normally.

> **Caution.** Do not press BOOT while the display is active: GPIO0 carries RGB panel data at that time.

> **Caution.** Do not hold BOOT during a hardware Reset — GPIO0 is a boot strapping pin, and holding it LOW through Reset puts the chip into download mode instead of normal boot.

Reset and power reconnect remain fallback startup methods. `WAKE_PIN=255` fully disables GPIO wake (it does not affect the RTC timer below). In the stock ESP32-4848S040 map there is no free RTC GPIO besides GPIO0 — BOOT/GPIO0 remains the default for this profile. On another board or with a different layout you can choose another free RTC GPIO (GPIO0–21 on ESP32-S3) and active level LOW or HIGH.

Before Deep Sleep the firmware cleanly stops playback, saves state, turns off the display, and only then puts the ESP32-S3 to sleep.

### TIMERS page and relative wake

The general description of Radio and Deep Sleep timers is in the [main English README](readme_english.md#timers). Below are the points that matter specifically for ESP32-4848S040.

In Settings → TIMERS → `DEEP SLEEP`:

- `DEEP SLEEP AFTER` sets the delay before entering deep sleep;
- `WAKE AFTER SLEEP` is the RTC interval that starts counting only after actual sleep entry;
- Sleep 5 MIN + Wake 3 MIN means: after 5 minutes the board sleeps, then wakes roughly 3 minutes later;
- if Sleep is off and Wake is on, the UI shows `WAKE AFTER NEXT SLEEP`: no countdown is running yet; the preset applies to the next `SLEEP NOW` or `deepsleep` command;
- BOOT and the RTC timer may be enabled together — the first source wins;
- absolute `HH:MM` scheduling is not implemented;
- if the local clock is not synchronized yet (`CLOCK NOT SYNCED`), relative timers still keep running.

For normal use the commands below are not required; they are for optional Telnet/Serial control.

| Telnet / Serial command | Action |
|---|---|
| `sleeptimer N` / `sleeptimer 0` | Set / cancel Radio Stop after `N` minutes; the UI preset is unchanged |
| `playtimer N` / `playtimer 0` | Set / cancel Radio Start after `N` minutes |
| `deepsleep` | Immediate Deep Sleep with the saved `WAKE AFTER SLEEP` |
| `deepsleep N` / `deepsleep 0` | Set / cancel Deep Sleep after `N` minutes |
| `sleep N` | Compatibility with the old CLI: immediate Deep Sleep with a one-shot wake after `N` minutes |
| `sleep N M` | Compatibility with the old CLI: Deep Sleep after `M` minutes and a one-shot wake after `N` minutes from entry |

The commands work over Serial and Telnet. The valid range is 0…1499 minutes where applicable. A one-shot wake interval from the legacy `sleep` command does not overwrite the saved `WAKE AFTER SLEEP` preset.

## GPIO reference table

Pins from `src/myoptions_4848S040.h`:

| Purpose | GPIO |
|---|---|
| Display: CS / SCK / SDA (SWSPI) | 39 / 48 / 47 |
| Display: DE / VSYNC / HSYNC / PCLK | 18 / 17 / 16 / 21 |
| Display: data R0–R4, G0–G5, B0–B4 | 16-bit RGB bus (see `src/myoptions_4848S040.h`) |
| Backlight (PWM) | 38 |
| Touch GT911: SDA / SCL | 19 / 45 |
| I2S: DATA / BCLK / LRCK | 40 / 1 / 2 |
| BOOT button / Deep Sleep wake (`WAKE_PIN`, active LOW) | 0 — shared with `ST7701_R4` |
| Physical MUTE button (`BTN_MUTE`, optional) | 255 — unused by default |
| Amplifier MUTE/enable output (`MUTE_PIN`, optional) | 255 — unused by default |

SD pins (CS=42, SCK=48, MISO=41, MOSI=47) are unused in this configuration: SD playback is disabled in the public beta.

**MUTE.** `BTN_MUTE` is an optional physical button to GND with an internal pull-up. `MUTE_PIN` is an independent optional GPIO output for mute/enable of an external amplifier (not the same signal as the button). Both lines are disabled by default (`255`) on ESP32-4848S040. If your board or amplifier supports hardware mute/enable, set a free GPIO in `src/myoptions_4848S040.h`.

## Closing

- Main project README → [readme_english.md](readme_english.md).
- Questions and bugs → [Issues](https://github.com/Witaliy76/Yoradio_lvgl/issues).
- Adaptation author: [Witaliy76](https://github.com/Witaliy76).
- License: GNU General Public License v3 or later — [LICENSE](LICENSE).
