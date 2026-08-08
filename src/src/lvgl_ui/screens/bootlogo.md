Author: Witaliy76 - https://github.com/Witaliy76

# Boot logo & LVGL Boot screen — reference

> **Language:** English. Russian version: [`bootlogo_rus.md`](bootlogo_rus.md).

This file lives next to `scr_boot.cpp`. It documents the **LVGL Boot** screen (Stage 5.4): bitmap asset, how to swap it, and layout tunables.

---

## 0. Frozen decisions

1. **Boot layout constants** — single block at the top of `scr_boot.cpp` (`Boot layout — single source`); keep all tunables there.
2. **Shuttle bar** — **indeterminate boot indicator only**; must not represent real boot progress or a determinate API.
3. **`onBootSignal()`** — **temporary** compatibility stub while the display queue still calls it; remove or repurpose when the boot contract is cleaned up.
4. **CPU readout during Boot** — any on-screen CPU indicator is **suppressed** while `isLvglBootActive()` (RGB panel `DspCore::loop()` — ST7701 / UEDX / AXS).
5. **`LV_LOG_LEVEL_INFO`** in `lv_conf.h` — **temporary** debug while Boot stabilizes; **revert to `LV_LOG_LEVEL_WARN`** after validation (keep `LV_LOG_TRACE_*` off).
6. **Logo placement (within column)** — the **whole column** (logo + status + bar) is aligned with **`LV_ALIGN_CENTER`** on the boot screen, then shifted by **`kRootLiftY`** (see §4). Not a bare centered image only.
7. **Min Boot dwell (~3 s)** — transition to LVGL Main after `DSP_START` waits until `kLvglBootMinVisibleMs` has elapsed since Boot was shown (`lvgl_ui.cpp`), unless the user already waited longer (e.g. slow WiFi). Non-blocking: `Display::_tryCompleteLvglPlayerHandoff()` in `loop()`. AP path still dismisses Boot immediately.
8. **Vertical rhythm (spacing-only freeze, 2026-03)** — accepted tuning: **`kRootLiftY = -16`**, **`kGapLogoToStatus = 24`**, **`kGapStatusToBar = 38`**. Typography, indicator geometry/colors/animation policy, and logo asset unchanged.

---

## 1. Files

| Role | Path |
|------|------|
| Boot bitmap — medium (wide panels, ex. 480 class) | `assets/yoradio_boot_medium.c` — `image_data_yoradio_boot_medium[]`, **170×149** (source: `bootlogo/yoradio_240.c`) |
| Boot bitmap — small (320-wide) | `assets/yoradio_boot_small.c` — `image_data_yoradio_boot_small[]`, **114×97** |
| Declarations | `assets/bootlogo_assets.h` |
| Which asset is used | `scr_boot.cpp`: if `width <= kBootLogoSmallAssetMaxScreenW` (320) → small, else medium |
| Boot UI | `scr_boot.cpp`, `scr_boot.h` (this folder) |
| When Boot is shown | `lvgl_ui::tryPresentLvglBootOnFirstDspLoop()` → `PageChain::showBoot()` (`../lvgl_ui.cpp`) |

---

## 2. Image format

