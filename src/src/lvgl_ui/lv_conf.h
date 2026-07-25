/**
 * @file lv_conf.h
 * LVGL 8.3 compile-time configuration for YoRadio.
 * Конфигурация компиляции LVGL 8.3 для YoRadio.
 * Author: Witaliy76 - https://github.com/Witaliy76
 */

#if 1 /* Set to "1" to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_SCREEN_TRANSP 0
#define LV_COLOR_MIX_ROUND_OFS 0
#define LV_COLOR_CHROMA_KEY lv_color_hex(0x00ff00)

/*=========================
   MEMORY SETTINGS
 *=========================*/

#define LV_MEM_CUSTOM 0
#if LV_MEM_CUSTOM == 0
    /* Compile-time logical TLSF pool size in KiB (default 48). Experiment may set 64 via build flag.
     * Логический размер TLSF pool в KiB (по умолчанию 48). Эксперимент может задать 64 через build flag. */
    #ifndef YORADIO_LVGL_POOL_SIZE_KIB
        #define YORADIO_LVGL_POOL_SIZE_KIB 48U
    #endif
    #define LV_MEM_SIZE (YORADIO_LVGL_POOL_SIZE_KIB * 1024U)
    #define LV_MEM_ADR 0
    /* Compile-time pool placement: 0=static DRAM work_mem_int, 1=PSRAM via project wrapper.
     * Размещение pool: 0=static DRAM work_mem_int, 1=PSRAM через project wrapper. */
    #ifndef YORADIO_LVGL_POOL_IN_PSRAM
        #define YORADIO_LVGL_POOL_IN_PSRAM 0
    #endif
    #if LV_MEM_ADR == 0
        #if YORADIO_LVGL_POOL_IN_PSRAM
            #define LV_MEM_POOL_INCLUDE "lv_mem_pool_psram.h"
            #define LV_MEM_POOL_ALLOC(size) yoradio_lvgl_pool_alloc(size)
        #else
            #undef LV_MEM_POOL_INCLUDE
            #undef LV_MEM_POOL_ALLOC
        #endif
    #endif
#else
    #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
    #define LV_MEM_CUSTOM_ALLOC   malloc
    #define LV_MEM_CUSTOM_FREE    free
    #define LV_MEM_CUSTOM_REALLOC realloc
#endif

#define LV_MEM_BUF_MAX_NUM 16
#define LV_MEMCPY_MEMSET_STD 0

/*====================
   HAL SETTINGS
 *====================*/

#define LV_DISP_DEF_REFR_PERIOD 20
#define LV_INDEV_DEF_READ_PERIOD 20

// Stage 2: use explicit esp_timer + lv_tick_inc() instead of millis() expression.
// Stage 2: используем явный esp_timer + lv_tick_inc(), а не выражение на основе millis()
#define LV_TICK_CUSTOM 0
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#define LV_DPI_DEF 130

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/

/*--- Drawing ---*/
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 4
#define LV_LAYER_SIMPLE_BUF_SIZE (24 * 1024)
#define LV_IMG_CACHE_DEF_SIZE 0
/* 4 stops: rim glow HOR mat→peak→mat + mat plateau to pixel edge (no grey tail). / 4 стопа — длинный «чистый» мат справа */
#define LV_GRADIENT_MAX_STOPS 4
#define LV_GRAD_CACHE_DEF_SIZE 0
/* Dither HOR/VERT gradients — less banding on RGB565 panels (volume bar, glows). / Меньше «лесенки» на 16-bit */
#define LV_DITHER_GRADIENT 1
#define LV_DISP_ROT_MAX_BUF (10 * 1024)

/*--- GPU ---*/
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_NXP_PXP 0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL 0

