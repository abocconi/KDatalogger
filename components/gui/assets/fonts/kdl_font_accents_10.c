/*******************************************************************************
 * Size: 10 px
 * Bpp: 4
 * Opts: --no-compress --no-prefilter --bpp 4 --size 10 --font managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf -r 0xC0,0xC8,0xC9,0xCC,0xD2,0xD9,0xE0,0xE8,0xE9,0xEC,0xF2,0xF9 --format lvgl --lv-include lvgl.h -o components/gui/assets/fonts/kdl_font_accents_10.c --lv-font-name kdl_font_accents_10
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef KDL_FONT_ACCENTS_10
#define KDL_FONT_ACCENTS_10 1
#endif

#if KDL_FONT_ACCENTS_10

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+00C0 "À" */
    0x0, 0x6, 0x20, 0x0, 0x0, 0x0, 0x2b, 0x10,
    0x0, 0x0, 0x1, 0xf6, 0x0, 0x0, 0x0, 0x88,
    0xc0, 0x0, 0x0, 0xd, 0x9, 0x40, 0x0, 0x6,
    0x70, 0x2b, 0x0, 0x0, 0xdc, 0xcc, 0xe3, 0x0,
    0x59, 0x0, 0x4, 0xa0, 0xc, 0x30, 0x0, 0xd,
    0x10,

    /* U+00C8 "È" */
    0x7, 0x10, 0x0, 0x3, 0xb0, 0x0, 0xfd, 0xdd,
    0xc0, 0xf0, 0x0, 0x0, 0xf0, 0x0, 0x0, 0xfc,
    0xcc, 0x70, 0xf0, 0x0, 0x0, 0xf0, 0x0, 0x0,
    0xfd, 0xdd, 0xd1,

    /* U+00C9 "É" */
    0x0, 0x7, 0x0, 0x0, 0xb4, 0x0, 0xfd, 0xdd,
    0xc0, 0xf0, 0x0, 0x0, 0xf0, 0x0, 0x0, 0xfc,
    0xcc, 0x70, 0xf0, 0x0, 0x0, 0xf0, 0x0, 0x0,
    0xfd, 0xdd, 0xd1,

    /* U+00CC "Ì" */
    0x7, 0x10, 0x3, 0xb0, 0x0, 0xf0, 0x0, 0xf0,
    0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0,
    0x0, 0xf0,

    /* U+00D2 "Ò" */
    0x0, 0x26, 0x0, 0x0, 0x0, 0x8, 0x70, 0x0,
    0x1, 0x9d, 0xdc, 0x40, 0xd, 0x60, 0x2, 0xd4,
    0x5b, 0x0, 0x0, 0x4b, 0x78, 0x0, 0x0, 0x1e,
    0x5b, 0x0, 0x0, 0x4b, 0xd, 0x60, 0x2, 0xd4,
    0x1, 0x9d, 0xdc, 0x40,

    /* U+00D9 "Ù" */
    0x0, 0x44, 0x0, 0x0, 0x0, 0xa4, 0x0, 0xf,
    0x0, 0x1, 0xe0, 0xf0, 0x0, 0x1e, 0xf, 0x0,
    0x1, 0xe0, 0xf0, 0x0, 0x1e, 0xe, 0x0, 0x2,
    0xd0, 0xa7, 0x0, 0x98, 0x1, 0xad, 0xd9, 0x0,

    /* U+00E0 "à" */
    0x5, 0xb0, 0x0, 0x0, 0x22, 0x0, 0x1b, 0xcd,
    0x60, 0x1, 0x0, 0xe0, 0x1a, 0xaa, 0xf1, 0x78,
    0x0, 0xe1, 0x2c, 0xaa, 0xe1,

    /* U+00E8 "è" */
    0x3, 0xb1, 0x0, 0x0, 0x13, 0x0, 0x8, 0xcc,
    0x90, 0x5a, 0x0, 0x87, 0x8c, 0xaa, 0xa8, 0x5b,
    0x0, 0x20, 0x7, 0xdc, 0xb1,

    /* U+00E9 "é" */
    0x0, 0x1b, 0x50, 0x0, 0x21, 0x0, 0x8, 0xcc,
    0x90, 0x5a, 0x0, 0x87, 0x8c, 0xaa, 0xa8, 0x5b,
    0x0, 0x20, 0x7, 0xdc, 0xb1,

    /* U+00EC "ì" */
    0x1a, 0x50, 0x0, 0x30, 0x1, 0xe0, 0x1, 0xe0,
    0x1, 0xe0, 0x1, 0xe0, 0x1, 0xe0,

    /* U+00F2 "ò" */
    0x2, 0xc2, 0x0, 0x0, 0x3, 0x0, 0x7, 0xdd,
    0xb1, 0x5c, 0x0, 0x7b, 0x87, 0x0, 0x1e, 0x5c,
    0x0, 0x7b, 0x7, 0xdd, 0xb1,

    /* U+00F9 "ù" */
    0x1, 0xb5, 0x0, 0x0, 0x3, 0x0, 0x2d, 0x0,
    0x1d, 0x2d, 0x0, 0x1d, 0x2d, 0x0, 0x1d, 0xe,
    0x10, 0x6d, 0x6, 0xdb, 0x9d
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 117, .box_w = 9, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 41, .adv_w = 107, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 68, .adv_w = 107, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 95, .adv_w = 50, .box_w = 4, .box_h = 9, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 113, .adv_w = 134, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 149, .adv_w = 127, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 181, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 202, .adv_w = 98, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 223, .adv_w = 98, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 244, .adv_w = 45, .box_w = 4, .box_h = 7, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 258, .adv_w = 102, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 279, .adv_w = 108, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x8, 0x9, 0xc, 0x12, 0x19, 0x20, 0x28,
    0x29, 0x2c, 0x32, 0x39
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 192, .range_length = 58, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 12, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Map glyph_ids to kern left classes*/
static const uint8_t kern_left_class_mapping[] =
{
    0, 1, 2, 2, 0, 3, 4, 5,
    6, 6, 7, 8, 7
};

/*Map glyph_ids to kern right classes*/
static const uint8_t kern_right_class_mapping[] =
{
    0, 1, 0, 0, 0, 2, 3, 4,
    5, 5, 0, 5, 6
};

/*Kern values between classes*/
static const int8_t kern_class_values[] =
{
    2, -2, -2, 0, -1, -2, 0, 0,
    0, -1, -1, -1, -2, 0, 0, 0,
    0, 0, -2, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -2, 0, 0, 0, 0, -2, 0,
    0, 0, -2, 0, -3, -1, 0, 0
};


/*Collect the kern class' data in one place*/
static const lv_font_fmt_txt_kern_classes_t kern_classes =
{
    .class_pair_values   = kern_class_values,
    .left_class_mapping  = kern_left_class_mapping,
    .right_class_mapping = kern_right_class_mapping,
    .left_class_cnt      = 8,
    .right_class_cnt     = 6,
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
    .kern_dsc = &kern_classes,
    .kern_scale = 16,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 1,
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
const lv_font_t kdl_font_accents_10 = {
#else
lv_font_t kdl_font_accents_10 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 9,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if KDL_FONT_ACCENTS_10*/

