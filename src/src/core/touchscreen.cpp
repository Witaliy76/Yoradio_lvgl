/**
 * @file touchscreen.cpp
 * @brief Production TouchPort owner; GT911 is the only backend (board profile, S07_bis-5 composition).
 */

#include "options.h"
#if YORADIO_HAS_TOUCH

#include "touchscreen.h"

#include "../platform/touch/resolved_touch_config.h"

void TouchScreen::init() {
    const yoradio::ResolvedTouchConfig touch =
        yoradio::resolveTouchConfig(_requestedFlip.load(std::memory_order_acquire));
    _appliedFlip = touch.flip_touch;
#if YORADIO_HAS_TOUCH
    _hwReady = _gt911.begin(touch);
#else
    _hwReady = false;
#endif
    // No stroke has been fed yet. Subsequent samples own the release gate.
    _flipAllowed = _hwReady;
}

bool TouchScreen::available() const {
#if YORADIO_HAS_TOUCH
    return _hwReady && _gt911.available();
#else
    return false;
#endif
}

yoradio::TouchSample TouchScreen::read() {
    if (!available()) {
        _flipAllowed = false;
        return yoradio::makeTouchUnavailable();
    }

#if YORADIO_HAS_TOUCH
    const yoradio::TouchSample sample = _gt911.read();
    switch (sample.status) {
        case yoradio::TouchSampleStatus::Valid:
        case yoradio::TouchSampleStatus::Error:
        case yoradio::TouchSampleStatus::Unavailable:
            _flipAllowed = false;
            break;
        case yoradio::TouchSampleStatus::Released:
            _flipAllowed = true;
            break;
        case yoradio::TouchSampleStatus::NoData:
            break;  // Neither a held stroke nor fault uncertainty is released.
    }
    return sample;
#else
    return yoradio::makeTouchUnavailable();
#endif
}

void TouchScreen::requestFlip(bool flipped) {
    _requestedFlip.store(flipped, std::memory_order_release);
}

void TouchScreen::applyPendingFlip() {
#if YORADIO_HAS_TOUCH
    if (!_flipAllowed || !available()) {
        return;
    }
    const bool flipped = _requestedFlip.load(std::memory_order_acquire);
    if (flipped == _appliedFlip) {
        return;
    }
    if (_gt911.setOrientation(yoradio_touch_orientation(flipped))) {
        _appliedFlip = flipped;
    } else {
        // Never expose coordinates with partially applied setter flags.
        _hwReady = false;
        _flipAllowed = false;
    }
#endif
}

#endif  // YORADIO_HAS_TOUCH
