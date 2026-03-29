#include "../core/options.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lv_touch_indev.h"
#include "lvgl.h"

#if (TS_MODEL!=TS_MODEL_UNDEFINED) && (DSP_MODEL!=DSP_DUMMY)
#include "../core/touchscreen.h"
#endif

// Pointer indev for LVGL; read_cb runs from lv_timer_handler() on DspTask (Core 0).
// Pointer indev; read_cb из lv_timer_handler() на DspTask.
static lv_indev_drv_t s_touch_indev_drv;
static lv_indev_t* s_touch_indev = nullptr;

static void lv_touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    (void)drv;
#if (TS_MODEL!=TS_MODEL_UNDEFINED) && (DSP_MODEL!=DSP_DUMMY)
    uint16_t x = 0;
    uint16_t y = 0;
    if (touchscreen.readPointerForLvgl(&x, &y)) {
        data->point.x = static_cast<lv_coord_t>(x);
        data->point.y = static_cast<lv_coord_t>(y);
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
#else
    data->state = LV_INDEV_STATE_RELEASED;
#endif
}

void lvgl_ui::initTouchIndev() {
    if (s_touch_indev) {
        return;
    }
    if (!lv_disp_get_default()) {
        return;
    }
#if (TS_MODEL!=TS_MODEL_UNDEFINED) && (DSP_MODEL!=DSP_DUMMY)
    lv_indev_drv_init(&s_touch_indev_drv);
    s_touch_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_touch_indev_drv.read_cb = lv_touch_read_cb;
    s_touch_indev = lv_indev_drv_register(&s_touch_indev_drv);
    (void)s_touch_indev;
#endif
}

#else

#include "lv_touch_indev.h"

void lvgl_ui::initTouchIndev() {}

#endif
