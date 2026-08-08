Author: Witaliy76 - https://github.com/Witaliy76

# Boot screen — LVGL object tree (`scr_boot`)

**Purpose / Назначение:**
**English:** Static boot UI for `LvglBootScreen` in `scr_boot.cpp`. Shows the YoRadio logo, a one-line status label, and an indeterminate shuttle indicator while firmware starts. Not a carousel page; uses a fixed dark theme independent of runtime palette presets. May be auto-deleted by LVGL during the PageChain handoff.
**Русский:** Статичный загрузочный экран `LvglBootScreen` из `scr_boot.cpp`. Показывает логотип YoRadio, одну строку статуса и indeterminate-бегунок пока стартует прошивка. Не является страницей карусели; использует фиксированную тёмную тему независимо от runtime пресетов. Может быть auto-deleted LVGL при handoff в PageChain.

**Source of truth:**
`scr_boot.cpp`:
- `LvglBootScreen::create()` — orchestration skeleton
- Private static builders: `create_root_column`, `create_logo`, `create_status`, `create_indeterminate_bar`
- `startIndeterminateAnim()` / `shuttleAnimExec()` — animation pipeline
- `setStatusUtf8()` — status update

**Maintenance / Поддержка:**
After any of the following, refresh this document:
- hierarchy or creation order changes;
- spacer gap values change;
- logo selection logic changes;
- status buffer or long-mode changes;
- animation parameters change;
- lifecycle contract changes.

---

## Static object tree

```
_screen  (fixed dark bg; not scrollable)
└── _root  (flex COLUMN; center align; pad=0; pad_row=0; LV_ALIGN_CENTER offset kRootLiftY)
    ├── _img_logo                  logo bitmap (small or medium)
    ├── spacer_logo_status         transparent; h=kGapLogoToStatus (24 px)
    ├── _lbl_status                "Starting..."; LONG_DOT; fixed color kBootFixedStatusText
    ├── spacer_status_bar          transparent; h=kGapStatusToBar (38 px)
    └── _prog_track                indeterminate bar track; layout=0 (no flex)
        ├── _prog_glow             halo layer; IGNORE_LAYOUT; same x as shuttle
        └── _prog_shuttle          core streak; IGNORE_LAYOUT; animated x
```

---

## Root-allocation fallback

If `create_root_column()` fails to allocate `_root`, all children are attached directly to `_screen`:

```
_screen
├── _img_logo
├── spacer_logo_status
├── _lbl_status
├── spacer_status_bar
└── _prog_track
    ├── _prog_glow
    └── _prog_shuttle
```

In this case the column block is not centered via `LV_ALIGN_CENTER + kRootLiftY`. This is an allocation-failure fallback, not a design path.

---

## Creation order

9 object creation call sites (verified):

| # | Object | Creator |
|---|--------|---------|
| 1 | `_screen` | `create()` |
| 2 | `_root` | `create_root_column()` |
| 3 | `_img_logo` | `create_logo()` |
| 4 | `spacer_logo_status` | `create_status()` via `create_transparent_spacer()` |
| 5 | `_lbl_status` | `create_status()` |
| 6 | `spacer_status_bar` | `create_status()` via `create_transparent_spacer()` |
| 7 | `_prog_track` | `create_indeterminate_bar()` |
| 8 | `_prog_glow` | `create_indeterminate_bar()` (only if track succeeded) |
| 9 | `_prog_shuttle` | `create_indeterminate_bar()` (only if track succeeded) |

Spacer objects are transparent 1 × N px flex children; not stored as class members.

---

## Logo asset selection

```
if (kBootLogoForceSmallAsset || screen_width <= kBootLogoSmallAssetMaxScreenW)
    → s_boot_logo_dsc_small   (compact panels, e.g. JC3248 320 px)
else
    → s_boot_logo_dsc_medium  (wide panels, e.g. 4848S040 480 px)

kBootLogoSmallAssetMaxScreenW = 320
kBootLogoForceSmallAsset      = false  (set true for small-asset preview)
```

Both descriptors are compile-time constants in `scr_boot.cpp`. No runtime scaling (`lv_image_set_scale`) is applied.

---

## Status label and external buffer

- `_status_text[128]` is a member of `LvglBootScreen` — owned by the screen instance.
- `lv_label_set_text_static(_lbl_status, _status_text)` — LVGL stores only the pointer, no copy.
- Buffer must stay alive while `_lbl_status` exists.
- Initialized to `kStrStarting` (`"Starting..."`) on `create()` via `strncpy`.
- Updated via `setStatusUtf8()` with deduplication (`strcmp`) to avoid redundant redraws.
- `LV_LABEL_LONG_DOT` prevents circular scrolling (stable status, lower relayout pressure).

---

## Indeterminate track structure

```
_prog_track (bar_w × kBarHeight px; layout=0)
├── _prog_glow    (shuttle_w × (kBarHeight+2) px; IGNORE_LAYOUT; y centered in track)
└── _prog_shuttle (core_w × (kBarHeight-1) px; IGNORE_LAYOUT; y centered in track)
```

`layout=0` on `_prog_track`: theme default flex would center children and fight `lv_obj_set_x()`. With `layout=0` the animator controls x directly.

LVGL 9 clips children to parent bounds; glow/shuttle exit the track completely at x = bar_w before the infinite repeat resets to x = 0.

### Geometry formulas

