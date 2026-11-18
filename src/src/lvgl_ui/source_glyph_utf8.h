// Source-carousel glyphs from the embedded Tabler subset. No separate icon engine.
// Глифы карусели источников из встроенного subset Tabler. Отдельного icon engine нет.
// Authoring: @tabler/icons-webfont 3.26.0 (MIT). See fonts/readme_fonts.md.
// wave-sine (U+ECD4) is the existing settings glyph, reused for I2S Input.
// wave-sine (U+ECD4) — уже существующий глиф настроек, повторно для I2S Input.
#ifndef YORADIO_SOURCE_GLYPH_UTF8_H
#define YORADIO_SOURCE_GLYPH_UTF8_H

#include <cstring>

namespace lvgl_ui {

inline const char* source_glyph_utf8_for_key(const char* icon_key) {
    if (!icon_key) return "";
    struct Row {
        const char* key;
        const char* utf8;
    };
    static const Row kRows[] = {
        { "radio", reinterpret_cast<const char*>(u8"\uEF2D") },
        { "sd", reinterpret_cast<const char*>(u8"\uF384") },
        { "usb", reinterpret_cast<const char*>(u8"\uF00C") },
        { "cast", reinterpret_cast<const char*>(u8"\uEA56") },
        { "folder-share", reinterpret_cast<const char*>(u8"\uF919") },
        { "brand-yandex", reinterpret_cast<const char*>(u8"\uFAE1") },
        { "brand-spotify", reinterpret_cast<const char*>(u8"\uED03") },
        { "brand-apple", reinterpret_cast<const char*>(u8"\uEC17") },
        { "bluetooth", reinterpret_cast<const char*>(u8"\uEA37") },
        { "plug-connected", reinterpret_cast<const char*>(u8"\uF00A") },
        { "wave-sine", reinterpret_cast<const char*>(u8"\uECD4") },
    };
    for (const Row& row : kRows) {
        if (std::strcmp(row.key, icon_key) == 0) return row.utf8;
    }
    return "";
}

}  // namespace lvgl_ui

#endif
