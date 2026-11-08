# Changelog

Intermediate releases inside a major Beta. Major milestones stay in `README.md`.

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