```
bar_w     = screen_width × kBarWidthPercent / 100
shuttle_w = max(kShuttleMinWidth, bar_w × kShuttleWidthPercent / 100)
x_max     = bar_w  (shuttle exits right edge, then jumps to 0)

glow_h    = kBarHeight + kGlowExtraHeight  (=7 px)
core_w    = max(kShuttleCoreMinWidth, shuttle_w × kShuttleCoreWidthPercent / 100)
core_h    = max(kShuttleCoreMinHeight, kBarHeight - 1)
glow_y    = (kBarHeight - glow_h) / 2
core_y    = (kBarHeight - core_h) / 2
```

---

## Animation pipeline

`enter()` triggers:
1. `lv_obj_update_layout(_screen)` — ensures `_prog_track` has a real width before geometry is read.
2. `startIndeterminateAnim()` — stops any prior animation, re-reads geometry, starts new one.

`shuttleAnimExec(var, x)` callback (static, registered as `lv_anim_exec_xcb_t`):
- Sets `lv_obj_set_x` on both `_prog_glow` and `_prog_shuttle` to the same `x`.
- Glow and shuttle share x so left edges stay aligned (no centering of core inside glow).

Animation parameters:
```
values:   0 → x_max (= bar_w)
time:     kShuttleAnimMs (1800 ms)
playback: 0 (LTR only; no bounce back)
repeat:   LV_ANIM_REPEAT_INFINITE
path:     lv_anim_path_ease_in_out
initial:  shuttleAnimExec(this, 0) called before lv_anim_start to set x=0 immediately
```

The indicator is **not** a determinate progress bar — `onBootSignal()` is a no-op stub; queue signals do not move the shuttle.

---

## Fixed-dark visual policy

Boot uses compile-time color literals (`lv_color_hex`, `lv_color_make`) — not `yoradio_palette()`.

```
kBootFixedBackground  = #000000
kBootFixedStatusText  = #CCCCCC
```

Dark/Light/Custom runtime theme preset changes do **not** affect Boot. `liveReapplyTheme()` is not implemented on this screen. This is intentional: the Boot logo was designed on a dark background.

---

## Enter/exit/update lifecycle

| Method | Action |
|--------|--------|
| `create()` | Build full object tree; initialize `_status_text` |
| `enter()` | `lv_obj_update_layout` + `startIndeterminateAnim()` |
| `update()` | No-op |
| `exit()` | No-op |
| `setStatusUtf8()` | Dedup + update status label via `lv_label_set_text_static` |
| `onBootSignal()` | No-op stub — boot queue callback, indeterminate shuttle does not react |

---

## Auto-delete and destroy contract

Boot screen is loaded via `lv_screen_load_anim(..., auto_del=true)` during PageChain handoff to Main. LVGL will free the object tree automatically.

`destroy()` therefore:
1. Calls `lv_anim_del(this, shuttleAnimExec)` — stops animation before handles become stale.
2. Calls `_nullHandles()` — detaches all stored pointers.
3. **Never** calls `lv_obj_delete(_screen)` or any LVGL deletion.

This is a load-bearing contract — calling `lv_obj_delete(_screen)` here would double-free.

---

## Recreate after stale handle

If `create()` is called when `_screen` already holds a non-null but invalid pointer (i.e., LVGL already freed it via auto-delete):

```
create():
    _screen != nullptr
    → lv_obj_is_valid(_screen) == false
    → _nullHandles()           (clear stale pointers)
    → proceed with normal create
```

This path exists because Boot may be reused across soft-reboots without re-instantiating the C++ object.

---

## Failure states

| Failure | Behavior |
|---------|----------|
| `_screen` allocation failed | early return; no children created |
| `_root` allocation failed | children attached to `_screen` (fallback path) |
| `_img_logo` allocation failed | no logo; rest of column continues |
| `spacer_logo_status` failed | gap missing; status label is still created |
| `_lbl_status` allocation failed | no status text; bar may still appear |
| `spacer_status_bar` failed | gap missing; bar may still appear |
| `_prog_track` allocation failed | `_prog_glow` and `_prog_shuttle` not created; `startIndeterminateAnim()` guards on `_prog_shuttle` |
| `_prog_glow` failed | glow absent; shuttle still works |
| `_prog_shuttle` failed | `startIndeterminateAnim()` returns early (guards on `_prog_shuttle`) |

---

## Shared-helper boundary

Boot spacer, logo, track, glow, and shuttle helpers remain local to `scr_boot.cpp`.

There is no proven cross-screen user with an identical lifecycle and visual contract. The `create_transparent_spacer()` helper is a Boot-private primitive; the clickable footer pill pattern used by Weather/Station/Preset is a separate, unrelated surface.

Shared-helper extraction for Boot-specific code is not warranted until a second real user with the same contract is identified.

---

## Notes / Заметки

- `BOOTREF-A`: structural refactor — UI string kStrStarting, `boot_status_font()` helper, named visual/animation constants, private static builders, `_nullHandles()`, `create_transparent_spacer()`, layout tree documentation. No behavioral changes.
- 9 object creation call sites verified.
- `_status_text` is member-owned; spacers are not stored as members.
- `liveReapplyTheme()` not implemented — Boot is theme-isolated by design.
- Animation parameters (`kShuttleAnimMs`, `kBarWidthPercent`, `kShuttleWidthPercent`) match the spec in `bootlogo.md` / `bootlogo_rus.md`.
