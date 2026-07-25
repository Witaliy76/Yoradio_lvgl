Author: Witaliy76 - https://github.com/Witaliy76

# Station screen — LVGL object tree (`scr_station`)

**Purpose / Назначение:**
**English:** Parent → child hierarchy for the Station carousel page (`LvglStationPage`). Chrome (status, header, footer) lives in `scr_station.cpp`; the list subtree, text buffer, overlays, and input pipeline are owned by the compile-time selected renderer (`station_list_active`). Use when reasoning about z-order, renderer choice, paging vs. scroll, or PageChain lifecycle.
**Русский:** Иерархия страницы Station: chrome в `scr_station.cpp`, список и ввод — у renderer'а `station_list_active` (compile-time). Для z-order, выбора renderer, paging vs scroll, lifecycle PageChain.

**Source of truth (code):**
- `scr_station.cpp` / `scr_station.h` — page chrome, count label, footer → Main, PageChain gestures
- `station_list_renderer_select.h` — compile-time dispatch
- `station_list_simple_paged.*` — paged renderer (STATIONPAGED-1/1A)
- `station_list_legacy_scroll.*` — legacy continuous scroll (extracted STATION legacy)

**Maintenance / Поддержка:**
Refresh this doc after hierarchy, renderer API, row geometry, or input pipeline changes.

---

## Compile-time renderer selection (STATIONPAGED-2)

Define in `src/myoptions.h` (pulled via `options.h` → `station_list_renderer_select.h`):

```cpp
// 1: lightweight paged Station list; 0/undefined: legacy continuous scroll
#define STATION_LIST_SIMPLE_PAGED 1
```

| Define | Renderer | Namespace alias |
|---|---|---|
| `1` | `station_list_simple_paged` | `station_list_active` |
| `0` or undefined | `station_list_legacy_scroll` | `station_list_active` |

**Include path:** `scr_station.h` → `station_list_renderer_select.h` → `../core/options.h` → `../../myoptions.h` (via `__has_include`).

`LvglStationPage` holds `station_list_active::Instance _list{}` and calls only `station_list_active::*` lifecycle APIs. No `#if` in `scr_station.cpp` except inside the selector header (single dispatch point).

---

## Root order (chrome — both renderers)

`_screen` flex **COLUMN** (top → bottom):
- `pad_all = LV_ACTIVE_PROFILE.frame_padding`
- `pad_row = kRootRowGap` (8 px)
- not scrollable

**Creation order (load-bearing):**

1. `_status_line.root` + status divider — `create_status_chrome`
2. `header` (local) — `create_header`
3. `_list.list_area` — `station_list_active::create(_list, _screen, pal)`
4. `_hint_area` — `create_hint_band`
5. `installCarouselGesturesOnPageRoot(_screen)`
6. `lv_obj_update_layout(_screen)` — footer affects flex height before first populate
7. `station_list_active::populate(_list)`

---

## Static object tree (chrome + list shell)

```
_screen  (flex COLUMN; pad_all=frame_pad; pad_row=8)
│
├── _status_line.root              wgt_status_line
├── status divider                 h=1; pal.divider
├── header                         flex ROW; local
│   ├── _lbl_title                 "STATIONS"
│   └── _lbl_count                 current / total
│
├── _list.list_area                flex_grow=1; owned by active renderer
│   └── (renderer-specific — see below)
│
└── _hint_area                     footer pill; tap → Main
    └── hint_row (local)
        ├── _lbl_hint_icon
        └── _lbl_hint_text
```

---

## Paged renderer object tree (`STATION_LIST_SIMPLE_PAGED=1`)

Non-scrollable `list_area`; one page label; vertical swipe changes page index.

```
_list.list_area                    NOT scrollable; pad_left=16; pad_top/bottom=8; CLICKABLE
├── lbl_page                       multiline; lv_label_set_text(page buffer); line_space=16
├── focus_row_bg                   highlight band (dynamic)
├── focus_row_accent               left strip (dynamic)
└── current_marker                 speaker glyph (dynamic)
```

**Buffer:** `page_text` — short heap/PSRAM buffer for one page only (`ensurePageTextBuffer`).

**Rows per page:** computed after layout:

