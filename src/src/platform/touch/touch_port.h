// TouchPort is the LVGL-neutral primary-pointer boundary.
#ifndef YORADIO_PLATFORM_TOUCH_TOUCH_PORT_H
#define YORADIO_PLATFORM_TOUCH_TOUCH_PORT_H

#include <stdint.h>

namespace yoradio {

enum class TouchSampleStatus : uint8_t {
    Valid = 0,
    NoData,
    Error,
    Unavailable,
};

struct TouchSample {
    TouchSampleStatus status;
    bool pressed;
    uint16_t x;
    uint16_t y;
};

constexpr TouchSample makeTouchPressed(uint16_t x, uint16_t y) {
    return TouchSample{TouchSampleStatus::Valid, true, x, y};
}

constexpr TouchSample makeTouchNoData() {
    return TouchSample{TouchSampleStatus::NoData, false, 0, 0};
}

constexpr TouchSample makeTouchError() {
    return TouchSample{TouchSampleStatus::Error, false, 0, 0};
}

constexpr TouchSample makeTouchUnavailable() {
    return TouchSample{TouchSampleStatus::Unavailable, false, 0, 0};
}

constexpr bool hasPressedPoint(const TouchSample& sample) {
    return sample.status == TouchSampleStatus::Valid && sample.pressed;
}

class TouchPort {
  public:
    virtual ~TouchPort() = default;
    virtual TouchSample read() = 0;
    virtual bool available() const = 0;
};

}  // namespace yoradio

#endif  // YORADIO_PLATFORM_TOUCH_TOUCH_PORT_H
