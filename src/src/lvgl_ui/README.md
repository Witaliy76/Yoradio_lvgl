# lvgl_ui — LVGL UI subsystem for YoRadio

All LVGL-related code lives exclusively in `src/src/lvgl_ui/`.
LVGL code must not be mixed with legacy display drivers in `src/src/displays/`.

## Stage 0 — Infrastructure only

The following are **forbidden** until Stage 2:

- Calling `lv_init()`
- Calling `lv_timer_handler()`
- Registering display or input drivers (`lv_disp_drv_register`, `lv_indev_drv_register`)
- Any runtime integration with the current Canvas/DspTask pipeline

## Directory structure

```
lvgl_ui/
├── lv_conf.h      — LVGL build configuration
├── lvgl_ui.h      — Public API header (stub in Stage 0)
├── lvgl_ui.cpp    — Implementation (stub in Stage 0)
├── profiles/      — Display Profile structs (Stage 3+)
├── screens/       — LVGL screen builders (Stage 4+)
├── widgets/       — Custom LVGL widgets (Stage 7+)
└── fonts/         — LVGL font files (Stage 4+)
```
