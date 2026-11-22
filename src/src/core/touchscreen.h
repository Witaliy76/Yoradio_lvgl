#ifndef touchscreen_h
#define touchscreen_h

#include "options.h"
#include "../platform/touch/touch_port.h"
#include <atomic>

#if YORADIO_HAS_TOUCH
// The board profile names the touch controller; GT911 is the only backend until
// S07_bis-5 moves the choice into platform/composition.h.
// Контроллер тача задаёт профиль платы; до S07_bis-5 (platform/composition.h)
// единственный backend — GT911.
#include "../platform/touch/gt911_backend.h"
static_assert(yoradio::kActiveBoard.touch.controller == yoradio::TouchController::Gt911,
              "YoRadio: the selected board's touch controller has no backend yet (S07_bis-5 composition).");
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
    yoradio::Gt911Backend _gt911;
#endif
};

extern TouchScreen touchscreen;

#endif
