#ifndef LV_PROFILE_SELECT_H
#define LV_PROFILE_SELECT_H

#include "../core/options.h"
#include "lv_profile_types.h"

// Compile-time active profile / Активный профиль на этапе компиляции.

#if DSP_MODEL == DSP_ST7701
#include "lv_profile_4848S040.h"
static const LvglDisplayProfile& LV_ACTIVE_PROFILE = LvglProfile_4848S040;
#elif DSP_MODEL == DSP_AXS15231B
#include "lv_profile_AXS15231B.h"
static const LvglDisplayProfile& LV_ACTIVE_PROFILE = LvglProfile_AXS15231B;
#elif DSP_MODEL == DSP_UEDX48480021
#include "lv_profile_UEDX48480021.h"
static const LvglDisplayProfile& LV_ACTIVE_PROFILE = LvglProfile_UEDX48480021;
#else
static constexpr LvglDisplayProfile LV_ACTIVE_PROFILE{
    480u,
    480u,
    0u,
    false,
    false,
    false,
    false,
    40u,
    true,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    8u,
    6u,
    4u,
    30u,
};
#endif

#endif
