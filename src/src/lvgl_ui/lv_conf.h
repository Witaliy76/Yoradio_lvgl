/**
 * @file lv_conf.h
 * LVGL 9.5.0 compile-time configuration for YoRadio.
 * Конфигурация компиляции LVGL 9.5.0 для YoRadio.
 * BASE-LVGL9-MIGRATION EXEC-01: ported from LVGL 8.3 lv_conf.h against the accepted
 * lv_conf_template.h for v9.5.0. Product widget/feature set preserved; macro names
 * updated to v9 (LV_USE_STDLIB_MALLOC, LV_USE_BUTTON/IMAGE/BUTTONMATRIX, LV_USE_SYSMON, ...).
 * Author: Witaliy76 - https://github.com/Witaliy76
 */

#if 1 /* Set to "1" to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

/* Do NOT unconditionally #include <stdint.h> here (unlike the old LVGL 8.3 config): LVGL 9 also
 * preprocesses this file for .S assembly translation units (__ASSEMBLY__ defined), and raw C
 * `typedef` output from <stdint.h> is not valid assembly. LV_STDINT_INCLUDE below is included by
 * LVGL's own headers only where safe. / Не подключать <stdint.h> напрямую здесь: LVGL 9 также
 * обрабатывает этот файл для .S (__ASSEMBLY__) — сырые typedef ломают ассемблер. */

/*====================
   COLOR SETTINGS
 *====================*/

#define LV_COLOR_DEPTH 16
/* Byte order: no swap. RGB565 (not the *_SWAPPED color format) is selected at the
 * display level in lvgl_ui::initDisplayDriver() via lv_display_set_color_format(). */
#define LV_COLOR_MIX_ROUND_OFS 0

/*=========================
   STDLIB WRAPPER SETTINGS
 *=========================*/

#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

#define LV_STDINT_INCLUDE       <stdint.h>
#define LV_STDDEF_INCLUDE       <stddef.h>
#define LV_STDBOOL_INCLUDE      <stdbool.h>
#define LV_INTTYPES_INCLUDE     <inttypes.h>
#define LV_LIMITS_INCLUDE       <limits.h>
#define LV_STDARG_INCLUDE       <stdarg.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    /* Compile-time logical TLSF pool size in KiB (default 48). Experiment may set 64/128 via build flag.
     * Логический размер TLSF pool в KiB (по умолчанию 48). Эксперимент может задать 64/128 через build flag. */
    #ifndef YORADIO_LVGL_POOL_SIZE_KIB
        #define YORADIO_LVGL_POOL_SIZE_KIB 48U
    #endif
    #define LV_MEM_SIZE (YORADIO_LVGL_POOL_SIZE_KIB * 1024U)   /**< [bytes] */
    #define LV_MEM_POOL_EXPAND_SIZE 0

    #define LV_MEM_ADR 0     /**< 0: unused*/
    /* Compile-time pool placement: 0=static DRAM work_mem_int, 1=PSRAM via project wrapper (allocator A).
     * Размещение pool: 0=static DRAM work_mem_int, 1=PSRAM через project wrapper (allocator A). */
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
#endif  /*LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN*/

/*====================
   HAL SETTINGS
 *====================*/

#define LV_DEF_REFR_PERIOD  20      /**< [ms] display refresh, input device read and animation step period */
#define LV_DPI_DEF 130

/*=================
 * OPERATING SYSTEM
 *=================*/

/* Stage 2 (carried into LVGL9): no RTOS-level LVGL task; DspTask remains sole lv_* owner.
 * Продолжение этапа 2: LVGL без своей RTOS-задачи; DspTask остаётся единственным владельцем lv_*. */
#define LV_USE_OS   LV_OS_NONE

/*========================
 * RENDERING CONFIGURATION
 *========================*/

#define LV_DRAW_BUF_STRIDE_ALIGN                1
#define LV_DRAW_BUF_ALIGN                       4
#define LV_DRAW_TRANSFORM_USE_MATRIX            0

#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE    (24 * 1024)    /**< [bytes]*/
#define LV_DRAW_LAYER_MAX_MEMORY 0  /**< No limit by default [bytes]*/
#define LV_DRAW_THREAD_STACK_SIZE    (8 * 1024)         /**< [bytes]*/
#define LV_DRAW_THREAD_PRIO LV_THREAD_PRIO_HIGH

