# Preset Temporary screen — LVGL object tree (`scr_preset`)

## Purpose / Назначение

**English:** Parent → child hierarchy for the Preset Temporary screen (`LvglPresetScreen`), built by private static layout builders called from `create()`. Documents object tree, creation order, interaction pipelines, timer ownership, theme reapply, and partial-allocation behavior. PRESETREF-A structural refactor; product semantics unchanged.

**Русский:** Иерархия объектов Preset Temporary, билдеры в `scr_preset.cpp`, порядок создания, события, таймеры, тема. PRESETREF-A — структурный refactor без изменения поведения.

## Source of truth

- Object tree and creation order: `LvglPresetScreen::create()` → `create_title()` → `create_preset_rows()` → `create_footer()`.
- Row content: `_updateRowContent()`, `preset_store`, `station_list_adapter`.
- Events: `_rowEventCb`, `_helperEventCb`.
- Timers: `_countdown_timer`, `_feedback_timer`.
- Theme: `_applyAllColors()` / `liveReapplyTheme()`.

## Maintenance / Поддержка

After any of the following, refresh this document:

- new/removed LVGL object in a row or footer;
- column width constants (`kSlotW`, `kNumW`);
- footer geometry or style contract (`wgt_footer_pill` API changes);
- timer or dismiss semantics;
- font resource changes.

---

## Static object tree

```
_screen                          (flex COLUMN; pad=frame_padding; pad_row=kRowGap)
├── _title                       (label "ПРЕСЕТЫ"; kFontTitle; centered; not clickable)
├── _rows[0]                     (card; ROW flex; clickable; kRowH=44)
│   ├── _slot_labels[0]          ("1"; kSlotW; kFontSlotNumber M16; text_secondary)
│   ├── _vdiv_lines[0]         (kDividerWidth × kDivH; divider; kDividerOpacity)
│   ├── _num_labels[0]         ("#N" / "--"; kNumW; profile font_normal; accent roles)
│   └── _name_labels[0]        (name; kFontStationName; flex_grow; LONG_DOT one line)
├── _rows[1] … _rows[7]         (same children, slot index 1…7)
└── _helper_box                  (clickable footer pill; kFooterBoxH=32; dismiss tap)
    └── _helper                  (countdown + feedback text; kFontFooter; not clickable)
```

**Runtime object count (full allocation):** 44 LVGL objects
(1 screen + 1 title + 8×(row + slot + vdiv + num + name) + helper_box + helper).

**Screen type:** `ScreenType::Temporary` — opened via `PageChain::showTemporary()`; dismiss returns to **origin carousel slot**.

---

## Creation order

`create()` orchestration (root setup inline, then builders):

| Step | Builder | Creates |
|------|---------|---------|
| 1 | `create()` inline | `_screen` |
| 2 | `create_title()` | `_title` |
| 3 | `create_preset_rows()` → `create_preset_row(0..7)` | each row subtree |
| 4 | `create_footer()` | `_helper_box` → `_helper` |

Per-row child order inside `create_preset_row()`:

```text
row → slot label → vertical divider → station-number label → station-name label
```

**9 source creation call-site groups** (unchanged equivalence):

1. `lv_obj_create` screen
2. `lv_label_create` title
3–7. per row: `lv_obj_create` row, `lv_label_create` slot, `lv_obj_create` vdiv, `lv_label_create` num, `lv_label_create` name
8–9. `lv_obj_create` helper_box, `lv_label_create` helper

---

## Row structure

| Column | Member | Width | Font | Color role |
|--------|--------|-------|------|------------|
| Slot № | `_slot_labels[i]` | `kSlotW = 24` | `kFontSlotNumber` (M16) | `text_secondary` |
| Divider | `_vdiv_lines[i]` | `kDividerWidth = 1` | — | `divider` @ `kDividerOpacity` |
| Station № | `_num_labels[i]` | `kNumW = 56` | `preset_station_number_font()` → profile `font_normal` | `accent` / `accent_soft` / `text_meta` |
| Station name | `_name_labels[i]` | `kNameFlexBaseWidth=1` + `flex_grow=1` | `kFontStationName` (M22) | `text_primary` / `text_secondary` |

Name label: fixed line height = `lv_font_get_line_height(kFontStationName)`; `LV_LABEL_LONG_DOT` for ellipsis.

---

## Footer structure

| Object | Role |
|--------|------|
| `_helper_box` | Full-width clickable pill; `LV_EVENT_CLICKED` → `_helperEventCb` → `dismissActiveTemporary()` |
| `_helper` | Single text target for countdown and save/error feedback; not clickable |

**Footer visual contract uses `wgt_footer_pill` (FOOTERPILL-1):**

- `lv_obj_remove_style_all` — Preset-specific theme reset at create (before shared calls).
- `wgt_footer_pill::prepare_surface()` — shared fixed contract: gradient off, shadow off, radius=14, border-width=2, bg OPA_40/OPA_50, CLICKABLE, not SCROLLABLE.
- `wgt_footer_pill::apply_palette()` — shared palette colors (bg, normal border, pressed border).
- `wgt_footer_pill::make_child_passive(_helper)` — clears CLICKABLE/SCROLLABLE on label.

**wgt_footer_pill owns:** fixed normal/pressed visual states and palette-dependent colors.
**Screen owns:** geometry (height=32, padding=12/6), text, callback, Temporary dismiss, timer.

