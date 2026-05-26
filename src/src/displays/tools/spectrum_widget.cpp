// Legacy Canvas SpectrumWidget removed in Block 8-E18D. LVGL is the only product UI.
// Audio FFT data path: spectrum_analyzer.cpp (independent).
// Legacy Canvas SpectrumWidget удалён в Block 8-E18D. Данные FFT: spectrum_analyzer.cpp.

#include "spectrum_widget.h"

SpectrumWidget::~SpectrumWidget() {}

void SpectrumWidget::init(SpectrumWidgetConfig conf) {
    _config = conf;
    _barColor = conf.barColor;
    _peakColor = conf.peakColor;
    _showPeaks = conf.showPeaks;
    _showGrid = conf.showGrid;
}

void SpectrumWidget::loop() {}

void SpectrumWidget::setActive(bool act, bool clr) {
    Widget::setActive(act, clr);
}

void SpectrumWidget::_draw() {}
void SpectrumWidget::_clear() {}
void SpectrumWidget::_drawBar(uint16_t x, uint16_t width, uint16_t barHeight, float value,
                              float peak) {
    (void)x; (void)width; (void)barHeight; (void)value; (void)peak;
}
void SpectrumWidget::_drawGrid() {}
uint16_t SpectrumWidget::_applyGradient(uint16_t baseColor, float progress) {
    (void)progress;
    return baseColor;
}
uint16_t SpectrumWidget::_blendColors(uint16_t color1, uint16_t color2, float ratio) {
    (void)ratio;
    return color1;
}
uint16_t SpectrumWidget::_darkenColor(uint16_t color, float factor) {
    (void)factor;
    return color;
}
uint16_t SpectrumWidget::_brightenColor(uint16_t color, float factor) {
    (void)factor;
    return color;
}
