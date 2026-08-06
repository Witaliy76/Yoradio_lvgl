/*******************************************************************************
 * Size: 18 px
 * Bpp: 4
 * Opts: --font G:\Github\Yoradio_RGB_Panel-1\tools\fonts\tabler-stripped.ttf --size 18 --bpp 4 --format lvgl --no-compress -o src\src\lvgl_ui\fonts\lv_font_yora_station_icons_18.c -r 60239,61263 --lv-include lvgl.h
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef LV_FONT_YORA_STATION_ICONS_18
#define LV_FONT_YORA_STATION_ICONS_18 1
#endif

#if LV_FONT_YORA_STATION_ICONS_18

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+EB4F "" */
    0x0, 0x0, 0x2, 0xc8, 0x0, 0x0, 0x0, 0x0,
    0x1, 0xef, 0xf1, 0x0, 0x0, 0x0, 0x0, 0xbf,
    0x9f, 0x10, 0x0, 0x0, 0x3, 0x8f, 0x66, 0xf1,
    0x4d, 0x20, 0x1e, 0xff, 0x90, 0x6f, 0x11, 0xed,
    0x5, 0xf6, 0x40, 0x6, 0xf1, 0x4, 0xf5, 0x5f,
    0x20, 0x0, 0x6f, 0x10, 0xf, 0x75, 0xf2, 0x0,
    0x6, 0xf1, 0x1, 0xf7, 0x4f, 0xcb, 0x30, 0x6f,
    0x10, 0x9f, 0x20, 0x8b, 0xee, 0x16, 0xf1, 0x4f,
    0x80, 0x0, 0x2, 0xfb, 0x6f, 0x11, 0x50, 0x0,
    0x0, 0x5, 0xfe, 0xf1, 0x0, 0x0, 0x0, 0x0,
    0x9, 0xfe, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4,
    0x20, 0x0, 0x0,

    /* U+EF4F "" */
    0x0, 0x30, 0x0, 0x0, 0x0, 0x20, 0x0, 0x0,
    0x6f, 0x50, 0x58, 0x20, 0xbf, 0x0, 0x0, 0x0,
    0xa7, 0x5f, 0xfe, 0x1d, 0x50, 0x0, 0x0, 0x0,
    0x8, 0xf5, 0xf1, 0x5, 0x20, 0x0, 0x19, 0x70,
    0x8f, 0x5f, 0x15, 0xfb, 0x0, 0x2, 0xda, 0x8,
    0xf5, 0xfd, 0x73, 0x0, 0x0, 0x0, 0x0, 0x8e,
    0x5f, 0xcf, 0xfd, 0x53, 0x0, 0x18, 0x8b, 0xe5,
    0xf4, 0xfa, 0xff, 0xf9, 0xb, 0xff, 0xfe, 0x5f,
    0x4f, 0x5e, 0xba, 0xe0, 0xea, 0x2d, 0xe1, 0x80,
    0x81, 0x63, 0x8f, 0x8, 0xf3, 0x12, 0x0, 0x0,
    0x0, 0x8, 0xe0, 0xe, 0xb0, 0x0, 0x0, 0x0,
    0x0, 0x8e, 0x0, 0x6f, 0x50, 0x0, 0x0, 0x0,
    0xa, 0xd0, 0x0, 0xcd, 0x0, 0x0, 0x0, 0x0,
    0xea, 0x0, 0x3, 0xfa, 0x0, 0x0, 0x0, 0xbf,
    0x20, 0x0, 0x5, 0xfd, 0x86, 0x68, 0xef, 0x60,
    0x0, 0x0, 0x3, 0xbf, 0xff, 0xfb, 0x30, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 288, .box_w = 13, .box_h = 14, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 91, .adv_w = 288, .box_w = 15, .box_h = 18, .ofs_x = 1, .ofs_y = -2}
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
const lv_font_t lv_font_yora_station_icons_18 = {
#else
lv_font_t lv_font_yora_station_icons_18 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 18,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
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



#endif /*#if LV_FONT_YORA_STATION_ICONS_18*/

