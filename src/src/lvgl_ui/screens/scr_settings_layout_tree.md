Author: Witaliy76 - https://github.com/Witaliy76

# Settings screen — LVGL object tree (`scr_settings`)

This file describes the current `LvglSettingsPage` hierarchy and lifecycle. The Settings slot
remains one fixed `PageChain` page; Display, Music Rail, Scrolling, and TIMERS are in-page views.

## Root and Main

`_screen` is a non-scrollable flex column with `LV_ACTIVE_PROFILE.frame_padding`, a 6 px row gap,
and `pal.device_background`.

```text
_screen
├── _status_line.root
├── status divider                         pal.divider
└── _view_main                             flex column, default visible
    ├── _cont_content
    │   ├── _row_display                   DISPLAY          <brightness> >
    │   ├── _row_music                     MUSIC RAIL       ON/OFF >
    │   ├── _row_resume_startup            RESUME ON STARTUP ON/OFF
    │   ├── _row_sleep_timer               TIMERS           OFF/RADIO/DEEP SLEEP >
    │   └── _row_wifi                      WI-FI            <SSID> >
    └── _footer_area                       RETURN TO MAIN
```

The whole TIMERS row is tappable and reuses `YORA_SETTINGS_GLYPH_SLEEP_TIMER`; no font subset or
PageChain slot was added.

## Display detail

Display is lazy-once for the Settings screen lifecycle (MEM1) and survives Back.

```text
_view_display
├── detail header                          back | display glyph | DISPLAY
└── _cont_display_content
    ├── BRIGHTNESS slider
    ├── AUTO DIM / DIM AFTER / DIM LEVEL
    ├── THEME
    ├── PERFORMANCE MONITOR
    └── SCROLLING >
```

## Music Rail detail

Music Rail is lazy and destroyed on Back/Settings exit.

```text
_view_music
├── detail header                          back | music glyph | MUSIC RAIL
└── _cont_music_content
    ├── PRESENCE RAIL
    └── RAIL PROFILE
```

## Scrolling detail

Scrolling is lazy and destroyed on Back/Settings exit. Its established sliders, preview,
geometry, and callbacks are unchanged by TIMERS.

```text
_view_scrolling
├── detail header                          back | display glyph | SCROLLING
└── _cont_scrolling_content
    ├── SCROLL SPEED slider
    ├── SCROLL TYPE
    ├── SCROLL DELAY slider
    └── SCROLLING PREVIEW
```

## TIMERS detail

TIMERS is lazy, fixed-height/non-scrollable, and destroyed on Back or Settings exit. It reuses one
control tree for both tabs, so repeated tab changes allocate no additional LVGL objects.

```text
_view_sleep_timer                         SettingsView::SleepTimer
├── detail header                         back | sleep glyph | TIMERS
└── _cont_sleep_timer_content             flex column; pad_top=4; row_gap=5
    ├── tabs                              height=38
    │   ├── _timer_tab_buttons[0]
    │   │   └── _timer_tab_labels[0]      RADIO
    │   └── _timer_tab_buttons[1]
    │       └── _timer_tab_labels[1]      DEEP SLEEP
    │
    ├── _timer_event_cards[0]             height=92
    │   ├── header
    │   │   ├── _timer_event_titles[0]    STOP RADIO AFTER / DEEP SLEEP AFTER
    │   │   └── _timer_at_labels[0]       STOPS AT / SLEEP AT ...
    │   └── controls row
    │       ├── HOURS column
    │       │   ├── _timer_captions[0] + _timer_value_labels[0]
    │       │   └── _timer_sliders[0]     range 0..24
    │       └── MINUTES column
    │           ├── _timer_captions[1] + _timer_value_labels[1]
    │           └── _timer_sliders[1]     range 0..59
    │
    ├── _timer_event_cards[1]             height=92
    │   ├── header
    │   │   ├── _timer_event_titles[1]    START RADIO AFTER / WAKE AFTER SLEEP
    │   │   └── _timer_at_labels[1]       STARTS AT / WAKE AT ...
    │   └── controls row
    │       ├── HOURS column
    │       │   ├── _timer_captions[2] + _timer_value_labels[2]
    │       │   └── _timer_sliders[2]     range 0..24
    │       └── MINUTES column
    │           ├── _timer_captions[3] + _timer_value_labels[3]
    │           └── _timer_sliders[3]     range 0..59
    │
    ├── _lbl_timer_state                  height=20; conflict/clock/remaining state
    └── buttons row                       height=42
        ├── _btn_timer_primary
        │   └── _lbl_timer_primary        START/CANCEL RADIO or DEEP SLEEP TIMER
        └── _btn_deep_sleep_now           visible on DEEP SLEEP tab only
            └── _lbl_deep_sleep_now       ENTER DEEP SLEEP NOW
```

