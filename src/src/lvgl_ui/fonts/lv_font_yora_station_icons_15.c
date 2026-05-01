/*******************************************************************************
 * Size: 15 px
 * Bpp: 4
 * Opts: --font G:\Github\Yoradio_RGB_Panel-1\.fontwork\tabler-stripped.ttf --size 15 --bpp 4 --format lvgl --no-compress -o src\src\lvgl_ui\fonts\lv_font_yora_station_icons_15.c -r 60239,61263 --lv-include lvgl.h
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef LV_FONT_YORA_STATION_ICONS_15
#define LV_FONT_YORA_STATION_ICONS_15 1
#endif

#if LV_FONT_YORA_STATION_ICONS_15

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+EB4F "" */
    0x0, 0x0, 0x2, 0x0, 0x0, 0x0, 0x0, 0x5,
    0xfc, 0x0, 0x0, 0x0, 0x2, 0xfc, 0xe0, 0x0,
    0x0, 0x13, 0xda, 0x5e, 0x8, 0x50, 0x3f, 0xfd,
    0x4, 0xe0, 0x6f, 0x26, 0xd0, 0x0, 0x4e, 0x0,
    0xc7, 0x6d, 0x0, 0x4, 0xe0, 0xa, 0x95, 0xe7,
    0x40, 0x4f, 0x0, 0xe6, 0x1a, 0xcf, 0x34, 0xf0,
    0xad, 0x0, 0x0, 0x9e, 0x6e, 0x3, 0x10, 0x0,
    0x0, 0xcf, 0xe0, 0x0, 0x0, 0x0, 0x1, 0xb7,
    0x0, 0x0,

    /* U+EF4F "" */
    0x7, 0x50, 0x0, 0x2, 0x90, 0x0, 0x0, 0x6f,
    0x1d, 0xf3, 0xba, 0x0, 0x0, 0x0, 0x4, 0xf9,
    0x90, 0x31, 0x0, 0x4, 0xa3, 0x4f, 0x79, 0x1f,
    0x80, 0x0, 0x49, 0x24, 0xf7, 0xfd, 0x62, 0x0,
    0x0, 0x2, 0x5e, 0x7b, 0xdf, 0xfb, 0x60, 0xd,
    0xff, 0xe7, 0xac, 0x7f, 0xbf, 0x13, 0xf3, 0xce,
    0x45, 0x73, 0x83, 0xf2, 0xd, 0x80, 0x20, 0x0,
    0x0, 0x1f, 0x20, 0x5f, 0x10, 0x0, 0x0, 0x1,
    0xf2, 0x0, 0xca, 0x0, 0x0, 0x0, 0x4f, 0x0,
    0x2, 0xf5, 0x0, 0x0, 0x1d, 0xa0, 0x0, 0x6,
    0xfa, 0x65, 0x7d, 0xd1, 0x0, 0x0, 0x3, 0xad,
    0xdc, 0x70, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 240, .box_w = 11, .box_h = 12, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 66, .adv_w = 240, .box_w = 13, .box_h = 14, .ofs_x = 1, .ofs_y = -1}
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

#if LV_VERSION_CHECK(8, 0, 0)
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
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
#if LV_VERSION_CHECK(8, 0, 0)
    .cache = &cache
#endif
};


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LV_VERSION_CHECK(8, 0, 0)
const lv_font_t lv_font_yora_station_icons_15 = {
#else
lv_font_t lv_font_yora_station_icons_15 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 14,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc           /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
};



#endif /*#if LV_FONT_YORA_STATION_ICONS_15*/

