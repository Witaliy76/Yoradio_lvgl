# LVGL Stage 2 — Runtime Design Notes

> **NOTE**
>
> This document contains exploratory architectural notes for
> LVGL runtime bring-up (Stage 2).
>
> These notes are NOT implemented yet and do not represent
> final architectural decisions.
>
> The current project stage is **Stage 1 (LittleFS transition)**.

---

## 1. Recommended runtime-init location

LVGL runtime initialization (`lv_init()`) should happen inside
`Config::init()` **after** the filesystem is mounted and before
`Display::init()` creates the DspTask.

Proposed call site in `src/src/core/config.cpp`:

```
Config::init()
  ├── EEPROM.begin()
  ├── _initHW()
  ├── FS.begin()        ← filesystem mount (currently SPIFFS.begin)
  ├── lv_init()         ← NEW — Stage 2
  └── ... rest of init
```

Rationale:
- `lv_init()` must precede any `lv_disp_drv_register()` or `lv_timer_handler()` calls.
- It has no hardware dependency, only requires heap to be available.
- Placing it in `Config::init()` guarantees it runs once on Core 1 (Arduino `setup()` context) before DspTask starts on Core 0.

Alternative: a dedicated `lvgl_ui::init()` called from `main.cpp::setup()` after `config.init()` and before `display.init()`. This keeps LVGL logic out of `config.cpp`.

---

## 2. Ownership model

### Current ownership

| Resource | Owner | Core |
|----------|-------|------|
| `Arduino_Canvas *gfx` | DspTask | 0 |
| `displayQueue` | DspTask (consumer) / any (producers) | 0 / any |
| `g_frameDirty` | DspTask (reader) / draw functions (writers) | 0 |
| `sdog` (SPI mutex) | DspTask + Audio | shared |

### Proposed LVGL ownership

| Resource | Owner | Core | Notes |
|----------|-------|------|-------|
| `lv_disp_t` | DspTask | 0 | Registered once at init |
| LVGL timers | DspTask | 0 | `lv_timer_handler()` runs in DspTask loop |
| LVGL draw buffers | DspTask | 0 | Allocated from PSRAM |
| `lv_indev_t` (touch) | DspTask | 0 | Read callback polls existing touch driver |

LVGL objects must only be created/modified from DspTask context
(Core 0) or protected by a mutex. In transitional mode (Stage 2–3),
the existing `displayQueue` can proxy LVGL operations.

---

## 3. lv_timer_handler strategy

### Option A — Inside DspTask loop (recommended)

```cpp
void Display::loop() {
    // ... existing queue processing ...

    #if YORADIO_USE_LVGL && YORADIO_LVGL_STAGE >= 2
        lv_timer_handler();  // runs all LVGL timers, animations, redraws
    #endif

    // ... existing dirty-flag flush ...
}
```

Pros:
- Single-threaded, no concurrency issues.
- Naturally rate-limited by `DSP_QUEUE_TICKS` and the 16 ms flush gate.
- Coexists with legacy Canvas rendering.

Cons:
- Tied to DspTask priority (3) and stack (3072 B — may need increase).

### Option B — Separate LVGL timer task

Not recommended for Stage 2. Introduces concurrency between LVGL
and legacy Canvas code sharing the same framebuffer.

### Tick source

LVGL needs a tick source. On ESP32 Arduino 3.x, `millis()` is
suitable:

```cpp
// In lv_conf.h:
#define LV_TICK_CUSTOM     1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
```

This is already configured in the Stage 0 `lv_conf.h`.

---

## 4. Transitional rendering model

During Stage 2, both Canvas (legacy) and LVGL rendering coexist:

```
┌─────────────────────────────────────────────┐
│  DspTask loop()                             │
│                                             │
│  ┌──────────────────┐  ┌─────────────────┐  │
│  │ Legacy Canvas    │  │ LVGL widgets    │  │
│  │ (existing code)  │  │ (new code)      │  │
│  └────────┬─────────┘  └───────┬─────────┘  │
│           │                    │             │
│           ▼                    ▼             │
│    gfxFlushScreen()     lv_timer_handler()   │
│    → gfx->flush()       → flush_cb()        │
│                          → gfx->draw16bitRGBBitmap() │
│                                             │
│  Both write to the same Arduino_Canvas      │
│  framebuffer (480×480×2 = 460 KB in PSRAM)  │
└─────────────────────────────────────────────┘
```

