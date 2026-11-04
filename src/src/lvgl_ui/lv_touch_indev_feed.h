// Narrow LVGL feed seam for the production touch read callback and host tests.
#ifndef YORADIO_LV_TOUCH_INDEV_FEED_H
#define YORADIO_LV_TOUCH_INDEV_FEED_H

#include "lv_touch_adapter_state.h"
#include "lvgl.h"

namespace lvgl_ui {

inline void applyTouchFrameToIndevData(const LvglTouchFrame& frame,
                                       bool feed_pressed,
                                       lv_indev_data_t& data) {
    // LVGL initializes data.point with the previous raw point before invoking
    // read_cb. A point-less RELEASED frame must leave it untouched so release
    // handlers receive the position at which the stroke actually ended.
    if (frame.has_point) {
        data.point.x = static_cast<lv_coord_t>(frame.x);
        data.point.y = static_cast<lv_coord_t>(frame.y);
    }
    data.state = feed_pressed ? LV_INDEV_STATE_PRESSED
                              : LV_INDEV_STATE_RELEASED;
}

}  // namespace lvgl_ui

#endif  // YORADIO_LV_TOUCH_INDEV_FEED_H