Fixed content height is 304 px (`38 + 92 + 92 + 20 + 42 + four 5 px gaps`), before the detail
header. It fits the 480×480 profile without vertical scrolling and leaves the existing status/header
frame intact. Slider rows are 28 px high with a padded knob and full-width columns for touch.

### Runtime and draft states

- With no active plan, sliders show persisted presets.
- `LV_EVENT_VALUE_CHANGED` updates only `_timer_event1_draft` / `_timer_event2_draft` and preview.
- `LV_EVENT_RELEASED` or `LV_EVENT_PRESS_LOST` persists one combined minute value for that event.
- Start takes an immutable runtime snapshot. Any active plan disables all four sliders.
- A telnet-created plan shows its runtime snapshot and remaining time in the same controls.
- Cancel clears the runtime plan and reloads persisted presets.
- The other tab remains visible but disabled and says which active plan must be cancelled first.
- `0 H 0 MIN` disables one event. When both Radio events are enabled, Start must be strictly
  later than Stop. A conflicting drag shows `START MUST BE LATER THAN STOP`; on release the UI
  automatically moves Start to one minute after Stop and persists the corrected pair.
- Deep Sleep uses independent intervals: `WAKE AFTER SLEEP` begins at actual sleep entry, so
  Sleep `5 MIN` plus Wake `3 MIN` is valid and does not need Radio-style ordering correction.
- `... AT` preview follows local time before Start. Once active, `...AT` is rebuilt on every
  snapshot from the monotonic remaining time plus the current wall clock, so a late NTP sync or a
  system-time correction updates the shown time without restarting the countdown itself.
- Deep Sleep `WAKE AT` shows `TIMER WAKE OFF` instead of a projected time when the wake preset is
  `0` (RTC wake disabled), rather than a misleading current-time-looking `... AT` value.
- `CLOCK NOT SYNCED` never blocks monotonic countdown execution.

### Theme contract

No TIMERS color is hardcoded. `_applyTimersTheme()` reapplies all live handles for Dark, Light,
and Custom using only `YoRadioPalette`:

- detail background/header: existing Settings helpers;
- tab and selected state: `panel_background`, `accent_soft`, `accent`, `divider`;
- primary/secondary/meta text: `text_primary`, `text_secondary`, `text_meta`;
- event cards/dividers: `panel_background`, `divider`;
- slider track/fill/knob: `volume_bar_track`, `volume_bar_fill`, `accent`, `panel_border`;
- enabled/disabled buttons: `accent`, `panel_background`, `panel_border`, palette text plus opacity;
- conflict and cross-plan warning: `accent`.

`liveReapplyTheme()` recolors the selected tab, disabled sliders/buttons, active state, and event
cards without rebuilding the Settings screen.

## View routing and cleanup

| View | Lifecycle | Carousel swipe |
|---|---|---|
| `Main` | root lifetime | allowed |
| `Display` | lazy once | blocked |
| `MusicRail` | lazy, destroy-on-Back/exit | blocked |
| `Scrolling` | lazy, destroy-on-Back/exit | blocked |
| `SleepTimer` (TIMERS) | lazy, destroy-on-Back/exit | blocked |

`block_gesture_bubble_deep(_view_sleep_timer)` prevents horizontal PageChain gestures from
escaping any TIMERS child. `_destroySleepTimerView()` and `_nullHandles()` clear every tab, card,
label, slider, button, draft, drag, and sync handle; callbacks die with the deleted LVGL tree.
