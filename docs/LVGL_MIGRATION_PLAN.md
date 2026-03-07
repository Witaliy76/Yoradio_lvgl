# LVGL Migration Plan — YoRadio RGB Panels

## Overview

Staged migration of the YoRadio display subsystem from
Arduino_GFX Canvas to LVGL. Each stage is self-contained,
verifiable, and non-breaking.

---

## Stage 0 — LVGL infrastructure

**STATUS: COMPLETED**

Completed items:

- [x] LVGL dependency added (`lvgl/lvgl@~8.3.0` in `platformio.ini`)
- [x] `lv_conf.h` added (`src/src/lvgl_ui/lv_conf.h`)
- [x] `lvgl_ui` skeleton created (`src/src/lvgl_ui/` with headers, stubs, and subdirectories)
- [x] Compile-time stage macros added (`YORADIO_USE_LVGL`, `YORADIO_LVGL_STAGE`)
- [x] Guardrails documented (README in `lvgl_ui/`, stage analysis in `STAGE0_STEP1_ANALYSIS.md`)
- [x] Runtime behaviour unchanged (no `lv_init()`, no `lv_timer_handler()`, no display driver registration)

Branch: `lvgl_stage0`

---

## Stage 1 — LittleFS transition

**STATUS: IN PROGRESS**

Migrate the internal flash filesystem from SPIFFS to LittleFS.
Required before LVGL asset storage (fonts, images) in later stages.

Key tasks:

- Analyze current SPIFFS usage across all subsystems
- Design safe migration path (SPIFFS → LittleFS)
- Implement filesystem switch with backward compatibility
- Validate all subsystems (WebUI, config, playlists, AI, OTA)
- Update partition table labels if needed

Branch: `lvgl_stage1_littlefs`

---

## Stage 2 — LVGL runtime bring-up

**STATUS: NOT STARTED**

Initialize LVGL runtime, register display driver, add
`lv_timer_handler()` to DspTask. Render test overlay on top
of legacy Canvas output.

See: `docs/LVGL_STAGE2_RUNTIME_DESIGN_NOTES.md`

---

## Stage 3 — Display profiles

**STATUS: NOT STARTED**

Create resolution-specific LVGL display profiles for each
supported panel (480×480 RGB, 320×480 QSPI, 480×480 round).

---

## Stage 4 — Screen framework

**STATUS: NOT STARTED**

Implement LVGL screen containers and navigation system.
Begin replacing legacy widget rendering with LVGL equivalents.

---

## Stage 5+ — Widget migration

**STATUS: NOT STARTED**

Gradually migrate individual widgets (metadata, VU meter,
spectrum, clock, playlist) from Canvas to LVGL.

---

*This document is updated as each stage progresses.*
