// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef LV_PROFILE_AXS15231B_H
#define LV_PROFILE_AXS15231B_H

#include "lv_profile_types.h"
#include "../fonts/lv_fonts.h"

// JC3248W535C — AXS15231B QSPI 320×480, touch AXS15231B.
//
// Initializer order = same as LvglDisplayProfile (see lv_profile_types.h).
// spectrum_*: heuristic from displayAXS15231Bconf.h VUBandsConfig (30px bar height, gap 4, 45 bands).

static const LvglDisplayProfile LvglProfile_AXS15231B{
    320u,
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
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_40_cyr),
    reinterpret_cast<const void*>(&lv_font_yora_montserrat_14_cyr),
    8u,
    4u,
    4u,
    35u,
};

#endif
