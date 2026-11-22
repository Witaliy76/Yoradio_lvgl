#ifndef touchscreen_h
#define touchscreen_h

#include "options.h"
#include "../platform/touch/touch_port.h"
#include <atomic>

#if YORADIO_HAS_TOUCH
// platform/composition.h chooses the touch backend from the board profile.
// GT911 is the only implemented backend; another choice must add its member here.
// Backend тача выбирает platform/composition.h по профилю платы. Реализован
// только GT911; другой выбор должен добавить здесь свой член класса.
#include "../platform/composition.h"
#include "../platform/touch/gt911_backend.h"
static_assert(yoradio::composition::kTouchBackend == yoradio::composition::TouchBackend::Gt911,
              "YoRadio: TouchScreen implements only the GT911 backend chosen by platform/composition.h.");
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
