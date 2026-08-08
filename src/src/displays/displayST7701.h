// Adaptation: Witaliy76 - https://github.com/Witaliy76
#ifndef displayST7701_h
#define displayST7701_h
#include "../core/options.h"

#include "Arduino.h"

class DspCore {
public:
    DspCore();
    // Returns DisplayPort::begin() success / Возвращает успех DisplayPort::begin().
    bool initDisplay();
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
#ifndef BATTERY_OFF
    void readBattery();
#endif
};

extern DspCore dsp;

#endif
