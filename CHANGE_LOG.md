# Changelog

Intermediate releases inside a major Beta. Major milestones stay in `README.md`.

## 0.9.434m-r2-lvgl-beta.3.04 — 2026-10-31

- Added a compile-time Board Contract and board selection through `YORADIO_BOARD`.
- Recorded the ESP32-S3 4848S040 factory wiring in one BoardProfile.
- Made `kWiring` the single effective pin configuration.
- Added Custom Wiring: 28 optional GPIO overrides, with compile-time GPIO and conflict checks.
- Left the external I2S DAC user-configurable.
- Kept the current display and touch drivers working through a temporary compatibility bridge.
- Documented the contract in the platform, board, profile, 4848S040 and compatibility READMEs.
- Confirmed display, touch and radio on the factory 4848S040 wiring.
- SD is not restored. SharedResources is not implemented. No additional board is supported yet.

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
