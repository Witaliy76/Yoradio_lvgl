// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef LV_PROFILE_UEDX48480021_H
#define LV_PROFILE_UEDX48480021_H

#include "lv_profile_types.h"
#include "../fonts/lv_fonts.h"

// UEDX48480021 — ST7701 480×480 round, CST826 touch.
//
// Initializer order = same as LvglDisplayProfile (see lv_profile_types.h).
// spectrum_*: board uses a narrower VU band (displayUEDX48480021conf.h ~ MAX_WIDTH−140, 30 bands, gap 4).

static const LvglDisplayProfile LvglProfile_UEDX48480021{
    480u,
    480u,
    0u,
    false,
    false,
    false,
    false,
    false,
    40u,
    true,
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_12_cyr),
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_16_cyr),
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_22_cyr),
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_48_cyr),
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr),
    8u,
    6u,
    4u,
    30u,
};

#endif
