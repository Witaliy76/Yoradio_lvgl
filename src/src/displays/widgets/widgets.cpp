// Legacy Canvas widgets removed in Block 8-E18D. LVGL is the only product UI.
// Legacy Canvas widgets удалены в Block 8-E18D. Продуктовый UI — только LVGL.

#include "../dspcore.h"
#if DSP_MODEL != DSP_DUMMY

#include "widgets.h"

TextWidget::TextCache* TextWidget::_textCache = nullptr;
size_t TextWidget::_cacheSize = 0;

TextWidget::~TextWidget() {
    if (_text) free(_text);
    if (_oldtext) free(_oldtext);
    _text = _oldtext = nullptr;
}

void TextWidget::init(WidgetConfig wconf, uint16_t buffsize, bool uppercase, uint16_t fgcolor,
                      uint16_t bgcolor) {
    Widget::init(wconf, fgcolor, bgcolor);
    _uppercase = uppercase;
    _buffsize = buffsize;
    _textwidth = _oldtextwidth = _oldleft = 0;
    _textheight = 0;
    _charWidth = 6;
    if (_text) free(_text);
    if (_oldtext) free(_oldtext);
    _text = static_cast<char*>(calloc(buffsize + 1, 1));
    _oldtext = static_cast<char*>(calloc(buffsize + 1, 1));
}

void TextWidget::setText(const char* txt) { (void)txt; }
void TextWidget::setText(int val, const char* format) { (void)val; (void)format; }
void TextWidget::setText(const char* txt, const char* format) { (void)txt; (void)format; }
void TextWidget::_draw() {}
uint16_t TextWidget::_realLeft() { return _config.left; }
char* TextWidget::_getCachedText(const char* txt) { return const_cast<char*>(txt); }
void TextWidget::_addToCache(const char* txt, char* value) { (void)txt; (void)value; }
void TextWidget::_clearCache() {}

void FillWidget::init(FillConfig conf, uint16_t bgcolor) {
    Widget::init(conf.widget, bgcolor, bgcolor);
    _height = conf.height;
    _lastDraw.valid = false;
}
void FillWidget::setHeight(uint16_t newHeight) { _height = newHeight; }
void FillWidget::_fastFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    (void)x; (void)y; (void)w; (void)h; (void)color;
}
void FillWidget::_draw() {}
void FillWidget::_clear() {}

ScrollWidget::ScrollWidget(const char* separator, ScrollConfig conf, uint16_t fgcolor,
                           uint16_t bgcolor) {
    init(separator, conf, fgcolor, bgcolor);
}
ScrollWidget::~ScrollWidget() {
    if (_sep) free(_sep);
    if (_window) free(_window);
    _sep = _window = nullptr;
}
void ScrollWidget::init(const char* separator, ScrollConfig conf, uint16_t fgcolor, uint16_t bgcolor) {
    TextWidget::init(conf.widget, conf.buffsize, conf.uppercase, fgcolor, bgcolor);
    _sep = separator ? strdup(separator) : nullptr;
    _window = static_cast<char*>(calloc(conf.buffsize + 1, 1));
    _doscroll = false;
}
void ScrollWidget::loop() {}
void ScrollWidget::setText(const char* txt) { (void)txt; }
void ScrollWidget::setText(const char* txt, const char* format) { (void)txt; (void)format; }
void ScrollWidget::setActive(bool act, bool clr) { TextWidget::setActive(act, clr); }
void ScrollWidget::lock(bool lck) { TextWidget::lock(lck); }
void ScrollWidget::ensureScrollMetrics() {}
bool ScrollWidget::canParticipateInScroll() { return false; }
void ScrollWidget::_setTextParams() {}
void ScrollWidget::_calcX() {}
void ScrollWidget::_drawFrame() {}
void ScrollWidget::_draw() {}
bool ScrollWidget::_checkIsScrollNeeded() { return false; }
bool ScrollWidget::_checkDelay(int m, uint32_t& tstamp) { (void)m; (void)tstamp; return false; }
void ScrollWidget::_clear() {}
void ScrollWidget::_reset() {}
void ScrollWidget::_rebuildCycleIfNeeded() {}

void SliderWidget::init(FillConfig conf, uint16_t fgcolor, uint16_t bgcolor, uint32_t maxval,
                        uint16_t oucolor) {
    Widget::init(conf.widget, fgcolor, bgcolor);
    _height = conf.height;
    _max = maxval;
    _value = 0;
    _outlined = conf.outlined;
    _oucolor = oucolor;
    _oldvalwidth = 0;
}
void SliderWidget::setValue(uint32_t val) { _value = val; }
void SliderWidget::_draw() {}
void SliderWidget::_drawslider() {}
void SliderWidget::_clear() {}
void SliderWidget::_reset() {}

VuWidget::~VuWidget() {}
void VuWidget::init(WidgetConfig wconf, VUBandsConfig bands, uint16_t vumaxcolor, uint16_t vumincolor,
                    uint16_t bgcolor) {
    Widget::init(wconf, vumaxcolor, bgcolor);
    _bands = bands;
    _vumaxcolor = vumaxcolor;
    _vumincolor = vumincolor;
    numBands = 0;
}
void VuWidget::loop() {}
void VuWidget::setActive(bool act, bool clr) { Widget::setActive(act, clr); }
void VuWidget::_draw() {}
void VuWidget::_clear() {}

void NumWidget::init(WidgetConfig wconf, uint16_t buffsize, bool uppercase, uint16_t fgcolor,
                     uint16_t bgcolor) {
    TextWidget::init(wconf, buffsize, uppercase, fgcolor, bgcolor);
}
void NumWidget::setText(const char* txt) { TextWidget::setText(txt); }
void NumWidget::setText(int val, const char* format) { TextWidget::setText(val, format); }
void NumWidget::_getBounds() {}
void NumWidget::_draw() {}
void NumWidget::_clear() {}

void ProgressWidget::loop() {}
void ProgressWidget::_progress() {}
bool ProgressWidget::_checkDelay(int m, uint32_t& tstamp) { (void)m; (void)tstamp; return false; }
void ProgressWidget::_clear() {}

void ClockWidget::draw() {}
void ClockWidget::_draw() {}
void ClockWidget::_clear() {}

void BitrateWidget::init(BitrateConfig bconf, uint16_t fgcolor, uint16_t bgcolor) {
    Widget::init(bconf.widget, fgcolor, bgcolor);
    _dimension = bconf.dimension;
    _format = BF_UNCNOWN;
    _buf[0] = '\0';
    _charWidth = 6;
    _textheight = 8;
    _bitrate = 0;
}
void BitrateWidget::setBitrate(uint16_t bitrate) { _bitrate = bitrate; }
void BitrateWidget::setFormat(BitrateFormat format) { _format = format; }
void BitrateWidget::_draw() {}
void BitrateWidget::_clear() {}

#endif // DSP_MODEL != DSP_DUMMY
