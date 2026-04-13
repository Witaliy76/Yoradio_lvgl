#ifndef CONTROL_GLYPH_UTF8_H
#define CONTROL_GLYPH_UTF8_H

// UTF-8 BMP PUA for lv_font_yora_control_icons_* (Tabler 3.26 — see lv_font_yora_control_icons.md).
// UTF-8 BMP PUA для шрифта control icons (Tabler 3.26).

namespace lvgl_ui {

inline const char* control_glyph_utf8_player_skip_back() {
    return reinterpret_cast<const char*>(u8"\uED48");
}
inline const char* control_glyph_utf8_player_play() {
    return reinterpret_cast<const char*>(u8"\uED46");
}
inline const char* control_glyph_utf8_player_stop() {
    return reinterpret_cast<const char*>(u8"\uED4A");
}
inline const char* control_glyph_utf8_player_skip_forward() {
    return reinterpret_cast<const char*>(u8"\uED49");
}
inline const char* control_glyph_utf8_list() {
    return reinterpret_cast<const char*>(u8"\uEB6B");
}
inline const char* control_glyph_utf8_settings() {
    return reinterpret_cast<const char*>(u8"\uEB20");
}

} // namespace lvgl_ui

#endif
