// Pure state used by the LVGL TouchPort adapter and narrow host tests.
// No LVGL, Arduino, display, or vendor-driver dependency belongs here.
#ifndef YORADIO_LV_TOUCH_ADAPTER_STATE_H
#define YORADIO_LV_TOUCH_ADAPTER_STATE_H

#include <stdint.h>

#include "../platform/touch/touch_port.h"

namespace lvgl_ui {

enum class LvglPointerFeed : uint8_t {
    Released = 0,
    Pressed,
};

struct LvglTouchFrame {
    LvglPointerFeed feed;
    yoradio::TouchSampleStatus source_status;
    bool has_point;
    bool physical_release;
    uint16_t x;
    uint16_t y;
};

// Only Released proves finger absence. NoData keeps the last trustworthy feed;
// after Error/Unavailable it cannot re-arm a cached/held point.
class LvglTouchAdapterState {
  public:
    LvglTouchAdapterState()
        : rearm_required_(false), pressed_(false), last_x_(0), last_y_(0) {}

    LvglTouchFrame consume(const yoradio::TouchSample& sample) {
        LvglTouchFrame frame{
            LvglPointerFeed::Released,
            sample.status,
            false,
            false,
            0,
            0,
        };

        switch (sample.status) {
            case yoradio::TouchSampleStatus::Valid:
                if (yoradio::hasPressedPoint(sample)) {
                    frame.has_point = true;
                    frame.x = sample.x;
                    frame.y = sample.y;
                    if (!rearm_required_) {
                        frame.feed = LvglPointerFeed::Pressed;
                        pressed_ = true;
                        last_x_ = sample.x;
                        last_y_ = sample.y;
                    }
                }
                break;
            case yoradio::TouchSampleStatus::NoData:
                if (pressed_ && !rearm_required_) {
                    frame.feed = LvglPointerFeed::Pressed;
                    frame.has_point = true;
                    frame.x = last_x_;
                    frame.y = last_y_;
                }
                break;
            case yoradio::TouchSampleStatus::Released:
                frame.physical_release = true;
                rearm_required_ = false;
                pressed_ = false;
                break;
            case yoradio::TouchSampleStatus::Error:
            case yoradio::TouchSampleStatus::Unavailable:
                rearm_required_ = true;
                pressed_ = false;
                break;
        }
        return frame;
    }

    bool rearmRequired() const { return rearm_required_; }

  private:
    bool rearm_required_;
    bool pressed_;
    uint16_t last_x_;
    uint16_t last_y_;
};

struct ScreensaverTouchDecision {
    bool feed_pressed;
    bool wake_requested;
};

// Product wake-only policy remains distinct from backend fault rearm. A
// faulted sample never clears the per-stroke suppression latch.
class ScreensaverTouchState {
  public:
    ScreensaverTouchState()
        : finger_down_(false),
          wake_sent_this_stroke_(false),
          suppress_until_release_(false),
          last_x_(0),
          last_y_(0) {}

    ScreensaverTouchDecision consume(const LvglTouchFrame& frame,
                                     bool saver_or_blank_active) {
        if (frame.physical_release) {
            finger_down_ = false;
            wake_sent_this_stroke_ = false;
            suppress_until_release_ = false;
            return ScreensaverTouchDecision{false, false};
        }

        if (frame.feed != LvglPointerFeed::Pressed || !frame.has_point) {
            return ScreensaverTouchDecision{false, false};
        }

        if (!saver_or_blank_active) {
            return ScreensaverTouchDecision{!suppress_until_release_, false};
        }

        const bool new_press = !finger_down_;
        const bool moved = finger_down_ &&
                           (frame.x != last_x_ || frame.y != last_y_);
        finger_down_ = true;
        last_x_ = frame.x;
        last_y_ = frame.y;

        bool wake_requested = false;
        if (!wake_sent_this_stroke_ && (new_press || moved)) {
            wake_sent_this_stroke_ = true;
            suppress_until_release_ = true;
            wake_requested = true;
        }
        return ScreensaverTouchDecision{!suppress_until_release_, wake_requested};
    }

    bool suppressionActive() const { return suppress_until_release_; }

  private:
    bool finger_down_;
    bool wake_sent_this_stroke_;
    bool suppress_until_release_;
    uint16_t last_x_;
    uint16_t last_y_;
};

}  // namespace lvgl_ui

#endif  // YORADIO_LV_TOUCH_ADAPTER_STATE_H
