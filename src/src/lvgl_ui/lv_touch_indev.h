// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef LV_TOUCH_INDEV_H
#define LV_TOUCH_INDEV_H

#include <stdint.h>

// Stage 5.2: LVGL pointer input device registration (DspTask / lv_timer_handler only).
// Этап 5.2: регистрация LVGL pointer indev (только DspTask / lv_timer_handler).

namespace lvgl_ui {

void initTouchIndev();

// Stage 6.4C: Touch state getters for indev-polling swipe detection (DspTask only).
// Reflect the state actually fed to LVGL (after screensaver-wake suppress).
// Возвращают состояние, реально переданное LVGL (с учётом suppress при пробуждении saver).
bool  touchIndevIsDown();  // true when LVGL sees PRESSED (not suppressed)
int16_t touchIndevX();     // last fed X coordinate (normalized)
int16_t touchIndevY();     // last fed Y coordinate (normalized)

}

#endif
