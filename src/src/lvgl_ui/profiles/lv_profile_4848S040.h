// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef LV_PROFILE_4848S040_H
#define LV_PROFILE_4848S040_H

#include "lv_profile_types.h"
#include "../fonts/lv_fonts.h"

// Sunton ESP32-4848S040 — ST7701 480×480, GT911 touch.
// touch_swap_horizontal_carousel: GT911+LVGL gesture dir vs PageChain UX — swap on this board (device 2026-03).
//
// Initializer order = LvglDisplayProfile fields (see lv_profile_types.h comments):
// width, height, default_rotation, color_swap, touch_swap_xy, touch_invert_x, touch_invert_y,
// touch_swap_horizontal_carousel, buf_lines, buf_in_psram, font_small…font_header, frame_padding,
// spectrum_bar_width, spectrum_bar_gap, spectrum_height.
// Spectrum numbers: heuristic match to displayST7701conf.h VUBandsConfig (bandsHspace=4, bar area ~5px wide).

/* const (not constexpr): font pointers are cross-TU addresses / не constexpr — адреса шрифтов из .c */
static const LvglDisplayProfile LvglProfile_4848S040{
    480u,
    480u,
    0u,
    false,
    false,
    false,
    false,
    true,
    40u,
    true,
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_12_cyr),
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_16_cyr),
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_22_cyr),
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_48_cyr),
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr),
    8u,
    5u,
    4u,
    30u,
};

#endif
