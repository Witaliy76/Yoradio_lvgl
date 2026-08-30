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

    // Derived runtime + file snapshot for WebUI. No lv_* calls.
    // After a WebUI upload/remove, call notePersistedUserFile so GET /font_status
    // does not reopen LittleFS (that read would disturb RGB after Stage-5 recovery).
    // Производный runtime + снимок файла для WebUI. Без lv_*.
    // После upload/remove вызвать notePersistedUserFile, чтобы GET /font_status
    // не открывал LittleFS (чтение сбивает RGB после Stage-5 recovery).
    static UserFontWebStatus userFontWebStatus();
    static const char* userFontRuntimeCstr(UserFontRuntime runtime);
    static void notePersistedUserFile(bool present, uint32_t size, const uint8_t hdr12[12]);
};

} // namespace lvgl_ui

#endif
