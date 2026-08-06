/*******************************************************************************
 * Size: 16 px
 * Bpp: 4
 * Opts: --font G:\Github\Yoradio_RGB_Panel-1\tools\fonts\tabler-stripped.ttf --size 16 --bpp 4 --format lvgl --no-compress -o src\src\lvgl_ui\fonts\lv_font_yora_station_icons_16.c -r 60239,61263 --lv-include lvgl.h
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef LV_FONT_YORA_STATION_ICONS_16
#define LV_FONT_YORA_STATION_ICONS_16 1
#endif

#if LV_FONT_YORA_STATION_ICONS_16

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+EB4F "" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x2e, 0xe1, 0x0, 0x0, 0x0, 0x0, 0xce, 0xf3,
    0x0, 0x0, 0x0, 0x9, 0xe4, 0xf3, 0x26, 0x0,
    0x1c, 0xef, 0x41, 0xf3, 0x4f, 0x60, 0x6f, 0x64,
    0x1, 0xf3, 0x8, 0xe0, 0x6e, 0x0, 0x1, 0xf3,
    0x3, 0xf1, 0x6e, 0x0, 0x1, 0xf3, 0x4, 0xf1,
    0x4f, 0xfc, 0x1, 0xf3, 0xd, 0xb0, 0x3, 0x6e,
    0xa2, 0xf3, 0x5d, 0x10, 0x0, 0x3, 0xf9, 0xf3,
    0x0, 0x0, 0x0, 0x0, 0x6f, 0xf2, 0x0, 0x0,
    0x0, 0x0, 0x6, 0x70, 0x0, 0x0,

    /* U+EF4F "" */
    0x6, 0x30, 0x0, 0x0, 0x44, 0x0, 0x0, 0xa,
    0xe0, 0x9d, 0x72, 0xf7, 0x0, 0x0, 0x0, 0x42,
    0xfa, 0xf1, 0x40, 0x0, 0x0, 0x4, 0x3, 0xf5,
    0xf1, 0x6e, 0x40, 0x0, 0x6f, 0x53, 0xf5, 0xf9,
    0x46, 0x0, 0x0, 0x0, 0x3, 0xf5, 0xfd, 0xfe,
    0x73, 0x0, 0x4, 0x99, 0xf5, 0xf5, 0xfa, 0xff,
    0xb0, 0xf, 0xcf, 0xf4, 0xf5, 0xf5, 0xf6, 0xf0,
    0x1f, 0x55, 0xe0, 0x30, 0x30, 0x34, 0xf0, 0x9,
    0xe0, 0x0, 0x0, 0x0, 0x4, 0xf0, 0x1, 0xf7,
    0x0, 0x0, 0x0, 0x5, 0xf0, 0x0, 0x7f, 0x10,
    0x0, 0x0, 0x9, 0xd0, 0x0, 0xd, 0xb0, 0x0,
    0x0, 0x4f, 0x60, 0x0, 0x2, 0xed, 0x76, 0x69,
    0xfa, 0x0, 0x0, 0x0, 0x19, 0xde, 0xec, 0x60,
    0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 256, .box_w = 12, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 78, .adv_w = 256, .box_w = 14, .box_h = 15, .ofs_x = 1, .ofs_y = -1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x400
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 60239, .range_length = 1025, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 2, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t lv_font_yora_station_icons_16 = {
#else
lv_font_t lv_font_yora_station_icons_16 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 15,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if LV_FONT_YORA_STATION_ICONS_16*/

