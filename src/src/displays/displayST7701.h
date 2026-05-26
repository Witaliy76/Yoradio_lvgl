#ifndef displayST7701_h
#define displayST7701_h
#include "../core/options.h"

#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "tools/l10n.h"  // Transitive include for core/ (const_Pl*, weatherFmt, …) on ST7701 builds

// Config struct types (clipArea, ScrollConfig, …); widgets/pages TU disabled on LVGL product (8-E18D).
// Типы конфигов; TU widgets/pages отключены на LVGL product (8-E18D).
#include "widgets/widgets.h"

#if __has_include("conf/displayST7701conf_custom.h")
  #include "conf/displayST7701conf_custom.h"
#else
  #include "conf/displayST7701conf.h"
#endif

#define CHARWIDTH   6
#define CHARHEIGHT  8

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

    // Link stubs — widgets.cpp / pages.cpp still compiled (E18D removes TU).
    // Заглушки линковки — widgets/pages пока в сборке (см. E18D).
    void clearDsp(bool black = false);
    void printClock(uint16_t top, uint16_t rightspace, uint16_t timeheight, bool redraw);
    void clearClock();
    void charSize(uint8_t textsize, uint8_t& width, uint16_t& height);
    void setClipping(clipArea ca);
    void clearClipping();
    void setScrollId(void* scrollid) { _scrollid = scrollid; }
    void* getScrollId() { return _scrollid; }
    void setLastScroller(void* lastScroller) {
        _lastScroller = lastScroller;
        _lastReleaseTime = millis();
    }
    void* getLastScroller() { return _lastScroller; }
    uint32_t getLastReleaseTime() { return _lastReleaseTime; }
    int16_t getScrollWidgetIndex(void* widget);
    void advanceScrollIndex();
    int16_t getLastScrollIndex() const { return _lastScrollIndex; }
    void normalizeScrollIndex();
    void resetScrollIndex() { _lastScrollIndex = -1; }
    void setNumFont();
    uint16_t textWidth(const char* txt);
    uint16_t textWidthN(const char* txt, int n);
    void startWrite(void);
    void endWrite(void);
#ifndef BATTERY_OFF
    void readBattery();
#endif

private:
    bool _clipping;
    clipArea _cliparea;
    void* _scrollid;
    void* _lastScroller;
    uint32_t _lastReleaseTime;
    int16_t _lastScrollIndex;
};

extern DspCore dsp;

#endif
