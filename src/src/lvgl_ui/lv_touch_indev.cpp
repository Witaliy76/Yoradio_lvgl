// Author: Witaliy76 - https://github.com/Witaliy76
#include "../core/options.h"
#include "lv_touch_indev.h"
#include "lv_touch_indev_feed.h"
#include "lvgl.h"

#if (TS_MODEL!=TS_MODEL_UNDEFINED)
#include <Arduino.h>
#include "../core/touchscreen.h"
#include "../core/display.h"
#include "../core/config.h"
#include "../core/autodim.h"
#endif

// Pointer indev for LVGL; read_cb runs from lv_timer_handler() on DspTask (Core 0).
// Pointer indev; read_cb из lv_timer_handler() на DspTask.
// BASE-LVGL9-MIGRATION C3: no lv_indev_drv_t in LVGL 9 — lv_indev_t is created/configured directly.
static lv_indev_t* s_touch_indev = nullptr;

#if (TS_MODEL!=TS_MODEL_UNDEFINED)
// Stage 6.4C: touch state for indev-level polling (top-edge swipe detection).
// Tracks exactly what is fed to LVGL — after screensaver suppress.
// Состояние тача, реально переданное LVGL (после suppress при пробуждении saver).
static bool s_touch_is_down = false;
static int16_t s_touch_last_x = 0;
static int16_t s_touch_last_y = 0;

// Minimal typed boundary: TouchPort owns hardware/geometry, this adapter owns
// only release-before-rearm state and an LVGL-neutral feed decision.
class LvglTouchAdapter final {
  public:
    explicit LvglTouchAdapter(yoradio::TouchPort& port) : port_(port) {}

    lvgl_ui::LvglTouchFrame read() {
        return state_.consume(port_.read());
    }

  private:
    yoradio::TouchPort& port_;
    lvgl_ui::LvglTouchAdapterState state_;
};

static LvglTouchAdapter s_touch_adapter(touchscreen);
static lvgl_ui::ScreensaverTouchState s_screensaver_touch_state;

// Stage 5.6: any touch press or finger movement wakes SCREENSAVER / SCREENBLANK (single path with LVGL indev).
// Этап 5.6: любое нажатие или движение пальца будит SCREENSAVER / SCREENBLANK (единый путь через LVGL indev).
static lvgl_ui::ScreensaverTouchDecision apply_screensaver_wake_policy(
    const lvgl_ui::LvglTouchFrame& frame) {
    const displayMode_e m = display.mode();
    const lvgl_ui::ScreensaverTouchDecision decision =
        s_screensaver_touch_state.consume(
            frame, m == SCREENSAVER || m == SCREENBLANK);
    if (decision.wake_requested) {
        config.screensaverTicks = SCREENSAVERSTARTUPDELAY;
        config.screensaverPlayingTicks = SCREENSAVERSTARTUPDELAY;
        display.putRequest(NEWMODE, static_cast<int>(PLAYER));
    }
    return decision;
}
#endif

#if TS_MODEL==TS_MODEL_GT911
static const char* touch_status_name(yoradio::TouchSampleStatus status) {
    switch (status) {
        case yoradio::TouchSampleStatus::Valid: return "Valid";
        case yoradio::TouchSampleStatus::NoData: return "NoData";
        case yoradio::TouchSampleStatus::Released: return "Released";
        case yoradio::TouchSampleStatus::Error: return "Error";
        case yoradio::TouchSampleStatus::Unavailable: return "Unavailable";
    }
    return "Unknown";
}

// Permanent Debug Touch lifecycle: the existing WebUI flag owns compact
// DOWN/MOVE/UP and fault-transition tracing. OFF exits immediately; MOVE is
// capped at 10 Hz. Coordinates are exactly those fed to LVGL.
static void lv_touch_debug_on_feed(const lvgl_ui::LvglTouchFrame& frame,
                                   lv_indev_state_t fed,
                                   const lv_point_t& fed_point) {
    static bool s_debug_active;
    static bool s_was_down;
    static uint16_t s_lx;
    static uint16_t s_ly;
    static yoradio::TouchSampleStatus s_last_status =
        yoradio::TouchSampleStatus::Unavailable;
    static uint32_t s_last_move_ms;

    if (!config.store.dbgtouch) {
        if (s_debug_active) {
            s_debug_active = false;
            s_was_down = false;
            s_last_status = yoradio::TouchSampleStatus::Unavailable;
            s_last_move_ms = 0;
        }
        return;
    }
    s_debug_active = true;

    const uint32_t now = millis();
    const bool fresh_point = frame.source_status == yoradio::TouchSampleStatus::Valid &&
                             frame.has_point;
    const bool source_down = fresh_point ||
        (frame.source_status == yoradio::TouchSampleStatus::NoData && s_was_down);
    const bool fed_down = (fed == LV_INDEV_STATE_PRESSED);
    const bool moved = fresh_point && s_was_down &&
                       (frame.x != s_lx || frame.y != s_ly);
    const bool status_changed = frame.source_status != s_last_status;

    const char* event = nullptr;
    if (frame.source_status == yoradio::TouchSampleStatus::Error &&
        status_changed) {
        event = "ERROR";
    } else if (frame.source_status == yoradio::TouchSampleStatus::Unavailable &&
               status_changed) {
        event = "UNAVAILABLE";
    } else if (source_down && !s_was_down) {
        event = "DOWN";
    } else if (!source_down && s_was_down) {
        event = "UP";
    } else if (source_down && moved && (now - s_last_move_ms) >= 100u) {
        event = "MOVE";
    }

    if (event) {
        Serial.printf("[TOUCH] %s xy=%ld,%ld tp=%s lv=%s\n",
                      event,
                      static_cast<long>(fed_point.x),
                      static_cast<long>(fed_point.y),
                      touch_status_name(frame.source_status),
                      fed_down ? "PRESSED" : "RELEASED");
        if (source_down) {
            s_last_move_ms = now;
        }
    }

    if (fresh_point) {
        s_was_down = true;
        s_lx = frame.x;
        s_ly = frame.y;
    } else if (!source_down) {
        s_was_down = false;
    }
    s_last_status = frame.source_status;
}
#endif

static void lv_touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
#if (TS_MODEL!=TS_MODEL_UNDEFINED)
    static bool s_prev_touch_down = false;
    const lvgl_ui::LvglTouchFrame frame = s_touch_adapter.read();
    const lvgl_ui::ScreensaverTouchDecision wake =
        apply_screensaver_wake_policy(frame);

    // TouchPort already returned logical display-space coordinates. LVGL
    // preloads data->point with its last raw point; point-less RELEASED frames
    // deliberately preserve it for release handlers.
    lvgl_ui::applyTouchFrameToIndevData(frame, wake.feed_pressed, *data);

    // Track exactly what is fed to LVGL, after fault rearm and wake suppression.
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
#if TS_MODEL==TS_MODEL_GT911
    lv_touch_debug_on_feed(frame, data->state, data->point);
#endif
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

void lvgl_ui::prepareTouchPolling() {
#if (TS_MODEL!=TS_MODEL_UNDEFINED)
    static bool initialized = false;
    if (!initialized) {
        touchscreen.init();
        initialized = true;
    }
    touchscreen.applyPendingFlip();
#endif
}

void lvgl_ui::initTouchIndev() {
    if (s_touch_indev) {
        return;
    }
    if (!lv_display_get_default()) {
        return;
    }
#if (TS_MODEL!=TS_MODEL_UNDEFINED)
    s_touch_indev = lv_indev_create();
    if (!s_touch_indev) {
        return;
    }
    lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch_indev, lv_touch_read_cb);
#endif
}