/*--- Logging ---*/
// LV_LOG_PRINTF: use stdio printf → on ESP32 Arduino this typically matches USB/UART Serial monitor.
// Без LV_LOG_PRINTF=1 и без lv_log_register_print_cb() LVGL не печатает логи вообще.
// Runtime default: WARN to reduce ISR/UART pressure and DspTask stack load.
// Рабочий уровень: WARN (в отличие от INFO, меньше нагрузки на UART/ISR и стек DspTask).
// Keep all LV_LOG_TRACE_* = 0 (TRACE can still WDT / flood UART).
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 1
    #define LV_LOG_TRACE_MEM 0
    #define LV_LOG_TRACE_TIMER 0
    #define LV_LOG_TRACE_INDEV 0
    #define LV_LOG_TRACE_DISP_REFR 0
    #define LV_LOG_TRACE_EVENT 0
    #define LV_LOG_TRACE_OBJ_CREATE 0
    #define LV_LOG_TRACE_LAYOUT 0
    #define LV_LOG_TRACE_ANIM 0
#endif

/*--- Asserts ---*/
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0
#define LV_ASSERT_HANDLER_INCLUDE <stdint.h>
#define LV_ASSERT_HANDLER while(1);

/*--- Others ---*/
/* Temporary diagnostic: LVGL built-in FPS + CPU overlay — disable after testing / Временная диагностика: встроенный overlay FPS+CPU — выключить после тестов. */
#define LV_USE_PERF_MONITOR 1
#if LV_USE_PERF_MONITOR
    /* LVGL aligns with (0,0) ofs only — YoRadio shifts the label in lvgl_ui::taskHandler() next to Wi‑Fi. / LVGL только (0,0) — сдвиг в lvgl_ui::taskHandler() у Wi‑Fi. */
    #define LV_USE_PERF_MONITOR_POS LV_ALIGN_TOP_RIGHT
#endif
#define LV_USE_MEM_MONITOR 0
#define LV_USE_REFR_DEBUG 0
#define LV_SPRINTF_CUSTOM 0
#define LV_SPRINTF_USE_FLOAT 0
#define LV_USE_USER_DATA 1
#define LV_ENABLE_GC 0

/*--- Compiler ---*/
#define LV_BIG_ENDIAN_SYSTEM 0
#define LV_ATTRIBUTE_TICK_INC
#define LV_ATTRIBUTE_TIMER_HANDLER
#define LV_ATTRIBUTE_FLUSH_READY
#define LV_ATTRIBUTE_MEM_ALIGN_SIZE 1
#define LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY
#define LV_ATTRIBUTE_FAST_MEM
#define LV_ATTRIBUTE_DMA
#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning
#define LV_USE_LARGE_COORD 1

/*==================
 *  FONT USAGE
 *==================*/

/* Stage 5.1: generated Cyrillic-capable Montserrat (see fonts/*.c, lv_font_conv).
 * Stage 5.1: сгенерированный Montserrat с кириллицей (fonts/*.c, lv_font_conv). */
