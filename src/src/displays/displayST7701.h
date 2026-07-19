#ifndef displayST7701_h
#define displayST7701_h
#include "../core/options.h"

#include "Arduino.h"
#include "Arduino_GFX_Library.h"

class DspCore {
public:
    DspCore();
    void initDisplay();
    void displayOn();
    void displayOff();
    void loop(bool force = false);
    uint16_t width();
    uint16_t height();
    void flip();
    void invert();
    void sleep();
    void wake();
    void setBrightness(uint8_t brightness);
    Arduino_G* getOutputDisplay();
#ifndef BATTERY_OFF
    void readBattery();
#endif
};

extern DspCore dsp;

#endif
