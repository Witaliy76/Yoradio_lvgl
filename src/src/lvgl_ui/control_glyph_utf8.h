// Author: Witaliy76 - https://github.com/Witaliy76
#ifndef CONTROL_GLYPH_UTF8_H
#define CONTROL_GLYPH_UTF8_H

// UTF-8 BMP PUA for Main control icons (Tabler 3.26 — see fonts/readme_fonts.md).
// UTF-8 BMP PUA для иконок полосы управления (Tabler 3.26).

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
inline const char* control_glyph_utf8_chevron_right() {
    return reinterpret_cast<const char*>(u8"\uEA61");
}
inline const char* control_glyph_utf8_chevron_left() {
    return reinterpret_cast<const char*>(u8"\uEA60");
}
inline const char* control_glyph_utf8_volume_2() {
    return reinterpret_cast<const char*>(u8"\uEB4F");
}

// Station Page — same Tabler PUA subset (marker + bottom hint), not the bottom control bar.
// Station Page — тот же subset Tabler (маркер и подсказка); не нижняя панель плеера.
inline const char* station_glyph_utf8_volume_2() {
    return reinterpret_cast<const char*>(u8"\uEB4F");
}
inline const char* station_glyph_utf8_hand_click() {
    return reinterpret_cast<const char*>(u8"\uEF4F");
}

} // namespace lvgl_ui

#endif
