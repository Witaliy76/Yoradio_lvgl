#ifndef touchscreen_h
#define touchscreen_h
#include "Arduino.h"

enum tsDirection_e { TSD_STAY, TSD_LEFT, TSD_RIGHT, TSD_UP, TSD_DOWN, TDS_REQUEST };

#define TOUCH_MULTI_DELAY       1000  // Задержка между мультитач-событиями
#define TOUCH_MODE_DELAY        100   // Задержка после смены режима

class TouchScreen {
  public:
    TouchScreen()
#if (TS_MODEL!=TS_MODEL_UNDEFINED) && (DSP_MODEL!=DSP_DUMMY)
        : _hwReady(false)
#endif
    {}
    void init();
    void loop();
    void flip();
#if (TS_MODEL!=TS_MODEL_UNDEFINED) && (DSP_MODEL!=DSP_DUMMY)
    // Stage 5.2: LVGL pointer poll only — same chip read + mapping as legacy, no gestures/side effects.
    // Этап 5.2: опрос для LVGL — тот же драйвер и маппинг, без жестов и побочных эффектов.
    bool readPointerForLvgl(uint16_t* outX, uint16_t* outY);
#endif
  private:
    uint16_t _oldTouchX, _oldTouchY, _width, _height;
#if (TS_MODEL!=TS_MODEL_UNDEFINED) && (DSP_MODEL!=DSP_DUMMY)
    bool _hwReady;
#endif
    uint32_t _touchdelay;
    bool _checklpdelay(int m, uint32_t &tstamp);
    tsDirection_e _tsDirection(uint16_t x, uint16_t y);
    bool _istouched();
    #if TS_MODEL==TS_MODEL_GT911
    bool _filterGT911Coordinates(uint16_t rawX, uint16_t rawY);
    #endif
};

extern TouchScreen touchscreen;

#endif