```text
rows_per_page = 1 + (content_height - line_height) / row_pitch
```

480×480 profile: **8 rows**, `row_pitch = 41` (25 + 16), `content_height ≈ 321`.

**Input:**
- Vertical swipe on RELEASED → ±1 page (`tryPageStep`), no pixel scroll, no momentum
- SHORT_CLICKED → row tap → `play_station(global_num)`
- Horizontal gestures bubble to PageChain on `_screen`

**Enter:** `onEnter()` → page index from current station; no scroll jump on NEWSTATION.

---

## Legacy renderer object tree (`STATION_LIST_SIMPLE_PAGED=0`)

Scrollable `list_area`; full playlist in one multiline label.

```
_list.list_area                    SCROLLABLE VER; SCROLL_MOMENTUM; scrollbar
├── lbl_list                       lv_label_set_text_static → list_text (full playlist)
├── focus_row_bg
├── focus_row_accent
└── current_marker
```

**Buffer:** `list_text` — PSRAM/heap buffer for entire playlist.

**Input:**
- Continuous vertical scroll + momentum + scrollbar
- SHORT_CLICKED with scroll/finger-travel guards → focus + play
- Suppression arm after scroll-like stroke

**Enter:** `onEnter()` → `scrollToCurrent` centers current row in viewport.

---

## Shared row geometry (both renderers)

```
kStationListFontLineHeight = 25 px
kStationListLineSpace      = 16 px
kStationLinePitch          = 41 px

kListPadLeft = 16; kListPadTop = kListPadBottom = 8

Left gutter: accent (3) | gap (5) | marker (32) | gap (5) | text
```

Paged: row Y = `local_row × 41` (page-local). Legacy: document Y = `(station − 1) × 41`.

---

## Focus vs. current (both renderers)

| | Focus | Current |
|---|---|---|
| State | `focus_station_num` | adapter `current_station_num()` |
| Visual | `focus_row_bg` + `focus_row_accent` | `current_marker` |
| NEWSTATION | unchanged | marker refresh only |
| Enter | paged: page follows current; legacy: scroll to current | same |

---

## PageChain lifecycle (chrome + renderer)

| Event | Chrome | Renderer |
|---|---|---|
| `create()` | build tree; layout; count label | `create` + `populate` |
| `enter()` | status update | `refreshOnActivate` + `onEnter` |
| `update()` | status only | — |
| `exit()` | no-op | — |
| `liveReapplyTheme()` | recolor chrome + dividers | `liveReapplyTheme(_list, pal)` |
| `destroy()` | `lv_obj_del(_screen)` | `releaseAfterTreeDelete(_list)` in `_nullHandles` |
| `releaseAfterAutoDelete()` | `_nullHandles` | `releaseAfterTreeDelete` |

`refreshCurrentStationVisuals()` on page: count label + `station_list_active::refreshCurrentStationVisuals(_list)` — no page jump (paged) / no scroll jump (legacy) on NEWSTATION.

---

## Paged page math (480×480, 8 rows)

```text
page_count      = ceil(total / rows_per_page)
first_station   = page_index × rows_per_page + 1
global_station  = page_index × rows_per_page + local_row + 1
page_index      = (current − 1) / rows_per_page   // on enter
```

Boundaries: stations 8/9, 16/17, etc.

---

## Hint band / footer

Unchanged from chrome layer: `_hint_area` clickable → `goToCarouselPage(MAIN_INDEX)`; horizontal swipe bubbles to PageChain. `wgt_footer_pill` owns pressed/normal visuals.

---

## Localization

User strings in `scr_station.cpp` (`kStr*` / `kFmt*`). Renderer error strings live in respective `.cpp` files.

---

## STATION refactor history (summary)

| Slice | Change |
|---|---|
| Legacy extraction | `station_list_legacy_scroll.*` |
| STATIONPAGED-1 | `station_list_simple_paged.*`; instant page flip |
| STATIONPAGED-1A | 8 rows on 480×480; corrected rows formula |
| STATIONPAGED-2 | `station_list_renderer_select.h`; `STATION_LIST_SIMPLE_PAGED` in myoptions.h |

**Note:** This document is local reference only — not source of truth for selector; code is authoritative. Not intended for source commit unless explicitly requested.
