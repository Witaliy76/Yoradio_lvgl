# Changelog

Intermediate releases inside a major Beta. Major milestones stay in `README.md`.

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
