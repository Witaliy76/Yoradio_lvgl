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
    lv_obj_t* lbl_sleep_timer = nullptr;
    lv_obj_t* lbl_clock = nullptr;
};

// DspTask-only create().
bool create(lv_obj_t* parent, Instance& out);

// DspTask-only: Wi‑Fi glyph, clock, weather — same logic as Main had inline (Stage 6.2 Patch A).
// Только DspTask: Wi‑Fi, часы, погода — та же логика, что была в Main.
void update(const Instance& inst);

// Stage 6.6R-B: reapply palette colors to existing instance (DspTask only).
// Updates icon/clock/weather text colors without layout changes.
// Этап 6.6R-B: обновить цвета существующего экземпляра из текущей палитры (только DspTask).
void reapplyTheme(Instance& inst);

} // namespace wgt_status_line
} // namespace lvgl_ui

#endif
