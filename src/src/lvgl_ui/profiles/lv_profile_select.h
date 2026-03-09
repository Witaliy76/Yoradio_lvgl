#ifndef LV_PROFILE_SELECT_H
#define LV_PROFILE_SELECT_H

#include "../core/options.h"
#include "lv_profile_types.h"

// Compile-time selection of the active LVGL display profile by DSP_MODEL.
// Компиляционный выбор активного профиля LVGL по DSP_MODEL.

#if DSP_MODEL == DSP_ST7701
  // Sunton ESP32-4848S040 (480x480 RGB)
  static constexpr LvglDisplayProfile LV_ACTIVE_PROFILE{480u, 480u};
#elif DSP_MODEL == DSP_AXS15231B
  // JC3248W535C (320x480 QSPI)
  static constexpr LvglDisplayProfile LV_ACTIVE_PROFILE{320u, 480u};
#elif DSP_MODEL == DSP_UEDX48480021
  // UEDX48480021 (480x480 round RGB)
  static constexpr LvglDisplayProfile LV_ACTIVE_PROFILE{480u, 480u};
#else
  // Fallback generic profile for non-LVGL-target displays.
  // Запасной профиль по умолчанию для нецелевых дисплеев LVGL.
  static constexpr LvglDisplayProfile LV_ACTIVE_PROFILE{
      480u, // width (max expected)
      480u  // height (max expected)
  };
#endif

#endif