#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW == 1
    #define LV_DRAW_SW_SUPPORT_RGB565       1
    #define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED       1
    #define LV_DRAW_SW_SUPPORT_RGB565A8     1
    #define LV_DRAW_SW_SUPPORT_RGB888       1
    #define LV_DRAW_SW_SUPPORT_XRGB8888     1
    #define LV_DRAW_SW_SUPPORT_ARGB8888     1
    #define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 1
    #define LV_DRAW_SW_SUPPORT_L8           1
    #define LV_DRAW_SW_SUPPORT_AL88         1
    #define LV_DRAW_SW_SUPPORT_A8           1
    #define LV_DRAW_SW_SUPPORT_I1           1

    #define LV_DRAW_SW_I1_LUM_THRESHOLD 127

    /* LV_USE_OS == LV_OS_NONE: single draw unit only (no parallel draw threads). */
    #define LV_DRAW_SW_DRAW_UNIT_CNT    1

    #define LV_USE_DRAW_ARM2D_SYNC      0
    #define LV_USE_NATIVE_HELIUM_ASM    0

    #define LV_DRAW_SW_COMPLEX          1
    #if LV_DRAW_SW_COMPLEX == 1
        #define LV_DRAW_SW_SHADOW_CACHE_SIZE 0
        #define LV_DRAW_SW_CIRCLE_CACHE_SIZE 4
    #endif

    #define  LV_USE_DRAW_SW_ASM     LV_DRAW_SW_ASM_NONE

    /* Dither HOR/VERT gradients — less banding on RGB565 panels (volume bar, glows) — handled by the
     * software renderer automatically in v9; no separate LV_DITHER_GRADIENT knob (removed vs v8.3). */
    #define LV_USE_DRAW_SW_COMPLEX_GRADIENTS    0
#endif

/* No GPU / vector backends on this product (ESP32-S3 + Arduino_GFX temporary bridge). */
#define LV_USE_NEMA_GFX 0
#define LV_USE_PXP 0
#define LV_USE_G2D 0
#define LV_USE_DRAW_DAVE2D 0
#define LV_USE_DRAW_SDL 0
#define LV_USE_DRAW_VG_LITE 0
#define LV_USE_DRAW_DMA2D 0
#define LV_USE_DRAW_OPENGLES 0
#define LV_USE_PPA  0
#define LV_USE_DRAW_EVE 0
#define LV_USE_DRAW_NANOVG 0

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/

/*--- Logging ---*/
// LV_LOG_PRINTF: use stdio printf → on ESP32 Arduino this typically matches USB/UART Serial monitor.
// Без LV_LOG_PRINTF=1 и без lv_log_register_print_cb() LVGL не печатает логи вообще.
// Runtime default: WARN to reduce ISR/UART pressure and DspTask stack load.
// Рабочий уровень: WARN (в отличие от INFO, меньше нагрузки на UART/ISR и стек DspTask).
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 1
    #define LV_LOG_USE_TIMESTAMP 1
    #define LV_LOG_USE_FILE_LINE 1
    #define LV_LOG_TRACE_MEM        0
    #define LV_LOG_TRACE_TIMER      0
    #define LV_LOG_TRACE_INDEV      0
    #define LV_LOG_TRACE_DISP_REFR  0
    #define LV_LOG_TRACE_EVENT      0
    #define LV_LOG_TRACE_OBJ_CREATE 0
    #define LV_LOG_TRACE_LAYOUT     0
    #define LV_LOG_TRACE_ANIM       0
    #define LV_LOG_TRACE_CACHE      0
#endif

/*--- Asserts ---*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0
#define LV_ASSERT_HANDLER_INCLUDE <stdint.h>
#define LV_ASSERT_HANDLER while(1);

/*--- Debug ---*/
#define LV_USE_REFR_DEBUG 0
#define LV_USE_LAYER_DEBUG 0
#define LV_USE_PARALLEL_DRAW_DEBUG 0

/*--- Others ---*/
#define LV_ENABLE_GLOBAL_CUSTOM 0
#define LV_CACHE_DEF_SIZE       0
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 0
/* 4 stops: rim glow HOR mat→peak→mat + mat plateau to pixel edge (no grey tail). / 4 стопа — длинный «чистый» мат справа */
#define LV_GRADIENT_MAX_STOPS 4
#define LV_OBJ_STYLE_CACHE      0
#define LV_USE_OBJ_ID           0
#define LV_USE_OBJ_NAME         0
#define LV_OBJ_ID_AUTO_ASSIGN   LV_USE_OBJ_ID
#define LV_USE_OBJ_ID_BUILTIN   1
#define LV_USE_OBJ_PROPERTY 0
#define LV_USE_OBJ_PROPERTY_NAME 1
#define LV_USE_GESTURE_RECOGNITION 0

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
#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning
#define LV_ATTRIBUTE_EXTERN_DATA
#define LV_USE_FLOAT            0
#define LV_USE_MATRIX           0
#ifndef LV_USE_PRIVATE_API
    #define LV_USE_PRIVATE_API  0
#endif

/*==================
 *  FONT USAGE
 *==================*/

/* Stage 5.1 / BASE-LVGL9-MIGRATION C4: generated Cyrillic-capable Montserrat, regenerated for v9 font ABI.
 * Stage 5.1 / C4: сгенерированный Montserrat с кириллицей, перегенерирован под ABI шрифтов v9. */
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
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
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
#define LV_USE_FONT_PLACEHOLDER 1

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

#define LV_WIDGETS_HAS_DEFAULT_VALUE  0