**Normal:** `panel_background` @ `LV_OPA_40`, `divider` border @ 2 px.
**Pressed:** `LV_OPA_50` fill, `text_meta` border.

Footer long press: no save action (only `LV_EVENT_CLICKED` registered).

---

## Fonts and text resources

### `kStr*` (UI strings)

| Constant | Text |
|----------|------|
| `kStrPresets` | ПРЕСЕТЫ |
| `kStrEmpty` | Пусто |
| `kStrUnavailable` | Недоступно |
| `kStrNoStation` | НЕТ СТАНЦИИ ДЛЯ СОХРАНЕНИЯ |
| `kStrSaveFailed` | ОШИБКА СОХРАНЕНИЯ |
| `kStrHelperDefault` | УДЕРЖИВАТЬ ДЛЯ СОХРАНЕНИЯ • ВОЗВРАТ 15 |
| `kStrEmptyText` | (empty) |
| `kStrEmptyStationNumber` | -- |

### `kFmt*` (format strings)

| Constant | Format |
|----------|--------|
| `kFmtSaved` | ПРЕСЕТ %u СОХРАНЕН |
| `kFmtCountdown` | УДЕРЖИВАТЬ ДЛЯ СОХРАНЕНИЯ • ВОЗВРАТ %d |
| `kFmtSlotNumber` | %d |
| `kFmtStationNumber` | #%u |
| `kFmtDecoratedStationName` | %s%s |

### Glyphs

| Constant | Value |
|----------|-------|
| `kBulletPrefixUtf8` | ` U+2022 ` (space-bullet-space) |

### Font resources

| Constant | Font |
|----------|------|
| `kFontTitle` | M20 |
| `kFontSlotNumber` | M16 |
| `kFontStationName` | M22 |
| `kFontFooter` | M14 |
| `preset_station_number_font()` | `LV_ACTIVE_PROFILE.font_normal` (nullable-checked) |

---

## Slot content states

| Condition | `_num_labels` | `_name_labels` |
|-----------|---------------|----------------|
| Empty | `--` (`text_meta`) | `Пусто` (`text_secondary`) |
| Occupied, invalid | `#N` (`accent_soft`) | `Недоступно` |
| Occupied, valid | `#N` (`accent`) | ` • <name>` (`text_primary`, LONG_DOT) |

Positional semantics: slot stores `uint16_t` station number only (PresetStore); playlist reorder may invalidate.

---

## Short-tap pipeline

```text
[Top-edge swipe] → showTemporary → enter()
[Tap card SHORT_CLICKED]
  → occupied && valid
  → play_station(num)
  → dismissActiveTemporary()
  → origin carousel slot
```

Empty or invalid row: no-op.

---

## Long-press / save pipeline

```text
[LONG_PRESSED on row]
  → refreshActiveTemporaryTimeout()
  → saveCurrentStation(slot)
  → on success: _updateRowContent, kFmtSaved feedback, restore timer 800 ms
  → on failure: kStrNoStation | kStrSaveFailed, restore timer 1200 ms
  → screen stays open (no dismiss)
```

`LONG_PRESSED_REPEAT`: timeout refresh only; `_long_press_handled` prevents double save.

---

## Countdown and feedback timers

| Timer | Period | Role |
|-------|--------|------|
| `_countdown_timer` | `kCountdownPeriodMs` (333 ms) | Updates `_helper` via `_updateCountdownHelper()` when second changes |
| `_feedback_timer` | one-shot 800/1200 ms | Restores countdown via `_setHelperDefault()` |

`_feedbackActive` pauses countdown display while feedback visible.
Both timers cancelled in `exit()` / `destroy()`.

Countdown text: `kFmtCountdown` — seconds rounded up; no `С` suffix (`15` … `1`).

---

## Temporary-screen lifecycle

```text
create()     → build tree (builders)
enter()      → preset_store::begin(), countdown, refresh 8 rows
update()     → no-op
exit()       → cancel timers
destroy()    → lv_obj_del(_screen), _nullHandles()
```

Dismiss paths:

- valid row tap → play + dismiss;
- footer tap → dismiss (no play);
- PageChain timeout (~15 s) → dismiss;
- Back/encoder (PageChain) → dismiss.

---

## Theme reapply

`liveReapplyTheme()` → `_applyAllColors()`:

- screen, title colors;
- `apply_footer_palette(_helper_box)` — no layout wipe;
- helper text: `text_secondary` when countdown; preserves `accent` on active success feedback;
- per-row `_applyRowColors()`.

---

## Failure states

| Case | UI |
|------|-----|
| Partial allocation in `create_preset_row` | `continue` on null row; partial tree; later enter still safe (null guards) |
| `preset_station_number_font()` null | num label created without font override |
| Save with no station | `kStrNoStation` in footer |
| Save I/O failure | `kStrSaveFailed` |
| Invalid occupied slot | row shows `Недоступно`; tap no-op |

---

## Shared-helper boundary

Preset footer visual pattern overlaps Weather and Station clickable footers.
Extraction into a shared footer-action helper is **intentionally not included in PRESETREF-A** and should be a separate cross-screen commit.

Общий footer helper для Weather/Station/Preset — **вне PRESETREF-A**, отдельный cross-screen commit.

---

## Notes / Заметки

- Title `kStrPresets` never changes during feedback; only `_helper` text changes.
- `_screen` is not clickable; no root-wide dismiss.
- PRESETREF-A: `style_transp()` removed (was unused).
- All `lv_*` calls run on DspTask only.
- Device smoke required before commit.