#define LV_FONT_YORA_MONTSERRAT_12_CYR 1
#define LV_FONT_YORA_MONTSERRAT_14_CYR 1
#define LV_FONT_YORA_MONTSERRAT_16_CYR 1
#define LV_FONT_YORA_MONTSERRAT_18_CYR 1
#define LV_FONT_YORA_MONTSERRAT_20_CYR 1
#define LV_FONT_YORA_MONTSERRAT_22_CYR 1
#define LV_FONT_YORA_MONTSERRAT_28_CYR 1
#define LV_FONT_YORA_MONTSERRAT_32_CYR 1
#define LV_FONT_YORA_MONTSERRAT_40_CYR 1
#define LV_FONT_YORA_MONTSERRAT_48_CYR 1
/* Tabler subset: Main status Wi-Fi glyphs (fonts/lv_font_yora_status_icons_18|20|22.c). */
#define LV_FONT_YORA_STATUS_ICONS_18 1
#define LV_FONT_YORA_STATUS_ICONS_20 1
#define LV_FONT_YORA_STATUS_ICONS_22 1
/* Tabler subset: Wi-Fi Flow header icon only — 24 px (≤320) / 36 px (480+); see lv_font_yora_wifi_flow_icons.md. */
#define LV_FONT_YORA_WIFI_FLOW_ICONS_24 1
#define LV_FONT_YORA_WIFI_FLOW_ICONS_36 1
/* Tabler subset: OWM weather glyphs 18–28 px (Main mini) + 36|64 px (Weather Page). A1/A3. */
#define LV_FONT_YORA_WEATHER_ICONS_18 1
#define LV_FONT_YORA_WEATHER_ICONS_20 1
#define LV_FONT_YORA_WEATHER_ICONS_22 1
#define LV_FONT_YORA_WEATHER_ICONS_24 1
#define LV_FONT_YORA_WEATHER_ICONS_28 1
#define LV_FONT_YORA_WEATHER_ICONS_36 1
#define LV_FONT_YORA_WEATHER_ICONS_64 1
/* Tabler subset: Weather Page metric row icons (wind/droplet/gauge/cloud-rain) @ 22|26 px. A1/A3. */
#define LV_FONT_YORA_WEATHER_METRIC_ICONS_22 1
#define LV_FONT_YORA_WEATHER_METRIC_ICONS_26 1
/* Tabler subset: Main control band icons (fonts/lv_font_yora_control_icons_18|20|22|24|26|28.c). Stage 6.1E-b. */
#define LV_FONT_YORA_CONTROL_ICONS_18 1
#define LV_FONT_YORA_CONTROL_ICONS_20 1
#define LV_FONT_YORA_CONTROL_ICONS_22 1
#define LV_FONT_YORA_CONTROL_ICONS_24 1
#define LV_FONT_YORA_CONTROL_ICONS_26 1
#define LV_FONT_YORA_CONTROL_ICONS_28 1
/* Tabler subset: Station Page current marker (volume-2) @ 14–24 px (profile pick later). Stage 6.3D-b+ */
#define LV_FONT_YORA_STATION_ICONS_14 1
#define LV_FONT_YORA_STATION_ICONS_15 1
#define LV_FONT_YORA_STATION_ICONS_16 1
#define LV_FONT_YORA_STATION_ICONS_17 1
#define LV_FONT_YORA_STATION_ICONS_18 1
#define LV_FONT_YORA_STATION_ICONS_19 1
#define LV_FONT_YORA_STATION_ICONS_20 1
#define LV_FONT_YORA_STATION_ICONS_21 1
#define LV_FONT_YORA_STATION_ICONS_22 1
#define LV_FONT_YORA_STATION_ICONS_23 1
#define LV_FONT_YORA_STATION_ICONS_24 1
/* Tabler subset: Info Page section rail (database, device-desktop, router, cpu) @ 24|28|32|36|40|44. Stage 6.2 Patch B. */
#define LV_FONT_YORA_INFO_SECTION_ICONS_24 1
#define LV_FONT_YORA_INFO_SECTION_ICONS_28 1
#define LV_FONT_YORA_INFO_SECTION_ICONS_32 1
#define LV_FONT_YORA_INFO_SECTION_ICONS_36 1
#define LV_FONT_YORA_INFO_SECTION_ICONS_40 1
#define LV_FONT_YORA_INFO_SECTION_ICONS_44 1
#define LV_FONT_YORA_SETTINGS_ICONS_28 1

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_MONTSERRAT_12_SUBPX 0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK 0
#define LV_FONT_UNSCII_8 0
#define LV_FONT_UNSCII_16 0
#define LV_FONT_CUSTOM_DECLARE                                                                 \
    extern const lv_font_t lv_font_yora_montserrat_12_cyr;                                      \
    extern const lv_font_t lv_font_yora_montserrat_14_cyr;                                      \
    extern const lv_font_t lv_font_yora_montserrat_16_cyr;                                      \
    extern const lv_font_t lv_font_yora_montserrat_18_cyr;                                      \
    extern const lv_font_t lv_font_yora_montserrat_20_cyr;                                      \
    extern const lv_font_t lv_font_yora_montserrat_22_cyr;                                      \
    extern const lv_font_t lv_font_yora_montserrat_28_cyr;                                      \
    extern const lv_font_t lv_font_yora_montserrat_32_cyr;                                      \
    extern const lv_font_t lv_font_yora_montserrat_40_cyr;                                      \
    extern const lv_font_t lv_font_yora_montserrat_48_cyr;                                      \
    extern const lv_font_t lv_font_yora_status_icons_18;                                      \
    extern const lv_font_t lv_font_yora_status_icons_20;                                      \
    extern const lv_font_t lv_font_yora_status_icons_22;                                      \
    extern const lv_font_t lv_font_yora_wifi_flow_icons_24;                                      \
    extern const lv_font_t lv_font_yora_wifi_flow_icons_36;                                      \
    extern const lv_font_t lv_font_yora_weather_icons_18;                                      \
    extern const lv_font_t lv_font_yora_weather_icons_20;                                      \
    extern const lv_font_t lv_font_yora_weather_icons_22;                                      \
    extern const lv_font_t lv_font_yora_weather_icons_24;                                      \
    extern const lv_font_t lv_font_yora_weather_icons_28;                                      \
    extern const lv_font_t lv_font_yora_weather_icons_36;                                      \
    extern const lv_font_t lv_font_yora_weather_icons_64;                                      \
    extern const lv_font_t lv_font_yora_weather_metric_icons_22;                               \
    extern const lv_font_t lv_font_yora_weather_metric_icons_26;                               \
    extern const lv_font_t lv_font_yora_control_icons_18;                                      \
    extern const lv_font_t lv_font_yora_control_icons_20;                                      \
    extern const lv_font_t lv_font_yora_control_icons_22;                                      \
    extern const lv_font_t lv_font_yora_control_icons_24;                                      \
    extern const lv_font_t lv_font_yora_control_icons_26;                                      \
    extern const lv_font_t lv_font_yora_control_icons_28;                                      \
    extern const lv_font_t lv_font_yora_station_icons_14;                                      \
    extern const lv_font_t lv_font_yora_station_icons_15;                                      \
    extern const lv_font_t lv_font_yora_station_icons_16;                                      \
    extern const lv_font_t lv_font_yora_station_icons_17;                                      \
    extern const lv_font_t lv_font_yora_station_icons_18;                                      \
    extern const lv_font_t lv_font_yora_station_icons_19;                                      \
    extern const lv_font_t lv_font_yora_station_icons_20;                                      \
    extern const lv_font_t lv_font_yora_station_icons_21;                                      \
    extern const lv_font_t lv_font_yora_station_icons_22;                                      \
    extern const lv_font_t lv_font_yora_station_icons_23;                                      \
    extern const lv_font_t lv_font_yora_station_icons_24;                                      \
    extern const lv_font_t lv_font_yora_info_section_icons_24;                                      \
    extern const lv_font_t lv_font_yora_info_section_icons_28;                                      \
    extern const lv_font_t lv_font_yora_info_section_icons_32;                                      \
    extern const lv_font_t lv_font_yora_info_section_icons_36;                                      \
    extern const lv_font_t lv_font_yora_info_section_icons_40;                                      \
    extern const lv_font_t lv_font_yora_info_section_icons_44;                                      \
    extern const lv_font_t lv_font_yora_settings_icons_28;

