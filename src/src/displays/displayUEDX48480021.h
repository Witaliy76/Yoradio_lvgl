// Adaptation: Witaliy76 - https://github.com/Witaliy76
#ifndef displayUEDX48480021_h
#define displayUEDX48480021_h
#include "../core/options.h"

#include "Arduino.h"
#include "Arduino_GFX_Library.h"

#if CLOCKFONT_MONO
  #include "fonts/DS_DIGI56pt7b_mono.h"        // https://tchapi.github.io/Adafruit-GFX-Font-Customiser/
#else
  #include "fonts/DS_DIGI56pt7b.h"
#endif
#define CHARWIDTH   6
#define CHARHEIGHT  8

using Canvas = Arduino_Canvas;

#include "widgets/widgets.h"
#include "widgets/pages.h"

#if __has_include("conf/displayUEDX48480021conf_custom.h")
  #include "conf/displayUEDX48480021conf_custom.h"
#else
  #include "conf/displayUEDX48480021conf.h"
#endif

#define BOOT_PRG_COLOR    0xE68B
#define BOOT_TXT_COLOR    0xFFFF
#define PINK              0xF97F

#define ST7701_SLPIN     0x10
#define ST7701_SLPOUT    0x11
#define ST7701_DISPOFF   0x28
#define ST7701_DISPON    0x29

class DspCore {
public:
    uint16_t plItemHeight;
    uint16_t plTtemsCount;
    uint16_t plCurrentPos;
    int plYStart;

    DspCore();
    void initDisplay();
    void displayOn();
    void displayOff();
    void drawLogo(uint16_t top);
    void clearDsp(bool black=false);
    void printClock();
    void printClock(uint16_t top, uint16_t rightspace, uint16_t timeheight, bool redraw);
    void clearClock();
    void drawPlaylist(uint16_t currentItem);
    void loop(bool force=false);
    void charSize(uint8_t textsize, uint8_t& width, uint16_t& height);
    void setTextSize(uint8_t s);
    uint16_t width();
    uint16_t height();
    void flip();
    void invert();
    void sleep();
    void wake();
    void setBrightness(uint8_t brightness);
    
    void writePixel(int16_t x, int16_t y, uint16_t color);
    void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void setClipping(clipArea ca);
    void clearClipping();
    void setScrollId(void * scrollid) { _scrollid = scrollid; }
    void * getScrollId() { return _scrollid; }
    void setLastScroller(void * lastScroller) { _lastScroller = lastScroller; _lastReleaseTime = millis(); }
    void * getLastScroller() { return _lastScroller; }
    uint32_t getLastReleaseTime() { return _lastReleaseTime; }
    int16_t getScrollWidgetIndex(void* widget); // Получить порядковый индекс ScrollWidget'а в активной странице
    void advanceScrollIndex(); // Увеличить индекс последнего скроллившегося виджета
    int16_t getLastScrollIndex() const { return _lastScrollIndex; } // Получить индекс последнего скроллившегося
    void normalizeScrollIndex(); // Нормализовать lastIndex при изменении состава scrollable-виджетов
    void resetScrollIndex() { _lastScrollIndex = -1; } // Сбросить индекс для немедленного перехода к следующему виджету
    Arduino_G* getOutputDisplay(); // Get output display for line-canvas creation
    void setNumFont();
    uint16_t textWidth(const char *txt);
    uint16_t textWidthN(const char *txt, int n);
    uint16_t textWidthGFX(const char *txt, uint8_t textsize);
    void printPLitem(uint8_t pos, const char* item, ScrollWidget& current, bool uppercase);
    void startWrite(void);
    void endWrite(void);
    uint8_t _charWidth(unsigned char c);
#ifndef BATTERY_OFF
    void readBattery();
#endif
private:
    char  _timeBuf[20], _dateBuf[20], _oldTimeBuf[20], _oldDateBuf[20], _bufforseconds[4], _buffordate[40];
    uint16_t _timewidth, _timeleft, _datewidth, _dateleft, _oldtimeleft, _oldtimewidth, _olddateleft, _olddatewidth, clockTop, clockRightSpace, clockTimeHeight, _dotsLeft;
    uint16_t _oldClockTop;  // previous clock Y (clear ghost when position changes / очистка при смене позиции)
    bool _clipping, _printdots;
    clipArea _cliparea;
    void * _scrollid;
    void * _lastScroller; // Last widget that finished scrolling (for round-robin fairness)
    uint32_t _lastReleaseTime; // Time when slot was last released (for single-widget timeout)
    int16_t _lastScrollIndex; // Индекс последнего скроллившегося виджета в порядке добавления
    void _getTimeBounds();
    void _clockSeconds();
    void _clockDate();
    void _clockTime();
};

extern DspCore dsp;

#endif
