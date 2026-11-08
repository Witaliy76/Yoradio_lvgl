#ifndef touchscreen_h
#define touchscreen_h

#include "options.h"
#include "../platform/touch/touch_port.h"
#include <atomic>

#if TS_MODEL == TS_MODEL_GT911
#include "../platform/touch/gt911_backend.h"
#endif

class TouchScreen
#if TS_MODEL != TS_MODEL_UNDEFINED
    : public yoradio::TouchPort
#endif
{
  public:
    TouchScreen()
#if TS_MODEL != TS_MODEL_UNDEFINED
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
#if TS_MODEL != TS_MODEL_UNDEFINED
    yoradio::TouchSample read() override;
    bool available() const override;
#endif
  private:
#if TS_MODEL != TS_MODEL_UNDEFINED
    bool _hwReady;
    std::atomic<bool> _requestedFlip;
    bool _appliedFlip;
    bool _flipAllowed;
#endif
#if TS_MODEL == TS_MODEL_GT911
    yoradio::Gt911Backend _gt911;
#endif
};

extern TouchScreen touchscreen;

#endif
