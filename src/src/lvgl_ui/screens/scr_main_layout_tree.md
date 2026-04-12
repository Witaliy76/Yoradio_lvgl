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
4. `cont_mid`  
5. `spacer_bottom` (flex grow)  
6. `zone_visual` (1 px placeholder)  
7. `zone_bottom`  
8. `_hit_play` — created last, **`LV_OBJ_FLAG_FLOATING`** (not a flex row; sits above center for tap)

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
│   ├── cont_text
│   │   ├── _lbl_station_name
│   │   ├── _lbl_track
│   │   └── _lbl_artist
│   └── row_meta
│       ├── _lbl_station_num
│       └── _lbl_bitrate
├── spacer_bottom
├── zone_visual
├── zone_bottom
│   ├── col_vol
│   │   ├── _lbl_volume
│   │   └── _bar_volume
│   └── _lbl_ai_line
└── _hit_play   (FLOATING — вне flex-потока, поверх для тапа)
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
    RM[row_meta]
  end

  subgraph ct["cont_text"]
    SN[_lbl_station_name]
    TR[_lbl_track]
    AR[_lbl_artist]
  end

  subgraph rmeta["row_meta"]
    NUM[_lbl_station_num]
    BR[_lbl_bitrate]
  end

  subgraph zb["zone_bottom"]
    CV[col_vol]
    AI[_lbl_ai_line]
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
  CM --> RM
  CT --> SN
  CT --> TR
  CT --> AR
  RM --> NUM
  RM --> BR
  ZB --> CV
  ZB --> AI
  CV --> VOL
  CV --> BAR
```

---

## Notes / Заметки

- **Theme padding:** Base `lv_obj` containers may inherit LVGL default `card` padding; Main zeroes explicit `pad_all` where needed — see comments in `scr_main.cpp` and `wgt_status_line.cpp`.  
  **Паддинг темы:** у базового `lv_obj` может быть `card` padding; на Main явно обнуляем `pad_all` там, где нужно — см. комментарии в `scr_main.cpp` и `wgt_status_line.cpp`.

- Widget `wgt_status_line` is defined in [`../widgets/wgt_status_line.cpp`](../widgets/wgt_status_line.cpp).
