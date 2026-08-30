// Production text/icon font ownership seam for LVGL UI.
// Единая production-точка владения текстовыми и icon-шрифтами LVGL UI.
#ifndef YORADIO_FONT_PROVIDER_H
#define YORADIO_FONT_PROVIDER_H

#include <cstdint>

#include "lvgl.h"

namespace lvgl_ui {

enum class UserFontRuntime : uint8_t {
    Emergency = 0,
    Factory = 1,
    UserActive = 2,
    Rejected = 3,
};

struct UserFontWebStatus {
    UserFontRuntime runtime = UserFontRuntime::Factory;
    bool provider_ready = false;
    bool file_present = false;
    uint32_t file_size = 0;
    bool reboot_required = false;
};

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

    // Derived runtime + LittleFS snapshot for WebUI. No lv_* calls.
    // Производный runtime + снимок LittleFS для WebUI. Без вызовов lv_*.
    static UserFontWebStatus userFontWebStatus();
    static const char* userFontRuntimeCstr(UserFontRuntime runtime);
};

} // namespace lvgl_ui

#endif
