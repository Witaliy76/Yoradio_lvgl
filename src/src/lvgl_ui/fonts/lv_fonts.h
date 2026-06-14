#ifndef LV_FONTS_H
#define LV_FONTS_H

/* YoRadio LVGL: Montserrat subset Latin + Cyrillic (generated, Stage 5.1; sizes 18/20/28/32 added for ladder).
 * Enable guards must be 1 in lv_conf.h (LV_FONT_YORA_MONTSERRAT_*_CYR).
 * YoRadio LVGL: подмножество Montserrat Latin + кириллица (генерация 5.1; 18/20/28/32 — более плавная лестница).
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

extern const lv_font_t lv_font_yora_montserrat_12_cyr;
extern const lv_font_t lv_font_yora_montserrat_14_cyr;
extern const lv_font_t lv_font_yora_montserrat_16_cyr;
extern const lv_font_t lv_font_yora_montserrat_18_cyr;
extern const lv_font_t lv_font_yora_montserrat_20_cyr;
extern const lv_font_t lv_font_yora_montserrat_22_cyr;
extern const lv_font_t lv_font_yora_montserrat_28_cyr;
extern const lv_font_t lv_font_yora_montserrat_32_cyr;
extern const lv_font_t lv_font_yora_montserrat_40_cyr;
extern const lv_font_t lv_font_yora_montserrat_48_cyr;
/* Tabler-derived subset: wifi-0..2, wifi, wifi-off @ 18/20/22 px — Main status line. */
extern const lv_font_t lv_font_yora_status_icons_18;
extern const lv_font_t lv_font_yora_status_icons_20;
extern const lv_font_t lv_font_yora_status_icons_22;
/* Tabler subset: Wi-Fi Flow header icon @ 24|36 px — lv_font_yora_wifi_flow_icons.md (per-row icons removed: heap). */
extern const lv_font_t lv_font_yora_wifi_flow_icons_24;
extern const lv_font_t lv_font_yora_wifi_flow_icons_36;
/* Tabler-derived subset: OWM weather glyphs.
 * 18/20/22/24/28 px — status row / Main mini (see lv_font_yora_weather_icons_22.md).
 * 64 px            — Weather Page hero icon (A1). */
extern const lv_font_t lv_font_yora_weather_icons_18;
extern const lv_font_t lv_font_yora_weather_icons_20;
extern const lv_font_t lv_font_yora_weather_icons_22;
extern const lv_font_t lv_font_yora_weather_icons_24;
extern const lv_font_t lv_font_yora_weather_icons_28;
extern const lv_font_t lv_font_yora_weather_icons_64;
/* Tabler-derived subset: Weather Page metric row icons (wind/droplet/gauge/cloud-rain) @ 22 px (A1). */
extern const lv_font_t lv_font_yora_weather_metric_icons_22;
/* Tabler-derived subset: Main control band transport + utility icons @ 18/20/22/24/26/28 px (see lv_font_yora_control_icons.md). */
extern const lv_font_t lv_font_yora_control_icons_18;
extern const lv_font_t lv_font_yora_control_icons_20;
extern const lv_font_t lv_font_yora_control_icons_22;
extern const lv_font_t lv_font_yora_control_icons_24;
extern const lv_font_t lv_font_yora_control_icons_26;
extern const lv_font_t lv_font_yora_control_icons_28;
/* Tabler-derived subset: Station Page current marker (volume-2) @ 14–24 px — pick per profile. */
extern const lv_font_t lv_font_yora_station_icons_14;
extern const lv_font_t lv_font_yora_station_icons_15;
extern const lv_font_t lv_font_yora_station_icons_16;
extern const lv_font_t lv_font_yora_station_icons_17;
extern const lv_font_t lv_font_yora_station_icons_18;
extern const lv_font_t lv_font_yora_station_icons_19;
extern const lv_font_t lv_font_yora_station_icons_20;
extern const lv_font_t lv_font_yora_station_icons_21;
extern const lv_font_t lv_font_yora_station_icons_22;
extern const lv_font_t lv_font_yora_station_icons_23;
extern const lv_font_t lv_font_yora_station_icons_24;
/* Tabler subset: Info Page section rail (Stage 6.2). */
extern const lv_font_t lv_font_yora_info_section_icons_24;
extern const lv_font_t lv_font_yora_info_section_icons_28;
extern const lv_font_t lv_font_yora_info_section_icons_32;
extern const lv_font_t lv_font_yora_info_section_icons_36;
extern const lv_font_t lv_font_yora_info_section_icons_40;
extern const lv_font_t lv_font_yora_info_section_icons_44;

#ifdef __cplusplus
}
#endif

#endif /* LV_FONTS_H */
