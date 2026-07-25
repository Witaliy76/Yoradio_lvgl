// Author: Witaliy76 - https://github.com/Witaliy76
#include "../core/options.h"
#include "lv_touch_indev.h"
#include "lvgl.h"

#if (TS_MODEL!=TS_MODEL_UNDEFINED)
#include "../core/touchscreen.h"
#include "../core/display.h"
#include "../core/config.h"
#include "../core/autodim.h"
#endif

#include "profiles/lv_profile_select.h"

// Pointer indev for LVGL; read_cb runs from lv_timer_handler() on DspTask (Core 0).
// Pointer indev; read_cb из lv_timer_handler() на DspTask.
static lv_indev_drv_t s_touch_indev_drv;
static lv_indev_t* s_touch_indev = nullptr;

#if (TS_MODEL!=TS_MODEL_UNDEFINED)
// Stage 6.4C: touch state for indev-level polling (top-edge swipe detection).
// Tracks exactly what is fed to LVGL — after screensaver suppress.
// Состояние тача, реально переданное LVGL (после suppress при пробуждении saver).
static bool s_touch_is_down = false;
static int16_t s_touch_last_x = 0;
static int16_t s_touch_last_y = 0;

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

#if (TS_MODEL!=TS_MODEL_UNDEFINED) && YORADIO_LVGL_TOUCH_DEBUG
#include <Arduino.h>
// Throttled trace: raw GT911 vs state fed to LVGL (incl. screensaver wake suppress).
// Урезанный лог: сырой тач и то, что реально уходит в LVGL (в т.ч. suppress после saver wake).
static void lv_touch_debug_on_feed(bool raw_down, uint16_t x, uint16_t y, lv_indev_state_t fed) {
    static bool s_was_down;
    static uint16_t s_lx;
    static uint16_t s_ly;
    static uint32_t s_last_move_ms;
    static bool s_suppress_msg;
    const uint32_t now = millis();
    const bool fed_down = (fed == LV_INDEV_STATE_PRESSED);
    if (!raw_down) {
        if (s_was_down) {
            Serial.printf("[touch] UP fed=%s\n", fed_down ? "PR" : "REL");
        }
        s_was_down = false;
        s_suppress_msg = false;
        return;
    }
    if (s_suppress_lvgl_pointer_until_release && !s_suppress_msg) {
        Serial.printf("[touch] suppress: LVGL fed=REL while raw DOWN xy=%u,%u\n", x, y);
        s_suppress_msg = true;
    }
    if (!s_was_down) {
        Serial.printf("[touch] DOWN xy=%u,%u fed=%s\n", x, y, fed_down ? "PR" : "REL");
        s_was_down = true;
        s_last_move_ms = now;
        s_lx = x;
        s_ly = y;
        return;
    }
    const int dx = static_cast<int>(x) - static_cast<int>(s_lx);
    const int dy = static_cast<int>(y) - static_cast<int>(s_ly);
    const uint32_t dist2 = static_cast<uint32_t>(dx * dx + dy * dy);
    if ((now - s_last_move_ms) >= 80u || dist2 >= 400u) {
        Serial.printf("[touch] MOVE xy=%u,%u d=%d,%d fed=%s\n", x, y, dx, dy, fed_down ? "PR" : "REL");
        s_last_move_ms = now;
        s_lx = x;
        s_ly = y;
    }
}
#endif

static void lv_touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    (void)drv;
#if (TS_MODEL!=TS_MODEL_UNDEFINED)
    static bool s_prev_touch_down = false;
    uint16_t x = 0;
    uint16_t y = 0;
    if (touchscreen.readPointerForLvgl(&x, &y)) {
        // Root X normalization: GT911/4848S040 delivers mirrored X after axis swap in readPointerForLvgl.
        // Fix here so every LVGL consumer (hit-test, gestures, sliders) gets correct coordinates.
        // Нормализация X: GT911 после swap осей даёт зеркальный X — исправляем в единой точке для LVGL.
        if (LV_ACTIVE_PROFILE.touch_swap_horizontal_carousel) {
            x = LV_ACTIVE_PROFILE.width - 1 - x;
        }
        touch_wake_saver_or_blank_if_needed(x, y, true);
        data->point.x = static_cast<lv_coord_t>(x);
        data->point.y = static_cast<lv_coord_t>(y);
        if (s_suppress_lvgl_pointer_until_release) {
            data->state = LV_INDEV_STATE_RELEASED;
        } else {
            data->state = LV_INDEV_STATE_PRESSED;
        }
        // Track LVGL-fed state (after suppress) for indev-level swipe polling.
        // Фиксируем реально переданное состояние (после suppress) для polling.
        // Auto Dim: activity only on RELEASED→PRESSED edge, not every poll frame.
        // Auto Dim: активность только на фронте нажатия, не на каждом poll.
        const bool touch_down = (data->state == LV_INDEV_STATE_PRESSED);
        s_touch_is_down = touch_down;
        if (touch_down) {
            s_touch_last_x = static_cast<int16_t>(data->point.x);
            s_touch_last_y = static_cast<int16_t>(data->point.y);
            if (!s_prev_touch_down) {
                autodim_notify_activity("touch");
            }
        }
        s_prev_touch_down = touch_down;
#if YORADIO_LVGL_TOUCH_DEBUG && (TS_MODEL!=TS_MODEL_UNDEFINED)
        lv_touch_debug_on_feed(true, x, y, data->state);
#endif
    } else {
        touch_wake_saver_or_blank_if_needed(0, 0, false);
        data->state = LV_INDEV_STATE_RELEASED;
        s_touch_is_down = false;
        s_prev_touch_down = false;
#if YORADIO_LVGL_TOUCH_DEBUG && (TS_MODEL!=TS_MODEL_UNDEFINED)
        lv_touch_debug_on_feed(false, 0, 0, data->state);
#endif
    }
#else
    data->state = LV_INDEV_STATE_RELEASED;
#endif
}

// Stage 6.4C: getters — safe for all TS_MODEL configurations / безопасны при любом TS_MODEL.
bool lvgl_ui::touchIndevIsDown() {
#if (TS_MODEL!=TS_MODEL_UNDEFINED)
    return s_touch_is_down;
#else
    return false;
#endif
}
int16_t lvgl_ui::touchIndevX() {
#if (TS_MODEL!=TS_MODEL_UNDEFINED)
    return s_touch_last_x;
#else
    return 0;
#endif
}
int16_t lvgl_ui::touchIndevY() {
#if (TS_MODEL!=TS_MODEL_UNDEFINED)
    return s_touch_last_y;
#else
    return 0;
#endif
}

void lvgl_ui::initTouchIndev() {
    if (s_touch_indev) {
        return;
    }
    if (!lv_disp_get_default()) {
        return;
    }
#if (TS_MODEL!=TS_MODEL_UNDEFINED)
    lv_indev_drv_init(&s_touch_indev_drv);
    s_touch_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_touch_indev_drv.read_cb = lv_touch_read_cb;
    s_touch_indev = lv_indev_drv_register(&s_touch_indev_drv);
    (void)s_touch_indev;
#endif
}