#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BUTTON        1
#define LV_USE_BUTTONMATRIX  1
#define LV_USE_CANVAS     1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMAGE      1
#define LV_USE_LABEL      1
#if LV_USE_LABEL
    #define LV_LABEL_TEXT_SELECTION 1
    #define LV_LABEL_LONG_TXT_HINT 1
    #define LV_LABEL_WAIT_CHAR_COUNT 3
#endif
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1 /* Wi-Fi 4A: password entry / ввод пароля Wi‑Fi */
#if LV_USE_TEXTAREA != 0
    #define LV_TEXTAREA_DEF_PWD_SHOW_TIME 1500    /**< [ms] */
#endif
#define LV_USE_TABLE      1

/*--- Extra widgets ---*/
#define LV_USE_ANIMIMG    0
#define LV_USE_ARCLABEL   0
#define LV_USE_CALENDAR   0
#define LV_USE_CHART      0
#define LV_USE_IMAGEBUTTON     0
#define LV_USE_KEYBOARD   1 /* Wi-Fi 4A: on-screen keyboard / экранная клавиатура */
#define LV_USE_LED        0
#define LV_USE_LIST       1
#define LV_USE_LOTTIE     0
#define LV_USE_MENU       0
#define LV_USE_MSGBOX     0
#define LV_USE_SCALE      0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_3DTEXTURE  0

/*--- Themes ---*/
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 1
    #define LV_THEME_DEFAULT_GROW 0
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif
#define LV_USE_THEME_SIMPLE 1
#define LV_USE_THEME_MONO 0

/*--- Layouts ---*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/*====================
 * 3RD PARTY LIBRARIES
 *====================*/

/* Custom LVGL filesystem driver (lv_fs_littlefs.cpp) wraps Arduino LittleFS directly on drive L:.
 * None of LVGL's built-in fs backends are used. / Собственный драйвер ФС — встроенные backends не нужны. */
#define LV_FS_DEFAULT_DRIVER_LETTER '\0'
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0
#define LV_USE_FS_MEMFS 0
#define LV_USE_FS_LITTLEFS 0
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 0
#define LV_USE_FS_ARDUINO_SD 0
#define LV_USE_FS_UEFI 0
#define LV_USE_FS_FROGFS 0

/* No third-party image/vector decoders — product images are static C descriptors + the
 * custom on-disk→lv_image_header_t translation (C5), not runtime PNG/JPG/SVG decode. */
#define LV_USE_LODEPNG 0
#define LV_USE_LIBPNG 0
#define LV_USE_BMP 0
#define LV_USE_TJPGD 0
#define LV_USE_LIBJPEG_TURBO 0
#define LV_USE_LIBWEBP 0
#define LV_USE_GIF 0
#define LV_USE_GSTREAMER 0
#define LV_BIN_DECODER_RAM_LOAD 0
#define LV_USE_RLE 0
#define LV_USE_QRCODE 0
#define LV_USE_BARCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_RLOTTIE 0
#define LV_USE_GLTF  0
#define LV_USE_VECTOR_GRAPHIC  0
#define LV_USE_THORVG_INTERNAL 0
#define LV_USE_THORVG_EXTERNAL 0
#define LV_USE_NANOVG 0
#define LV_USE_LZ4_INTERNAL  0
#define LV_USE_LZ4_EXTERNAL  0
#define LV_USE_SVG 0
#define LV_USE_SVG_ANIMATION 0
#define LV_USE_SVG_DEBUG 0
#define LV_USE_FFMPEG 0

/*==================
 * OTHERS
 *==================*/

#define LV_USE_SNAPSHOT 0

/* BASE-LVGL9-MIGRATION C6 (DIAG-001/DIAG-002): perf monitor disabled for production. Do not enable
 * LV_USE_SYSMON solely for this — the old custom FPS-label scan/reposition path in lvgl_ui.cpp is
 * removed, not reimplemented for v9. / Perf-монитор отключён для production; LV_USE_SYSMON не
 * включаем ради него — старый custom FPS-оверлей удалён, не переписан под v9. */
#define LV_USE_SYSMON   0
#define LV_USE_PROFILER 0
#define LV_USE_MONKEY 0
#define LV_USE_GRIDNAV 0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT 0
#define LV_USE_OBSERVER 1
#define LV_USE_IME_PINYIN 0
#define LV_USE_FILE_EXPLORER 0

/* Demos / examples: not part of the product UI — disabled to keep compiled surface matched to the
 * v8.3 baseline. / Демо и примеры не входят в продукт — отключены, как и раньше на v8.3. */
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_RENDER 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0
#define LV_USE_DEMO_VECTOR_GRAPHIC  0
#define LV_USE_DEMO_GLTF            0
#define LV_USE_DEMO_FLEX_LAYOUT     0
#define LV_USE_DEMO_MULTILANG       0
#define LV_USE_DEMO_EBIKE           0
#define LV_USE_DEMO_HIGH_RES        0
#define LV_USE_DEMO_SMARTWATCH      0

#endif /* LV_CONF_H */
#endif /* Content enable */
