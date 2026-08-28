// Production text/icon font ownership seam for LVGL UI.
// Единая production-точка владения текстовыми и icon-шрифтами LVGL UI.
#ifndef YORADIO_FONT_PROVIDER_H
#define YORADIO_FONT_PROVIDER_H

#include <cstdint>

#include "lvgl.h"

namespace lvgl_ui {

class FontProvider final {
public:
    // Call once on the LVGL-owning DspTask before the first Boot label.
    static bool begin();

    // The embedded TTFs are scalable: any positive px may be requested. Returned
    // pointers remain stable for the lifetime of the UI.
    // Встроенные TTF масштабируемы: любой positive px. Указатели стабильны на жизнь UI.
    static const lv_font_t* text(uint16_t px);
    static const lv_font_t* icon(uint16_t px);

    static bool primaryAvailable();
};

} // namespace lvgl_ui

#endif
