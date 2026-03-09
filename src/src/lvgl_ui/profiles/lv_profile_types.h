#ifndef LV_PROFILE_TYPES_H
#define LV_PROFILE_TYPES_H

#include <stdint.h>

// Shared LVGL display profile type for all boards.
// Общий тип профиля LVGL-дисплея для всех плат.
struct LvglDisplayProfile {
    uint16_t width;
    uint16_t height;
};

#endif

