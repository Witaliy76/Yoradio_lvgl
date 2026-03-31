#include "../core/options.h"

#if YORADIO_USE_LVGL && (YORADIO_LVGL_STAGE >= 2)

#include "lv_touch_indev.h"
#include "lvgl.h"

#if (TS_MODEL!=TS_MODEL_UNDEFINED) && (DSP_MODEL!=DSP_DUMMY)
#include "../core/touchscreen.h"
#include "../core/display.h"
#include "../core/config.h"
#endif

// Pointer indev for LVGL; read_cb runs from lv_timer_handler() on DspTask (Core 0).
// Pointer indev; read_cb из lv_timer_handler() на DspTask.
static lv_indev_drv_t s_touch_indev_drv;
static lv_indev_t* s_touch_indev = nullptr;

#if (TS_MODEL!=TS_MODEL_UNDEFINED) && (DSP_MODEL!=DSP_DUMMY)
// After wake putRequest, feed LVGL RELEASED until finger up — avoids click/gesture on Main/Info same stroke.
// После wake в LVGL подаём RELEASED до отпускания — иначе тот же жест даёт toggle/карусель под оверлеем.
static bool s_suppress_lvgl_pointer_until_release = false;

// Stage 5.6: any touch press or finger movement wakes SCREENSAVER / SCREENBLANK (single path with LVGL indev).
// Этап 5.6: любое нажатие или движение пальца будит SCREENSAVER / SCREENBLANK (единый путь через LVGL indev).
static void touch_wake_saver_or_blank_if_needed(uint16_t x, uint16_t y, bool pointer_down) {
    static bool s_finger_down;
    static bool s_wake_sent_this_stroke;
    static uint16_t s_last_x;
    static uint16_t s_last_y;
    // Release clears suppress even after mode is PLAYER — wake putRequest is async.
    // Отпускание сбрасывает suppress даже при PLAYER — смена режима асинхронная.
    if (!pointer_down) {
        s_finger_down = false;
        s_wake_sent_this_stroke = false;
        s_suppress_lvgl_pointer_until_release = false;
        return;
    }
    const displayMode_e m = display.mode();
    if (m != SCREENSAVER && m != SCREENBLANK) {
        return;
    }
    const bool new_press = !s_finger_down;
    const bool moved = s_finger_down && (x != s_last_x || y != s_last_y);
    s_finger_down = true;
    s_last_x = x;
    s_last_y = y;
    if (s_wake_sent_this_stroke) {
        return;
    }
    if (!new_press && !moved) {
        return;
    }
    config.screensaverTicks = SCREENSAVERSTARTUPDELAY;
    config.screensaverPlayingTicks = SCREENSAVERSTARTUPDELAY;
    display.putRequest(NEWMODE, static_cast<int>(PLAYER));
    s_wake_sent_this_stroke = true;
    s_suppress_lvgl_pointer_until_release = true;
}
#endif

static void lv_touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    (void)drv;
#if (TS_MODEL!=TS_MODEL_UNDEFINED) && (DSP_MODEL!=DSP_DUMMY)
    uint16_t x = 0;
    uint16_t y = 0;
    if (touchscreen.readPointerForLvgl(&x, &y)) {
        touch_wake_saver_or_blank_if_needed(x, y, true);
        data->point.x = static_cast<lv_coord_t>(x);
        data->point.y = static_cast<lv_coord_t>(y);
        if (s_suppress_lvgl_pointer_until_release) {
            data->state = LV_INDEV_STATE_RELEASED;
        } else {
            data->state = LV_INDEV_STATE_PRESSED;
        }
    } else {
        touch_wake_saver_or_blank_if_needed(0, 0, false);
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
