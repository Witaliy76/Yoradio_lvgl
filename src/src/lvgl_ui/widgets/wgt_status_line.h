#ifndef WGT_STATUS_LINE_H
#define WGT_STATUS_LINE_H

#include "lvgl.h"

namespace lvgl_ui {
namespace wgt_status_line {

// Main status strip: Wi‑Fi + centered clock + weather glyph+°C on the right (three equal flex columns).
// Полоса: три колонки — Wi‑Fi слева, часы по центру экрана, погода справа.

struct Instance {
    lv_obj_t* root = nullptr;
    lv_obj_t* lbl_wifi = nullptr;
    lv_obj_t* cont_weather = nullptr;
    lv_obj_t* lbl_weather_glyph = nullptr;
    lv_obj_t* lbl_weather_temp = nullptr;
    lv_obj_t* lbl_clock = nullptr;
};

// DspTask-only create().
bool create(lv_obj_t* parent, Instance& out);

} // namespace wgt_status_line
} // namespace lvgl_ui

#endif
