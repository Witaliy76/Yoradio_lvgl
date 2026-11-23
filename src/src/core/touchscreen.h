#ifndef touchscreen_h
#define touchscreen_h

#include "options.h"
#include "../platform/touch/touch_port.h"
#include <atomic>

#if YORADIO_HAS_TOUCH
// platform/composition.h chooses the touch backend from the board profile;
// touch_backend_select.h maps that choice to a class (GT911 or FT5x06).
// Backend тача выбирает platform/composition.h по профилю платы;
// touch_backend_select.h сопоставляет выбор с классом (GT911 или FT5x06).
#include "../platform/touch/touch_backend_select.h"
#endif

class TouchScreen
#if YORADIO_HAS_TOUCH
    : public yoradio::TouchPort
#endif
{
  public:
    TouchScreen()
#if YORADIO_HAS_TOUCH
        : _hwReady(false), _requestedFlip(false), _appliedFlip(false),
          _flipAllowed(false)
#endif
    {}

    // DspTask only: init before the first LVGL timer/pointer poll.
    void init();
    // Cross-task publication only; never touches backend/config/LVGL state.
    void requestFlip(bool flipped);
    // DspTask only, between TouchPort polls, never inside read().
    void applyPendingFlip();
#if YORADIO_HAS_TOUCH
    yoradio::TouchSample read() override;
    bool available() const override;
#endif
  private:
#if YORADIO_HAS_TOUCH
    bool _hwReady;
    std::atomic<bool> _requestedFlip;
    bool _appliedFlip;
    bool _flipAllowed;
#endif
#if YORADIO_HAS_TOUCH
    yoradio::ActiveTouchBackend _backend;
#endif
};

extern TouchScreen touchscreen;

#endif
