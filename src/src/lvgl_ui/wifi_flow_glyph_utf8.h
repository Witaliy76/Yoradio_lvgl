// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef WIFI_FLOW_GLYPH_UTF8_H
#define WIFI_FLOW_GLYPH_UTF8_H

// UTF-8 BMP PUA for lv_font_yora_wifi_flow_icons_24|36 (Tabler wifi @ U+EB52) — header label only.
// Глиф только для заголовка Wi‑Fi Flow; не смешивать с Montserrat в одном label.

namespace lvgl_ui {

inline const char* wifi_flow_glyph_utf8_wifi_full() {
    return reinterpret_cast<const char*>(u8"\uEB52");
}

} // namespace lvgl_ui

#endif
