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
/* Tabler-derived subset: OWM weather mini @ 18/20/22/24/28 px — Main bottom row (see lv_font_yora_weather_icons_22.md). */
extern const lv_font_t lv_font_yora_weather_icons_18;
extern const lv_font_t lv_font_yora_weather_icons_20;
extern const lv_font_t lv_font_yora_weather_icons_22;
extern const lv_font_t lv_font_yora_weather_icons_24;
extern const lv_font_t lv_font_yora_weather_icons_28;
/* Tabler-derived subset: Main control band transport + utility icons @ 18/20/22/24/26 px (see lv_font_yora_control_icons.md). */
extern const lv_font_t lv_font_yora_control_icons_18;
extern const lv_font_t lv_font_yora_control_icons_20;
extern const lv_font_t lv_font_yora_control_icons_22;
extern const lv_font_t lv_font_yora_control_icons_24;
extern const lv_font_t lv_font_yora_control_icons_26;

#ifdef __cplusplus
}
#endif

#endif /* LV_FONTS_H */