- **Names vs size:** lcd-image-converter keeps its **project/image name** in comments (e.g. `yoradio_240`). That is **not** the pixel width. Real size is in code: **`170 × 149`** for medium (`YORADIO_BOOTLOGO_MEDIUM_*`), **114 × 97** for small.
- **Encoding:** RGB565 (`uint16_t` per pixel), **no RLE** for the embedded array path.
- **LVGL 9:** static `lv_image_dsc_t` descriptors use `LV_COLOR_FORMAT_RGB565`, `magic = LV_IMAGE_HEADER_MAGIC`, and `stride = width × 2`; the display is explicitly configured as unswapped RGB565.
- **Tool:** [lcd-image-converter](https://github.com/riuson/lcd-image-converter) — preset **Color R5G6B5**, C array output.

**PNG from LittleFS:** not supported by the current LVGL configuration; runtime PNG/JPEG/SVG decoders are disabled. Boot uses static C descriptors and does not depend on LittleFS.

**Scale instead of a second file:** LVGL 9 `lv_image_set_scale(img, z)` uses **256 = 100%**, **128 ≈ 50%**, **77 ≈ 30%**. Set the pivot with `lv_image_set_pivot(img, w/2, h/2)` (source pixel coordinates) so scaling stays visually centered. Trade-off: extra draw cost and softer edges vs. a dedicated downscaled bitmap.

---

## 3. Replacing the boot logo — what must change

Dropping a new file under `assets/` and renaming it is **not** enough. **Three places must stay consistent** with the **actual** image width, height, and pixel buffer symbol:

| Step | What to do |
|------|------------|
| 1. Asset `.c` | Add or replace the compiled `.c` under `lvgl_ui/assets/`. The pixel array must have **external linkage** (not `static`) so `scr_boot.cpp` can link it. Array length = **W × H** `uint16_t` values. |
| 2. `bootlogo_assets.h` | `extern` + **`YORADIO_BOOTLOGO_MEDIUM_W`** / **`_H`** (and/or **`SMALL_*`**) = real dimensions from the converter. |
| 3. `scr_boot.cpp` | `s_boot_logo_dsc_medium` / `s_boot_logo_dsc_small` — keep `.data`, `.w`, `.h`, `data_size` in sync with the chosen buffer. |

**Yes — dimensions matter.** If `W`/`H` or `data_size` do not match the array, LVGL reads past the buffer → garbage or crash.

**Optional:** rename the `.c` file freely (PlatformIO compiles all `.c` under `src/`). Renaming the **C symbol** (`image_data_*`) requires updating the `extern` and `s_boot_logo_dsc.data` to match.

After edits: rebuild and verify on hardware.

---

## 4. Layout & styling — where to edit

**Primary knob:** constants at the top of `scr_boot.cpp` (block `Boot layout — single source`).

| Constant | Meaning |
|----------|---------|
| `kRootLiftY` | Vertical offset of the **entire** column vs screen center (`lv_obj_align(..., 0, kRootLiftY)`); negative moves the block **up**. |
| `kStatusPadMul` | Status label width margin: `frame_padding * kStatusPadMul`. |
| `kGapLogoToStatus` | Pixels between logo and status in the center column. |
| `kGapStatusToBar` | Pixels between status and indeterminate bar in the center column. |
| `kBarWidthPercent` | Track width as % of screen width. |
| `kBarHeight` | Track height (px). |
| `kShuttleWidthPercent` | Shuttle width as % of track. |
| `kShuttleAnimMs` | One **LTR pass** duration (ms), ease in-out; **no** playback / ping-pong (`lv_anim_set_playback_time(..., 0)`). |

**Canonical spacing (accepted):** `kRootLiftY = -16`, `kGapLogoToStatus = 24`, `kGapStatusToBar = 38` — see §0.8.

**Layout structure:** a single centered flex-column container: `logo → spacer → status → spacer → bar`.

**Status font:** `LV_ACTIVE_PROFILE.font_small` — edit the active profile under `../profiles/` (e.g. `lv_profile_4848S040.h`).

**Track / shuttle look:** `apply_track_style()` / `apply_shuttle_style()` in `scr_boot.cpp`.

---

## 5. Behaviour notes

- **Historical Boot → Main artifact note:** the older shared-`Arduino_Canvas` path was stabilized with a full-frame buffer and `full_refresh = 1`. That is not the current 4848S040 topology. Current LVGL 9 uses one 480×160 PSRAM draw buffer (153600 B), `LV_DISPLAY_RENDER_MODE_PARTIAL`, and a synchronous direct flush through Arduino_GFX. Treat the old full-frame observation as migration history, not current troubleshooting guidance.
- **Min visible time:** at least **3 s** before Main after `DSP_START` if the network was fast — see §0.7.
- **Shuttle:** indeterminate only; **not** real progress.
- **`onBootSignal()`:** temporary stub — see §0.3.
- **Status text:** from `Display::loop()` (`BOOTSTRING` / `WAITFORSD` → `bootScreenSetStatusUtf8`).
- **Threading:** all `lv_*` for Boot on **DspTask** only.