#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_FONT_FMT_TXT_LARGE 0
/* Stage 5.1: lv_font_conv defaults to RLE-compressed bitmaps; must match generated fonts.
 * Stage 5.1: lv_font_conv по умолчанию даёт RLE — без этого LVGL предупреждает и ломает отрисовку. */
#define LV_USE_FONT_COMPRESSED 1
#define LV_USE_FONT_SUBPX 0
#if LV_USE_FONT_SUBPX
    #define LV_FONT_SUBPX_BGR 0
#endif

/*==================
 *  TEXT SETTINGS
 *==================*/

#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3
#define LV_TXT_COLOR_CMD "#"
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*==================
 *  WIDGET USAGE
 *==================*/

#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1 /* Wi-Fi 4A: password entry / ввод пароля Wi‑Fi */
#define LV_USE_TABLE      1

/*--- Extra widgets ---*/
#define LV_USE_ANIMIMG    0
#define LV_USE_CALENDAR   0
#define LV_USE_CHART      0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0
#define LV_USE_KEYBOARD   1 /* Wi-Fi 4A: on-screen keyboard / экранная клавиатура */
#define LV_USE_LED        0
#define LV_USE_LIST       1
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_SPAN       0

/*--- Themes ---*/
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 1
    #define LV_THEME_DEFAULT_GROW 0
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif
#define LV_USE_THEME_BASIC 1
#define LV_USE_THEME_MONO 0

/*--- Layouts ---*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/*--- Demos ---*/
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0

#endif /* LV_CONF_H */
#endif /* Content enable */
