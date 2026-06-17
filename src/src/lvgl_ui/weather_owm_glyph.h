#ifndef WEATHER_OWM_GLYPH_H
#define WEATHER_OWM_GLYPH_H

#include <cstring>

// OWM `icon` string (e.g. "01n") → UTF-8 for Tabler weather subset font.
// Weather page hero/forecast icons: lv_font_yora_weather_icons_64 / _36.
// Строка OWM `icon` → UTF-8 глифов Tabler (страница погоды + мини на Main).

// ── Metric row icons (A1) ─────────────────────────────────────────────────────────────────
// lv_font_yora_weather_metric_icons_26: wind / droplets / gauge / cloud-rain / umbrella
// Таблер-иконки для строки метрик (ветер, влажность, давление, осадки, зонт).
// Source: @tabler/icons-webfont 3.26.0 (MIT).
//
// Glyph strings for lv_label_set_text() with lv_font_yora_weather_metric_icons_26.
// Строки глифов для lv_label_set_text() с lv_font_yora_weather_metric_icons_26.
#define YORA_WEATHER_METRIC_GLYPH_WIND      (reinterpret_cast<const char*>(u8"\uEC34"))
#define YORA_WEATHER_METRIC_GLYPH_HUMIDITY  (reinterpret_cast<const char*>(u8"\uFC12"))
#define YORA_WEATHER_METRIC_GLYPH_PRESSURE  (reinterpret_cast<const char*>(u8"\uEAB1"))
#define YORA_WEATHER_METRIC_GLYPH_RAIN      (reinterpret_cast<const char*>(u8"\uEA72"))
// A3.1e: precipitation probability (hero Осадки + hourly %); not OWM condition icon.
// A3.1e: вероятность осадков (hero Осадки + hourly %); не иконка условия OWM.
#define YORA_WEATHER_METRIC_GLYPH_UMBRELLA  (reinterpret_cast<const char*>(u8"\uEBF1"))
// ─────────────────────────────────────────────────────────────────────────────────────────

namespace lvgl_ui {

// Map OpenWeatherMap icon id to one PUA glyph (Tabler 3.26, see lv_font_yora_weather_icons_22.md).
inline const char* weather_owm_icon_to_glyph_utf8(const char* owm_icon) {
    static const char* const k_cloud_rain = reinterpret_cast<const char*>(u8"\uEA72");
    static const char* const k_cloud_storm = reinterpret_cast<const char*>(u8"\uEA74");
    static const char* const k_cloud = reinterpret_cast<const char*>(u8"\uEA76");
    static const char* const k_sun = reinterpret_cast<const char*>(u8"\uEB30");
    static const char* const k_snowflake = reinterpret_cast<const char*>(u8"\uEC0B");
    static const char* const k_cloud_fog = reinterpret_cast<const char*>(u8"\uECD9");
    static const char* const k_moon_stars = reinterpret_cast<const char*>(u8"\uECE7");
    static const char* const k_haze = reinterpret_cast<const char*>(u8"\uEFAA");
    static const char* const k_haze_moon = reinterpret_cast<const char*>(u8"\uFAF8");

    if (!owm_icon || std::strlen(owm_icon) < 3) {
        return k_cloud;
    }
    if (std::strncmp(owm_icon, "01d", 3) == 0) return k_sun;
    if (std::strncmp(owm_icon, "01n", 3) == 0) return k_moon_stars;
    if (std::strncmp(owm_icon, "02d", 3) == 0) return k_haze;
    if (std::strncmp(owm_icon, "02n", 3) == 0) return k_haze_moon;
    if (std::strncmp(owm_icon, "03d", 3) == 0 || std::strncmp(owm_icon, "03n", 3) == 0) return k_cloud;
    if (std::strncmp(owm_icon, "04d", 3) == 0 || std::strncmp(owm_icon, "04n", 3) == 0) return k_cloud;
    if (std::strncmp(owm_icon, "09d", 3) == 0 || std::strncmp(owm_icon, "09n", 3) == 0) return k_cloud_rain;
    // A3: rain (10d/10n) → cloud-rain, not droplet (droplet reserved for humidity metric).
    // A3: дождь (10d/10n) → cloud-rain; droplet — только метрика влажности.
    if (std::strncmp(owm_icon, "10d", 3) == 0 || std::strncmp(owm_icon, "10n", 3) == 0) return k_cloud_rain;
    if (std::strncmp(owm_icon, "11d", 3) == 0 || std::strncmp(owm_icon, "11n", 3) == 0) return k_cloud_storm;
    if (std::strncmp(owm_icon, "13d", 3) == 0 || std::strncmp(owm_icon, "13n", 3) == 0) return k_snowflake;
    if (std::strncmp(owm_icon, "50d", 3) == 0 || std::strncmp(owm_icon, "50n", 3) == 0) return k_cloud_fog;
    return k_cloud;
}

} // namespace lvgl_ui

#endif
