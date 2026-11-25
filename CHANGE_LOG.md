# Changelog

Intermediate releases inside a major Beta. Major milestones stay in `README.md`.

## 0.9.434m-r2-lvgl-beta.3.10 — 2026-11-25

- Added VIEWE UEDX48480040E-WB-A (GC9503 4" 480×480, FT6336U touch) as a second supported board, built from source: display, touch, SD playback and an external I2S DAC; wiring guide in `README_UEDX48480040.md`.
- Reworked the hardware layer for several boards: board profiles with tested defaults and declared shared pins, one place that maps a board to its display and touch drivers; retired the `DSP_MODEL` / `TS_MODEL` selectors.
- Renamed the build environment to `S3_N16R8` (one environment for the ESP32-S3 N16R8 family) and started tracking `dependencies.lock`.
- Renamed the RGB panel driver to `display_rgb_panel`; the Serial log tag `[ST7701]` is now `[rgb_panel]`.
- Verified on hardware: 4848S040 unchanged (display, touch, HTTPS radio, SD); VIEWE display, touch, SD and audio.

## 0.9.434m-r2-lvgl-beta.3.09 — 2026-11-20

- Added MP3 and FLAC playback from FAT and exFAT SD cards on the 4848S040.
- Added a Media Library to WebUI and LVGL for browsing stations and folders without interrupting playback.
- Added folder playback with automatic track advance, repeat modes and shuffle.
- Increased the Arduino loop task stack from 8 KiB to 16 KiB for HTTPS radio connections.

## 0.9.434m-r2-lvgl-beta.3.08 — 2026-11-08

- Validate display, touch and I2S GPIO values before narrowing, rejecting invalid/reserved pins and conflicting claims.
- Distinguish confirmed GT911 release from missing reports; preserve touch state across report gaps and block false presses after errors.
- Apply Flip Touch on DspTask between polls, deferring orientation changes while a contact is held or release remains uncertain.

## 0.9.434m-r2-lvgl-beta.3.07 — 2026-11-04

- Added explicit Square480 UI profile selection in BoardDefaults and separated physical display geometry from factory UI metrics.
- Migrated screen, Main JPEG background and WebUI geometry consumers to ResolvedUiMetrics and retired the old LVGL profile bridge.

## 0.9.434m-r2-lvgl-beta.3.06 — 2026-11-04

- Migrated GT911 touch to the official Espressif driver with TouchPort and LVGL adapter.
- Fixed touch release coordinates and removed legacy TAMC touch code.
- Added WebUI Debug Touch diagnostics and preserved screensaver wake-only input.
- Fixed Main button pressed feedback and clipping across Dark, Custom and Light themes.
- Passed the 4848S040 hardware touch gate (9/9) and verified Main button feedback.

## 0.9.434m-r2-lvgl-beta.3.05 — 2026-11-01

- Added typed display configuration and centralized ST7701 panel settings.
- Added guarded RGB PCLK transitions for safe flash writes.
- Removed legacy ST7701 display GPIO aliases.
- Fixed playlist upload so failed or malformed uploads preserve the active playlist.
- Verified reboot, screensaver, display sleep/wake and flash writes on hardware.

## 0.9.434m-r2-lvgl-beta.3.04 — 2026-10-31

- Added compile-time Board Contract and board selection for ESP32-S3 4848S040.
- Centralized factory and effective hardware wiring.
- Added optional Custom Wiring with compile-time GPIO validation.
- Preserved existing display, touch and user-configurable I2S audio.
- Verified display, touch and radio operation on the 4848S040.

## 0.9.434m-r2-lvgl-beta.3.03 — 2026-10-27

- Accepted mixed Arduino + ESP-IDF baseline.
- Fixed RGB display cold-start instability.
- Added RGB flash-write protection.
- Extended protected flash writes to NVS and LittleFS paths.
- Restored the accepted watchdog policy.
- Removed automatic RGB restart/resync recovery.

## 0.9.434m-r2-lvgl-beta.3.02 — 2026-10-06

- Grew the S3 LVGL memory pool to 384 KiB for Weather to Main headroom.
- Allocated render-layer draw buffers from the PSRAM heap.
- Freed the previous carousel page before building the next one.

## 0.9.434m-r2-lvgl-beta.3.01 — 2026-10-05

- Stopped the Weather pressure reading from clipping the trailing g in mmHg.
