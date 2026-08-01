// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef LV_PROFILE_SELECT_H
#define LV_PROFILE_SELECT_H

#include "../core/options.h"
#include "lv_profile_types.h"

// Compile-time active profile / Активный профиль на этапе компиляции.
// Public beta supports DSP_ST7701 (ESP32-4848S040) only.
// Публичная beta поддерживает только DSP_ST7701 (ESP32-4848S040).

#if DSP_MODEL == DSP_ST7701
#include "lv_profile_4848S040.h"
static const LvglDisplayProfile& LV_ACTIVE_PROFILE = LvglProfile_4848S040;
#else
#error "Unsupported DSP_MODEL: LVGL profile select currently supports DSP_ST7701 only"
#endif

#endif
