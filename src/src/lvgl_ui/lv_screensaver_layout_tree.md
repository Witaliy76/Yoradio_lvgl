# LVGL Screensaver Overlay — layout tree (SSCLK-P0 / P0A)

> **Module:** `lv_screensaver.cpp` / `lv_screensaver.h`  
> **Stage slice:** SSCLK-P0 — rotation foundation; **SSCLK-P0A** — pivot cleanup + clipping fix  
> **ScreenType:** Overlay on `lv_layer_top()` — **not** `ILvglScreen`, **not** PageChain

---

## 1. Purpose

Full-screen screensaver overlay shown when `displayMode_e == SCREENSAVER`.  
P0 validates LVGL 8.3 `lv_img` transform on target. P0A fixes scattered pivot dots and hand clipping.

---

## 2. Ownership

| Owner | Objects / state |
|-------|-----------------|
| `lv_screensaver.cpp` | `s_ss_root`, `s_clock_layer`, three `lv_img` hands, **one** `s_center_cap`, angle cache, `s_clock_center_x/y` |
| Flash (asset) | `ssclk_hand_probe_map[]` in `assets/ssclk_hand_probe.c` |
| Core (unchanged) | Idle ticks, wake, `network.timeinfo`, `CLOCK` request |

All `lv_*` calls: **DspTask only**.

---

## 3. Object tree

```
lv_layer_top()
└── s_ss_root                    [lv_obj, full-screen, fixed, screensaver_background]
    └── s_clock_layer            [lv_obj, full viewport W×H, transparent, padding 0]
        ├── s_hand_hour          [lv_img, probe sprite, z-bottom]
        ├── s_hand_minute        [lv_img, probe sprite]
        ├── s_hand_second        [lv_img, probe sprite, z-top of hands]
        └── s_center_cap         [lv_obj circle 10×10, LV_ALIGN_CENTER, does NOT rotate]
```

**One-cap rule:** exactly **one** center cap on screen — LVGL object only; probe sprites must **not** contain hub dots or caps.

**Z-order:** creation order = paint order (hour → minute → second → cap).

---

## 4. Canonical clock center (P0A)

Set once per `screensaverShow()` from runtime viewport:

```
s_clock_center_x = viewport_width / 2
s_clock_center_y = viewport_height / 2
```

Example 4848S040: **(240, 240)** for 480×480.

All hands use `setupProbeHandPlacement()` — no per-hand coordinate copies.

---

## 5. Probe sprite convention (P0A)

| Property | Value |
|----------|-------|
| Format | `LV_IMG_CF_TRUE_COLOR_ALPHA` (RGB565 LE + alpha) |
| Dimensions | **13 × 80** px (odd width → exact center pixel at x=6) |
| Pivot (sprite) | **(6, 79)** — bottom center |
| Orientation | Points to **12 o'clock** at angle 0 |
| Shaft | Symmetric 3 px wide (x=5,6,7), straight to pivot |
| Forbidden in sprite | Decorative hub ellipse, tail, embedded cap/dot |

**P0A root cause (scattered dots):** old 12×80 sprite had asymmetric decorative ellipse at y=71–76 (x=2..9). Three rotated copies produced three orbiting dots; plus fixed `s_center_cap` = fourth dot.

---

## 6. Hand placement helper

For each hand (`setupProbeHandPlacement`):

1. `lv_img_set_src(hand, &s_hand_probe_dsc)`
2. `lv_obj_set_pos(hand, s_clock_center_x - 6, s_clock_center_y - 79)`
3. `lv_img_set_pivot(hand, 6, 79)`
4. `lv_img_set_antialias(hand, true)`
5. `lv_img_set_angle(hand, 0)` — updated later via angle cache

---

## 7. Transform clipping margin (P0A)

| Metric | Old (160×160 layer) | New (480×480 viewport) |
|--------|----------------------|------------------------|
| Clock center | (80, 80) | (240, 240) |
| Max opaque radius from pivot | 79 px | 79 px |
| Min distance center → edge | **80 px** | **240 px** |
| Clipping margin (edge − radius) | **1 px** (clips + AA) | **161 px** |
| Required minimum margin | 4 px | ✓ satisfied |

Old layer: hand tip at 6 o'clock reached y=159 in 160 px layer → clipped.  
New full-viewport layer eliminates clipping for current probe length.

---

## 8. Angle formulas (tenths of degree)

| Hand | Formula |
|------|---------|
| Hour | `(tm_hour % 12) * 300 + tm_min * 5 + tm_sec / 12` |
| Minute | `tm_min * 60 + tm_sec` |
| Second | `tm_sec * 60` |

Update: `lv_img_set_angle()` only when angle changed.

---

## 9. Time / CLOCK path

```
network.ticks() → display.putRequest(CLOCK)
  → Display::loop() (_mode == SCREENSAVER)
  → screensaverRefreshClock()
  → applyHandAnglesFromTimeinfo()
```

No module-local timer.

---

## 10. Lifecycle

**Show:** `screensaverHide()` → create tree → apply angles  
**Hide:** `lv_obj_del(s_ss_root)` → null handles → reset angle cache

---

## 11. Deferred

Production backgrounds, dial/bezel, YORADIO, date, dim, drift, encoder wake, theme tokens.

---

## 12. Related files

| File | Role |
|------|------|
| `lv_screensaver.cpp` | Implementation |
| `assets/ssclk_hand_probe.h/.c` | Probe sprite |
| `assets/ssclk_hand_probe_source.png` | Regeneration source |
