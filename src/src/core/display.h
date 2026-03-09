#ifndef display_h
#define display_h
#include "options.h"

#include "Arduino.h"
#include <Ticker.h>
#include "config.h"
#include "common.h"
#include "../displays/dspcore.h"

// Spectrum Analyzer (always available, runtime switch in config)
#include "../displays/tools/spectrum_analyzer.h"
#include "../displays/tools/spectrum_widget.h"

#if NEXTION_RX!=255 && NEXTION_TX!=255
  #define USE_NEXTION
  #include "../displays/nextion.h"
#endif

#ifndef DUMMYDISPLAY
  #include "../lvgl_ui/lvgl_ui.h"
  void loopDspTask(void * pvParameters);

class Display {
  public:
    uint16_t currentPlItem;
    uint16_t numOfNextStation;
    displayMode_e _mode;
    bool _isStationsChanging = false;  // Флаг смены режима станций
    bool _isVolumeChanging = false;    // Флаг смены режима громкости
  public:
    Display() {};
    displayMode_e mode() { return _mode; }
    void mode(displayMode_e m) { _mode=m; }
    bool isStationsChanging() { return _isStationsChanging; }
    bool isVolumeChanging() { return _isVolumeChanging; }
    void init();
    void loop();
    void _start();
    bool ready() { return _bootStep==2; }
    void resetQueue();
    void putRequest(displayRequestType_e type, int payload=0);
    void flip();
    void invert();
    bool deepsleep();
    void wakeup();
    void setContrast();
    void printPLitem(uint8_t pos, const char* item, bool uppercase);
    void setAIInterpretation(const String& text);  // AI interpretation widget / Виджет AI интерпретации
  private:
    ScrollWidget _meta, _title1, _plcurrent;
    ScrollWidget *_weather;
    ScrollWidget *_title2;
    ScrollWidget *_ai_interpretation;
    BitrateWidget *_fullbitrate;
    FillWidget *_metabackground, *_plbackground;
    SliderWidget *_volbar, *_heapbar;
    Pager _pager;
    Page _footer;
    VuWidget *_vuwidget;
    SpectrumWidget *_spectrumwidget;
    bool _usingSpectrum;
    // AI interpretation pending state (when not on PG_PLAYER page)
    bool _aiPending = false;
    char _aiPendingText[256];
    NumWidget _nums;
    ProgressWidget _testprogress;
    ClockWidget _clock;
    Page *_boot;
    TextWidget *_bootstring, *_volip, *_voltxt, *_rssi, *_bitrate;
    Ticker _returnTicker;
    uint8_t _bootStep;
    bool _suspendFlush;
    lvgl_ui::UiBackend _activeBackend = lvgl_ui::UiBackend::LegacyCanvas;  // Stage 4.1: metadata only
    void _time(bool redraw = false);
    void _apScreen();
    void _swichMode(displayMode_e newmode);
    void _drawPlaylist();
    void _volume();
    void _title();
    void _station();
    void _drawNextStationNum(uint16_t num);
    void _createDspTask();
    void _showDialog(const char *title);
    void _buildPager();
    void _bootScreen();
    void _setReturnTicker(uint8_t time_s);
    void _layoutChange(bool played);
    void _setRSSI(int rssi);
    void _deactivateAllMeters();
    void _applyPendingAI();  // Apply pending AI interpretation when returning to PG_PLAYER
  public:
    Page* getActivePage() const { return _pager.getActivePage(); } // Получить активную страницу для доступа из DspCore
};

#else

class Display {
  public:
    uint16_t currentPlItem;
    uint16_t numOfNextStation;
    displayMode_e _mode;
  public:
    Display() {};
    displayMode_e mode() { return _mode; }
    void mode(displayMode_e m) { _mode=m; }
    void init();
    void _start();
    void putRequest(displayRequestType_e type, int payload=0);
    void loop(){}
    bool ready() { return true; }
    void resetQueue(){}
    void centerText(const char* text, uint8_t y, uint16_t fg, uint16_t bg){}
    void rightText(const char* text, uint8_t y, uint16_t fg, uint16_t bg){}
    void flip(){}
    void invert(){}
    void setContrast(){}
    bool deepsleep(){return true;}
    void wakeup(){}
    void printPLitem(uint8_t pos, const char* item, bool uppercase){}
    void setAIInterpretation(const String& text){}  // AI interpretation widget / Виджет AI интерпретации
};

#endif

extern Display display;


#endif
