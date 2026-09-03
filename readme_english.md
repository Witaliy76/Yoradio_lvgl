[Russian version](README.md)

# YoRadio LVGL

YoRadio LVGL is an ESP32-S3 Wi-Fi radio with a square touchscreen. Its interface is built on LVGL 9.5 and uses a six-page PageChain, with dedicated Wi-Fi Setup / Recovery, Preset Temporary, and Screensaver screens.

The device is accompanied by a Web UI for playback, stations, behavior settings, and Appearance controls for themes, Main screen backgrounds, station artwork, and an optional user text TTF. The default audio configuration uses an external I2S DAC or amplifier.

The project is based on [e2002/yoradio](https://github.com/e2002/yoradio). The current repository is [Witaliy76/Yoradio_lvgl](https://github.com/Witaliy76/Yoradio_lvgl).

> **Public version:** `0.9.434m-r2-lvgl-beta.2`
> **Current source:** `0.9.434m-r2-lvgl-beta.2-s6.4`
> **Validated board:** ESP32-4848S040

<p align="center">
  <img src="readme/english/device-front.jpg" alt="YoRadio LVGL device" width="450">
</p>

## Design and character

YoRadio is intended as a standalone musical instrument and an object of presence, not an application moved onto a small screen. Music remains the main content: the interface supports listening without demanding constant attention or competing with playback.

The visual language follows a calm Scandinavian hi-fi approach and the character of analog audio equipment: clear typography, restrained composition, quiet accents, and references to classic instruments. This is visible in the Beocord-inspired Visual page and the Amber Hi-Fi fallback palette for the Custom theme. The device is designed to sit naturally in a room without resembling a phone or tablet.

Its normal state is calm. The interface does not flash or ask for a response. Movement appears only where it carries meaning, such as signal activity or a scrolling long title. The screen uses a three-zone instrument composition: technical status at the top, the listening object in the center, and quiet technical details and signs of signal life below.

Each page has a distinct role: Main is for listening, Visual for atmosphere, Info for device state, Stations for selection, Weather for the surroundings, and Settings for functional control. No page tries to become a universal menu or duplicate the others.

## Main features

- Internet radio playback: MP3, AAC, FLAC, OGG/Vorbis, and Opus.
- Six-page LVGL 9.5 touchscreen interface: Info, Main, Visual, Stations, Weather, and Settings.
- Paged station list with eight visible rows.
- Weather with current conditions and a forecast from OpenWeatherMap.
- Beocord-inspired Visual with two channels and eight signal segments per channel.
- Preset Temporary for quick access to eight saved stations.
- Analog Screensaver for idle operation.
- Web UI for playback, stations, settings, Appearance, and firmware updates.
- Dark, Light, and Custom themes, station artwork, and separate Main backgrounds for each theme.
- Compile-time RU / EN / PL / SK interface localization.
- Scalable TTF fonts and Tabler icons with RU / EN / PL / SK support; optional user replacement of normal text via Appearance.
- Optional AI Layer as a quiet information layer over music.

## Change history

### 03 September 2026 — 0.9.434m-r2-lvgl-beta.2-s6.4

Final acceptance of the ESP32-4848S040 board is complete.

**RGB scanout stabilization.** A long-standing defect is fixed: repeatedly saving a preset, or
switching themes, could leave the screen black and then bring the image back shifted horizontally.
The cause was the RGB panel driver policy — the project build of `libesp_lcd` disabled the
automatic scanout restart on VSYNC. The stock ESP-IDF 5.5.5 build with `RESTART_IN_VSYNC=ON` is now
used, and a build-time check prevents the obsolete library override from being reintroduced by
accident. Neither the black screen nor the residual shift reproduces on the device any more.

**Rapid theme switching.** Every tap on the Theme row used to run the whole heavy operation
synchronously — reinitializing the LVGL theme, refreshing styles across the interface, reapplying
pages and writing the choice to a file. A quick burst of switches could therefore trip the watchdog
and reboot the device. Requests are now coalesced: only the final selection is applied, intermediate
ones are discarded, and the file is written once. The Theme row shows the selected value
immediately, and the final background matches the final theme.

**Diagnostics cleanup.** Eight families of debug macros and temporary counters left over from closed
investigations were removed from the sources (about 1070 lines), making the console output much
quieter. PCM/VU telemetry and the touch, AI-layer and LVGL-stack debug switches are retained — they
will be useful for upcoming work.

Public beta remains `0.9.434m-r2-lvgl-beta.2`. This is not a separate public release.

### 30 August 2026 — 0.9.434m-r2-lvgl-beta.2-s5.6

Settings → Display now has a **Performance monitor**: the FPS/CPU overlay can be turned on or off without a reboot. It is off by default, and the choice is persisted. Compile-time LVGL sysmon was not disabled.

Public beta remains `0.9.434m-r2-lvgl-beta.2`. This is not a separate public release.

### 30 August 2026 — 0.9.434m-r2-lvgl-beta.2-s5.5

Appearance can upload a user **TTF** for normal interface text (512 KiB maximum). A reboot is required to apply it; the live font does not hot-swap, and the device does not reboot by itself. If the file is missing or rejected, factory text remains. Play and PT Sans samples are in the repository and are not embedded in firmware.

Public beta remains `0.9.434m-r2-lvgl-beta.2`. This is not a separate public release.

### 29 August 2026 — 0.9.434m-r2-lvgl-beta.2-s5.3

Main now uses factory JPEG backgrounds and browser-normalized user JPEGs. That reduces the LittleFS footprint. After a theme change or background upload, the RGB frame recovers automatically.

Public beta remains `0.9.434m-r2-lvgl-beta.2`.

### 29 August 2026 — 0.9.434m-r2-lvgl-beta.2-s4.8d

A major modernization of YoRadio's graphics stack is complete.

The interface now uses **LVGL 9.5**, and the ESP32-4848S040 display uses a direct **esp_lcd** path without Arduino_GFX. Appearance, navigation, and device behaviour were preserved through the change.

Fonts now use **TTF/TinyTTF**: text and icons scale at runtime instead of a separate generated font for each size. Interface languages **RU / EN / PL / SK** are supported.

The same work also includes a series of display, page-transition, Station Artwork, and RGB-panel fixes and optimizations.

Public beta remains `0.9.434m-r2-lvgl-beta.2`; current sources continue active development.

### 13 August 2026 — build environment update

The project now uses **PIOArduino 55.03.311**, Arduino-ESP32 **3.3.11**, and ESP-IDF **5.5.5**.

The system libraries YoRadio needs are stored and linked by the project itself. They support more robust networking and playback of demanding streams, including FLAC, and keep TLS and the RGB display working as a matched set.

Manual replacement of files inside `.platformio` is no longer required. A normal build is PlatformIO → **Build**.

### 28 July 2026 — 0.9.434m-r2-lvgl-beta.2

First public beta of YoRadio LVGL for ESP32-4848S040.

Earlier alpha builds were internal only and were not released separately.

## Supported hardware

ESP32-4848S040 is currently the only fully supported and verified board. Other hardware profiles are in development. Each supported board uses a dedicated guide for wiring, ready-to-flash packages, and hardware-specific details.

| Item | Value |
|---|---|
| Board | ESP32-4848S040 |
| Module | ESP32-S3-WROOM-1-N16R8 |
| Flash | 16 MB |
| PSRAM | 8 MB |
| Display | ST7701S, 480×480 |
| Touch | GT911 over I2C |

→ **[ESP32-4848S040 board guide](README_4848S040_english.md)** — specifications, audio wiring, configuration, ready-to-flash packages, and board details.

## Interface

The six pages form a ring and are changed with a horizontal swipe:

```text
Info ↔ Main ↔ Visual ↔ Stations ↔ Weather ↔ Settings
```

### Main

Main is the playback screen: station artwork, station and track metadata, playback controls, volume level, stream information, and the lower information area. The optional AI line uses the lower area without covering metadata or controls. Presence Rail provides restrained signal activity, and each color theme may have its own optional background.

<p align="center">
  <img src="readme/english/main-screen-guide.png" alt="Main playback screen" width="450">
</p>

### Info

Info presents technical and playback information: network and Wi-Fi state, firmware and chip details, uptime, display and LVGL versions, heap, and PSRAM use.

<p align="center">
  <img src="readme/english/info-screen-guide.png" alt="System information screen" width="450">
</p>

### Visual

Visual is a digital reconstruction of a classic indicator mechanism inspired by vintage Beocord instruments. It is not affiliated with or endorsed by Bang & Olufsen.

The display follows the decoded audio signal through two independent channels. Each channel has eight segments spanning −20 dB to +5 dB; the final three segments form the red overload zone. The ballistics use a fast rise, approximately 0.12 seconds of peak hold, and a controlled release. The level combines the signal body with limited activity correction and transient emphasis, so the display responds to musical energy rather than isolated spikes alone.

Strongly compressed radio streams may keep the segments within a narrow range. That is a normal reflection of the stream dynamics, not a Visual fault. The indicators operate only while a stream is playing.

<p align="center">
  <img src="readme/english/visual-screen-guide.png" alt="Dual-channel Visual meter" width="450">
</p>

### Stations

Stations shows the station list, the current position and total count, the active row, and the station currently playing. Its paged renderer displays eight rows at a time. Swipe vertically to browse, tap to return to Main, or swipe horizontally to move through the page carousel.

<p align="center">
  <img src="readme/english/station-screen-guide.png" alt="Station browser" width="450">
</p>

### Weather

Weather shows current conditions, feels-like temperature, wind, humidity, pressure, precipitation, hourly points, and a three-day forecast from OpenWeatherMap.

The weather network path retries failed name resolution or connections up to three times in one update cycle. It tries the system DNS first, then Cloudflare at `1.1.1.1`, and Quad9 at `9.9.9.9`. Addresses already attempted in the same cycle are deduplicated, and a working IP from current conditions is preferred for the forecast request. This reduces failures caused by temporary router or provider DNS problems, but it does not guarantee uninterrupted service.

<p align="center">
  <img src="readme/english/weather-screen-guide.png" alt="Weather forecast screen" width="450">
</p>

### Settings

Settings provides direct access to Display brightness and theme, Presence Rail, Resume on Startup, Sleep Timer and its end action, and Wi-Fi setup. Tap a row with an arrow to open its settings.

<p align="center">
  <img src="readme/english/settings-screen-guide.png" alt="Settings screen" width="450">
</p>

## Additional screens and modes

### Preset Temporary

Preset Temporary provides quick access to favorite stations. A downward swipe from the top edge opens it from exactly four pages:

- Info;
- Main;
- Visual;
- Weather.

The gesture does not open Preset Temporary from Stations or Settings because their vertical and service gestures belong to those pages.

There are eight slots. Tap a filled slot to play it; long press to save the current station. After about 15 seconds without activity, Preset Temporary closes and returns to the page from which it was opened.

<p align="center">
  <img src="readme/english/preset-screen-guide.png" alt="Preset Temporary screen" width="450">
</p>

### Display settings

Settings → Display contains brightness, Auto Dim, Performance monitor, and the theme selector.

<p align="center">
  <img src="readme/english/display-settings-guide.png" alt="Display settings" width="450">
</p>

### Screensaver

Screensaver is a full-screen analog clock for idle operation. Its behavior is configured in the Web UI, and a screen tap exits it.

<p align="center">
  <img src="readme/english/screensaver-guide.png" alt="Analog clock screensaver" width="450">
</p>

### Sleep Timer and Sleep Device

Sleep Timer can stop the radio or use Sleep Device as its end action. When Sleep Device is confirmed, playback stops, state is flushed, the display and backlight turn off, and the board enters Deep Sleep. The touch panel does not wake the device. Start it again with Reset or by reconnecting power.

<p align="center">
  <img src="readme/english/sleep-device-guide.png" alt="Sleep Device confirmation" width="450">
</p>

## Web UI and Appearance

Open `http://<device-IP>/` on the local network. The IP address is shown on Info. The Web UI provides playback controls, the station list, behavior settings, Appearance, and firmware/filesystem updates.

<p align="center">
  <img src="readme/english/webui-appearance-entry.png" alt="Web UI Appearance entry" width="700">
</p>

### Station Artwork

Automatic artwork retrieval from a radio stream is not currently supported. Artwork is assigned manually to the station that is playing, so a library can be built one station at a time.

The browser accepts common image formats it can decode, then centers and cover-crops the image, resizes it to 120×120, and converts it to the device format. **Choose image** selects a file and shows a preview, **Upload to device** stores the processed image, and **Remove artwork** deletes it.

<p align="center">
  <img src="readme/english/webui-station-artwork-guide.png" alt="Station Artwork setup" width="700">
</p>

### Color Theme and Custom Palette

Dark, Light, and Custom apply immediately and are saved. Dark and Light are built-in palettes. Custom accepts a `theme_custom.txt` file with one `key=#RRGGBB` entry per line, up to 4096 bytes. Lines beginning with `#` are comments. Numeric and Boolean settings such as `theme_dark=true` are also supported.

The complete example is [`src/src/lvgl_ui/theme/theme_custom.example.txt`](src/src/lvgl_ui/theme/theme_custom.example.txt), and the key reference is [`src/src/lvgl_ui/theme/THEME.md`](src/src/lvgl_ui/theme/THEME.md).

**Upload & Apply** stores and immediately activates the palette. **Remove custom palette** deletes the uploaded file and restores the built-in **Amber Hi-Fi** fallback. These operations affect Custom only; Dark and Light remain unchanged.

<p align="center">
  <img src="readme/english/webui-color-theme-guide.png" alt="Custom color theme setup" width="700">
</p>

### Main Screen Backgrounds

Background images are used only on Main. Dark, Light, and Custom have independent slots, so each theme can keep a different image.

The browser accepts common image formats and keeps the original aspect ratio. The longest side is limited to 1280 px with no crop, stretch, or upscale. The file is saved as JPEG (quality 0.90) into that theme's user slot. Uploading a background does not change the active theme. **Choose image** prepares a preview, **Upload to device** stores the user JPEG, and **Remove image** clears only that user file. If no user image is present, Main uses the factory JPEG for the theme; if that is also absent, it uses the theme color.

<p align="center">
  <img src="readme/english/webui-main-backgrounds-guide.png" alt="Main Screen Backgrounds setup" width="700">
</p>

<p align="center">
  <img src="readme/english/webui-custom-background-guide.png" alt="Custom theme background setup" width="700">
</p>

### User text font

Normal interface text can be replaced with a **TTF** file (not OTF, and not icons). Upload and remove it in Web UI → Appearance. Maximum size is **512 KiB**. A **reboot** is required after a successful upload; the live font does not change immediately, and there is no automatic reboot. Appearance also offers an optional **Reboot now** button.

If no user file is present or the file is rejected, factory Montserrat remains. Tabler icons are unchanged. Optional Play and PT Sans samples live in [`fonts/samples/`](fonts/samples/); they are not firmware assets and are not copied into LittleFS at build time.

## Differences from upstream

This fork develops YoRadio as a touchscreen device with an LVGL interface and board-specific hardware profiles. Compared with [e2002/yoradio](https://github.com/e2002/yoradio), it adds:

- an LVGL 9.5 interface instead of legacy Canvas screens on the supported board;
- scalable embedded TTF fonts (TinyTTF) instead of a per-size compiled font ladder;
- a six-page navigation ring plus dedicated Boot, Wi-Fi, Preset Temporary, and Screensaver modes;
- a Beocord-inspired Visual with defined signal ballistics;
- Web UI Appearance controls for themes, an editable Custom palette, independent Main backgrounds, station artwork, and an optional user text TTF;
- a weather request path with retries and resolver fallback;
- matched network and TLS library profiles for demanding streams and HTTPS requests during playback;
- LittleFS;
- compile-time RU / EN / PL / SK localization;
- the optional AI Layer in the lower Main information line;
- an external I2S DAC as the primary everyday audio path.

Other board profiles may remain in the source tree, but this public beta covers only verified ESP32-4848S040 scenarios.

## AI Layer

AI Layer is an optional quiet layer over music. It is not an assistant or chat interface and does not try to keep the screen filled with text. Silence is a normal state. It requires an OpenAI-compatible API, key, model, and prompt; without them, YoRadio remains a conventional internet radio.

When the layer has something useful to add, it uses a short line in the lower Main information area beneath the stream details. It does not cover playback controls, volume, or station metadata, and it does not open dialogs. The line stays empty when there is nothing appropriate to show, and AI content is not shown on other pages.

More information:

- [AI Layer in YoRadio](readme_ai_layer_eng.md);
- [how the prompt works](readme_ai_prompt_explained_eng.md).

## Beta limitations

- ESP32-4848S040 is the only publicly supported board.
- Interface language is selected at compile time; there is no runtime language switch.
- Sleep Device exits only through Reset or power reconnection.
- The Web UI is local-network HTTP without HTTPS.
- An external I2S DAC is recommended for full audio output.

## Getting started

1. Prepare an ESP32-4848S040 and a suitable power supply.
2. Ready-to-flash packages are in [`build_bin/4848S040/`](build_bin/4848S040/) for **RU / EN / PL / SK**. Use `firmware.bin` and `littlefs.bin` from the same language folder. The complete address map and Espressif Flash Download Tool guide are in [`build_bin/4848S040/README.md`](build_bin/4848S040/README.md). You may also build from source.
3. Complete Wi-Fi Setup on first start.
4. Open the Web UI at the device IP address.

For DAC wiring, configuration, first start, and touch controls, see the **[ESP32-4848S040 board guide](README_4848S040_english.md)**.

### Source build note

To build:

1. Clone or update YoRadio.
2. Open the project in PlatformIO.
3. Press **Build**.

Nothing else is needed. You no longer have to replace ESP-IDF archives inside `.platformio` by hand — the former instructions for copying `.a` files into `framework-arduinoespressif32-libs` are obsolete and unsupported.

PlatformIO resolves the package pinned in [`platformio.ini`](platformio.ini) on its own: PIOArduino `55.03.311` (Arduino-ESP32 `3.3.11`, ESP-IDF `5.5.5`). The shared PlatformIO framework package stays stock — YoRadio never modifies or overwrites anything inside it.

Instead, YoRadio keeps its own overrides in the repository, under `library!/esp-idf-5.5.5/s3/`. That directory holds seven ESP-IDF archives rebuilt from stock Espressif sources with a YoRadio configuration, plus a `manifest.txt` recording full provenance (versions, commits, SHA256 sums, and the exact configuration delta). It is **not** an ordinary Arduino library: nothing there should be copied, installed, or picked out file by file.

The rest is handled by the build helper `yoradio_build.py`. PlatformIO runs it automatically on every build (via `extra_scripts` in `platformio.ini`) — **never run it by hand**. It detects the target chip, checks the platform, core, and ESP-IDF versions, verifies the SHA256 of all seven archives, and only then puts them on the linker search path together with the link options that set requires.

Validation is all-or-nothing: if all seven archives match, the full optimized YoRadio profile is used; if anything at all fails to match, none of it is used. In that case the build does not stop — the helper prints a warning and builds against the complete stock ESP-IDF library set. This fallback is intended behaviour rather than an error, but it is not equivalent to the optimized profile: the resulting firmware has stock ESP-IDF networking and TLS characteristics.

The set matters for three practical device characteristics.

The custom **LwIP** profile supports long playback of demanding network streams, including FLAC and high-bitrate stations, by reducing the likelihood of network stalls and buffering failures. It cannot guarantee uninterrupted playback because station and network quality still matter.

The custom **mbedTLS** profile is not merely an HTTPS switch. AI Layer can make a TLS request while LVGL and the audio decoder are active. With the stock profile, the TLS handshake could fail when a sufficiently large contiguous internal-memory block was unavailable. The dynamic-buffer mode lowers those requirements, while a guard skips a request if the available block is still too small. The archives form one matched profile, together with their link options, and must not be replaced individually with arbitrary versions.

The rebuilt **esp_lcd** is what the RGB display needs: the automatic per-VSYNC RGB panel restart is disabled in it, and YoRadio's own Display layer owns resynchronisation instead. Stock `esp_lcd` cannot reproduce that behaviour.

- ESP32-S3 and ESP32-P4 are separate profiles. A custom archive set is currently adopted for S3 only; a P4 build uses stock ESP-IDF libraries throughout and never inherits the S3 archives.
- The Windows build is device-verified. Building with the local overlay on Linux/macOS has not been exercised yet — that is an open portability item, not a known firmware problem.
- All of the above applies to source builds only. Ready-to-flash packages in `build_bin/` are already built with the required library set.
- Select the interface language in `src/myoptions.h` with `L10N_LANGUAGE`: `RU`, `EN`, `PL`, or `SK`. There is no runtime language switch.
- A normal build embeds two ready TTF files from [`src/src/lvgl_ui/fonts/`](src/src/lvgl_ui/fonts/) (`readme_fonts.md`) in application Flash. You do not generate fonts by hand, install fontTools, or upload a factory TTF through LittleFS. Optional user text is uploaded separately through the Web UI (`/fonts/user.ttf`). Samples in [`fonts/samples/`](fonts/samples/) are not part of the firmware.

## Credits

- **e2002** — original YoRadio project;
- **Wolle (schreibfaul1)** — AudioI2S;
- **Maleksm** (4pda.to) — AudioI2S improvements;
- **moononournation** — Arduino_GFX, historical Type9/parity provenance;
- **LVGL** — interface graphics engine.

## License and authors

This project is based on [YoRadio](https://github.com/e2002/yoradio) and is distributed under the **GNU General Public License v3 or later**. See [`LICENSE`](LICENSE).

Third-party components are listed in [`NOTICE`](NOTICE). GPL v3 requires distributors of compiled firmware to provide access to the corresponding source; this repository and its `platformio.ini` instructions provide that source context.

## Feedback

Questions, bug reports, and proposals are welcome through [Issues](https://github.com/Witaliy76/Yoradio_lvgl/issues) and [Pull Requests](https://github.com/Witaliy76/Yoradio_lvgl/pulls) in [Witaliy76/Yoradio_lvgl](https://github.com/Witaliy76/Yoradio_lvgl).
