# Main screen — LVGL object tree (`scr_main`)

**English:** Parent → child hierarchy for `LvglMainScreen::create()` in `scr_main.cpp`. Use when reasoning about layout, flex, and theme padding.  
**Русский:** Иерархия родитель → потомок для `LvglMainScreen::create()` в `scr_main.cpp`. Удобно для разметки, flex и паддингов темы.

**Source of truth:** [`scr_main.cpp`](scr_main.cpp) — update this file when the tree changes.

**Maintenance / Поддержка:** After editing `create()` (new containers, reorder, rename), refresh the ASCII block and the Mermaid block below so they stay accurate.

---

## Order on `_screen` (flex column, top → bottom)

1. `wgt_status_line.root`  
2. `status_divider`  
3. `spacer_top` (flex grow)  
4. `cont_mid` (text stack only — 6.1E-c)  
5. `spacer_bottom` (flex grow)  
6. `zone_visual` (1 px placeholder)  
7. `zone_bottom_sym_spacer` (symmetry, height may be set after layout)  
8. `zone_bottom`

Floating overlays (not flex children of `_screen` in the same sense): `_lbl_vol_popup`; `_vol_touch_zone` / `_vol_gesture_guard` inside `zone_bottom`; `_screen_bottom_carousel_guard` if needed.

---

## Tree (ASCII)

```
_screen
├── wgt_status_line.root  (see ../widgets/wgt_status_line.cpp)
│   ├── lbl_wifi
│   ├── spacer (flex grow)
│   ├── cont_weather
│   │   ├── lbl_weather_glyph
│   │   └── lbl_weather_temp
│   └── lbl_clock
├── status_divider
├── spacer_top
├── cont_mid
│   └── cont_text
│       ├── _lbl_station_name
│       ├── _lbl_track
│       └── _lbl_artist
├── spacer_bottom
├── zone_visual
├── zone_bottom_sym_spacer
├── zone_bottom
│   ├── control_band (shared shelf underlay; flex row)
│   │   ├── spacer_left (width = utility_group — centers transport triad on screen)
│   │   ├── transport_group (LV_OBJ_FLAG_OVERFLOW_VISIBLE; pad_hor inset)
│   │   │   └── prev / play-stop / next — lv_btn + transport callbacks; play/stop label synced from player.status()
│   │   └── utility_group (LV_OBJ_FLAG_OVERFLOW_VISIBLE; tight pad_column list↔settings)
│   │       └── list / settings — lv_btn, text_secondary; routing out of scope until wired
│   ├── row_meta_stream
│   │   ├── _lbl_station_num
│   │   └── _lbl_bitrate
│   ├── col_vol
│   │   ├── _lbl_volume
│   │   └── _bar_volume
│   ├── _bar_buffer (heap / divider line)
│   ├── _lbl_ai_line
│   ├── _vol_touch_zone (FLOATING)
│   └── _vol_gesture_guard (FLOATING)
├── _screen_bottom_carousel_guard (FLOATING, if gap)
└── _lbl_vol_popup (FLOATING)
```

---

## Diagram (Mermaid)

Render in GitHub / VS Code Markdown preview / Mermaid-compatible tools.

```mermaid
flowchart TB
  subgraph screen["_screen"]
    SL[wgt_status_line.root]
    SD[status_divider]
    ST[spacer_top]
    CM[cont_mid]
    SB[spacer_bottom]
    ZV[zone_visual]
    ZSS[zone_bottom_sym_spacer]
    ZB[zone_bottom]
  end

  subgraph sl["wgt_status_line.root"]
    WIFI[lbl_wifi]
    SP[spacer]
    CWX[cont_weather]
    CLK[lbl_clock]
  end

  subgraph cwx["cont_weather"]
    WG[lbl_weather_glyph]
    WT[lbl_weather_temp]
  end

  subgraph cm["cont_mid"]
    CT[cont_text]
  end

  subgraph ct["cont_text"]
    SN[_lbl_station_name]
    TR[_lbl_track]
    AR[_lbl_artist]
  end

  subgraph zb["zone_bottom"]
    CB[control_band]
    RMS[row_meta_stream]
    CV[col_vol]
    BUF[_bar_buffer]
    AI[_lbl_ai_line]
  end

  subgraph cb["control_band"]
    SLB[spacer_left]
    TG[transport_group]
    UG[utility_group]
  end

  subgraph rms["row_meta_stream"]
    NUM[_lbl_station_num]
    BR[_lbl_bitrate]
  end

  subgraph cv["col_vol"]
    VOL[_lbl_volume]
    BAR[_bar_volume]
  end

  SL --> WIFI
  SL --> SP
  SL --> CWX
  SL --> CLK
  CWX --> WG
  CWX --> WT
  CM --> CT
  CT --> SN
  CT --> TR
  CT --> AR
  ZB --> CB
  ZB --> RMS
  ZB --> CV
  ZB --> BUF
  ZB --> AI
  CB --> SLB
  CB --> TG
  CB --> UG
  RMS --> NUM
  RMS --> BR
  CV --> VOL
  CV --> BAR
```

---

## Notes / Заметки

- **Control band (Stage 6.1E+):** Three flex children: `spacer_left` (width set after layout to match `utility_group`) + `transport_group` + `utility_group`. Transport uses Tabler control icon fonts (profile-dependent sizes, incl. 28px on wide); utility uses smaller glyph with large hit target. No floating `_hit_play` — transport taps only on visible buttons.  
  **Полоса управления:** три flex-ребёнка: балансирующий `spacer_left` + транспорт + utility. Иконки Tabler; скрытой зоны тапа по центру нет.

- **Theme padding:** Base `lv_obj` containers may inherit LVGL default `card` padding; Main zeroes explicit `pad_all` where needed — see comments in `scr_main.cpp` and `wgt_status_line.cpp`.  
  **Паддинг темы:** у базового `lv_obj` может быть `card` padding; на Main явно обнуляем `pad_all` там, где нужно — см. комментарии в `scr_main.cpp` и `wgt_status_line.cpp`.

- Widget `wgt_status_line` is defined in [`../widgets/wgt_status_line.cpp`](../widgets/wgt_status_line.cpp).
