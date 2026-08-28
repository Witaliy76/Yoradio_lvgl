#ifndef SETTINGS_GLYPH_UTF8_H
#define SETTINGS_GLYPH_UTF8_H

// UTF-8 BMP PUA for Settings category icons (Tabler 3.26 — see fonts/readme_fonts.md).
// UTF-8 BMP PUA для иконок категорий Settings (Tabler 3.26).

namespace lvgl_ui {

#define YORA_SETTINGS_GLYPH_DISPLAY     (reinterpret_cast<const char*>(u8"\uEA89"))
#define YORA_SETTINGS_GLYPH_WIFI        (reinterpret_cast<const char*>(u8"\uEB18"))
#define YORA_SETTINGS_GLYPH_MUSIC_RAIL  (reinterpret_cast<const char*>(u8"\uECD4"))
#define YORA_SETTINGS_GLYPH_SLEEP_TIMER (reinterpret_cast<const char*>(u8"\uECE7"))
#define YORA_SETTINGS_GLYPH_AI_LAYER    (reinterpret_cast<const char*>(u8"\uF6D7"))
// 6.7S7: reuse wave-sine until settings icon font gains player-play glyph.
// 6.7S7: временно wave-sine, пока в settings font нет player-play.
#define YORA_SETTINGS_GLYPH_RESUME_ON_STARTUP YORA_SETTINGS_GLYPH_MUSIC_RAIL

} // namespace lvgl_ui

#endif