### flush_cb design

The LVGL display driver flush callback should write directly
into the Arduino_Canvas framebuffer:

```cpp
void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *buf) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)buf, w, h);
    markFrameDirty();
    lv_disp_flush_ready(drv);
}
```

This avoids double-buffering and reuses the existing
`g_frameDirty` → `gfxFlushScreen()` pipeline for actual
hardware transfer.

### Screen region partitioning

For gradual migration, legacy widgets and LVGL widgets should
occupy non-overlapping screen regions. A `displayMode_e` flag
can control which renderer owns which area.

---

## 5. PSRAM and buffering risks

### Memory budget (ESP32-S3 N16R8)

| Allocation | Size | Location |
|------------|------|----------|
| Arduino_Canvas framebuffer | 480×480×2 = 460 KB | PSRAM |
| LVGL draw buffer (recommended) | 480×40×2 = 38.4 KB | PSRAM |
| LVGL internal heap (`LV_MEM_SIZE`) | 128 KB | Internal SRAM or PSRAM |
| Audio buffers | ~32 KB | Internal SRAM |

Total PSRAM usage ≈ 660 KB out of 8 MB — comfortable margin.

### Risks

1. **Internal SRAM pressure**: If `LV_MEM_SIZE` uses internal
   SRAM (default), it competes with FreeRTOS stacks and Audio
   buffers. Consider `LV_MEM_CUSTOM=1` with a PSRAM allocator
   or set `LV_MEM_ADR` to a PSRAM region.

2. **DspTask stack overflow**: Current stack is 3072 B. LVGL
   `lv_timer_handler()` can use 1–2 KB of stack for rendering.
   Stack should be increased to at least 8192 B.

3. **Cache coherence**: PSRAM on ESP32-S3 uses cache. Large
   framebuffer writes should be 32-byte aligned for optimal
   DMA performance.

4. **Concurrent PSRAM access**: Audio task (Core 1) and DspTask
   (Core 0) both access PSRAM. The ESP32-S3 OPI PSRAM controller
   handles this at hardware level, but large burst transfers may
   cause stalls.

---

## 6. Display Profiles impact

Current display drivers:

| Driver | Display | Resolution | Interface |
|--------|---------|------------|-----------|
| `displayST7701` | ST7701S | 480×480 | RGB Panel |
| `displayAXS15231B` | AXS15231B | 320×480 | QSPI |
| `displayUEDX48480021` | ST7701S | 480×480 | RGB Panel (round) |

Each driver creates `Arduino_Canvas *gfx` differently. The LVGL
flush callback must be resolution-aware. Proposed approach:

- LVGL display driver registered with actual panel resolution.
- `lv_conf.h` defines `LV_HOR_RES_MAX` / `LV_VER_RES_MAX` as
  the maximum (480). Actual resolution set at registration time.
- Display profiles (`src/src/lvgl_ui/profiles/`) map widget
  layouts to each resolution. Created at Stage 3.

---

## 7. Proposed Stage 2 scope

Stage 2 should be minimal and verifiable:

1. **lv_init()** — call in `setup()` flow after FS mount.
2. **Display driver registration** — create `lv_disp_drv_t`
   with flush callback writing to `Arduino_Canvas`.
3. **lv_timer_handler()** — add to DspTask loop, gated by
   `YORADIO_LVGL_STAGE >= 2`.
4. **Test overlay** — render a small LVGL label ("LVGL OK")
   in a corner, on top of the legacy Canvas output.
5. **Verify coexistence** — confirm legacy widgets and the
   LVGL test label render correctly without artifacts.
6. **Stack increase** — bump DspTask stack to 8192 B.
7. **PSRAM draw buffer** — allocate LVGL draw buffer from PSRAM.

No legacy widgets are replaced. No input drivers registered.
No screens or navigation.

### Acceptance criteria

- Build succeeds for all 4 environments.
- Runtime: legacy UI renders normally.
- Runtime: LVGL test label visible.
- No heap fragmentation or stack overflow.
- `YORADIO_LVGL_STAGE=1` still compiles with zero LVGL runtime.

---

*Document created during Stage 1 preparation. To be reviewed
and refined before Stage 2 implementation begins.*
