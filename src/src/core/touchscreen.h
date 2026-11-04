#ifndef touchscreen_h
#define touchscreen_h

#include "options.h"
#include "../platform/touch/touch_port.h"

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
        : _hwReady(false)
#endif
    {}

    void init();
    void flip();
#if TS_MODEL != TS_MODEL_UNDEFINED
    yoradio::TouchSample read() override;
    bool available() const override;
#endif
  private:
#if TS_MODEL != TS_MODEL_UNDEFINED
    bool _hwReady;
#endif
#if TS_MODEL == TS_MODEL_GT911
    yoradio::Gt911Backend _gt911;
#endif
};

extern TouchScreen touchscreen;

#endif
