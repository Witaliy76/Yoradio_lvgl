/**
 * @file touchscreen.cpp
 * @brief Production TouchPort owner for the official GT911 backend.
 */

#include "options.h"
#if (TS_MODEL != TS_MODEL_UNDEFINED)

#include "touchscreen.h"

#include "config.h"
#include "../platform/touch/resolved_touch_config.h"

void TouchScreen::init() {
    const yoradio::ResolvedTouchConfig touch =
        yoradio::resolveTouchConfig(config.store.fliptouch);
#if TS_MODEL == TS_MODEL_GT911
    _hwReady = _gt911.begin(touch);
#else
    _hwReady = false;
#endif
}

bool TouchScreen::available() const {
    const yoradio::ResolvedTouchConfig touch =
        yoradio::resolveTouchConfig(config.store.fliptouch);
#if TS_MODEL == TS_MODEL_GT911
    return _hwReady && touch.available && _gt911.available();
#else
    return false;
#endif
}

yoradio::TouchSample TouchScreen::read() {
    if (!available()) {
        return yoradio::makeTouchUnavailable();
    }

#if TS_MODEL == TS_MODEL_GT911
    return _gt911.read();
#else
    return yoradio::makeTouchUnavailable();
#endif
}

void TouchScreen::flip() {
#if TS_MODEL == TS_MODEL_GT911
    const yoradio::ResolvedTouchConfig touch =
        yoradio::resolveTouchConfig(config.store.fliptouch);
    _hwReady = _gt911.setOrientation(touch.orientation);
#endif
}

#endif  // TS_MODEL != TS_MODEL_UNDEFINED
