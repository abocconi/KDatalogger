#pragma once

#include "lvgl.h"

/**
 * @file kdl_theme.h
 * @brief Design tokens (palette, fonts, shared styles) for the KDatalogger UI.
 *
 * The UI is a flat, high-contrast instrument panel: no rounded corners, no
 * shadows, no gradients. Every surface is a solid fill delimited by 1 px
 * strokes, which keeps the invalidated areas small and the SPI flush cheap.
 * Do not introduce radius/shadow styles here without re-checking the refresh
 * budget in display_driver.c.
 */

/* -- Palette ---------------------------------------------------------------
 * Hex values are the single source of truth for the whole GUI; pages and
 * widgets must reference these macros, never a literal lv_color_hex().      */

#define KDL_COLOR_INK           lv_color_hex(0x14161A)  /**< Primary text / strokes  */
#define KDL_COLOR_INK_DIM       lv_color_hex(0x4A4E55)  /**< Secondary values        */
#define KDL_COLOR_INK_MUTED     lv_color_hex(0x6B6F76)  /**< Units, ids, captions    */
#define KDL_COLOR_INK_FAINT     lv_color_hex(0x8D9198)  /**< Micro labels            */

#define KDL_COLOR_PAPER         lv_color_hex(0xF4F3EF)  /**< Screen background       */
#define KDL_COLOR_CARD          lv_color_hex(0xFFFFFF)  /**< Probe card fill         */
#define KDL_COLOR_SURFACE       lv_color_hex(0xECEAE3)  /**< Analog cells, panels    */
#define KDL_COLOR_RAIL          lv_color_hex(0xE3E1DA)  /**< Key label rail          */

#define KDL_COLOR_BORDER        lv_color_hex(0xD5D3CC)  /**< Idle row border         */
#define KDL_COLOR_BORDER_SOFT   lv_color_hex(0xC9C7C0)  /**< Legend cell border      */
#define KDL_COLOR_HAIRLINE      lv_color_hex(0xECEAE3)  /**< In-card separator       */
#define KDL_COLOR_TRACK         lv_color_hex(0xE3E1DA)  /**< Fill-bar track          */

#define KDL_COLOR_HOT           lv_color_hex(0xC8481F)  /**< Over high threshold     */
#define KDL_COLOR_WARM          lv_color_hex(0xB57614)  /**< Approaching threshold   */

#define KDL_COLOR_REC_ON        lv_color_hex(0xFF4B2B)  /**< REC dot, lit phase      */
#define KDL_COLOR_REC_OFF       lv_color_hex(0x7A1D10)  /**< REC dot, dark phase     */
#define KDL_COLOR_REC_STOPPED   lv_color_hex(0x8D9198)  /**< REC dot while stopped   */

#define KDL_COLOR_ALERT_BG      lv_color_hex(0xFDEEE8)
#define KDL_COLOR_ALERT_BORDER  lv_color_hex(0xC8481F)
#define KDL_COLOR_ALERT_INK     lv_color_hex(0x7A2B12)

#define KDL_COLOR_KEY_BG        lv_color_hex(0xE3E1DA)  /**< Labelled key slot       */
#define KDL_COLOR_KEY_BG_LAST   lv_color_hex(0xD9D7D0)  /**< Bottom (USB) key slot   */
#define KDL_COLOR_KEY_BG_EMPTY  lv_color_hex(0xD0CEC7)  /**< Unassigned key slot     */
#define KDL_COLOR_KEY_INK_EMPTY lv_color_hex(0xA8A6A0)
#define KDL_COLOR_KEY_TICK      lv_color_hex(0x14161A)
#define KDL_COLOR_KEY_TICK_OFF  lv_color_hex(0xB6B4AE)

/** @brief Number of distinct chart series colours available. */
#define KDL_SERIES_COLOR_COUNT 6

/* -- Fonts -----------------------------------------------------------------
 * LVGL's built-in Montserrat faces cover ASCII 0x20-0x7E plus U+00B0
 * (degree), U+2022 (bullet) and the LV_SYMBOL_* set, but no accented
 * letters. Each face below is a RAM copy of a built-in one, chained through
 * `fallback` to a generated face holding only the Italian accented vowels
 * (assets/fonts/kdl_font_accents_*.c). The built-in stays the primary, so
 * line height and baseline -- and every layout measured against them -- are
 * unchanged: LVGL draws a fallback glyph on the primary font's baseline.
 * Valid only after kdl_theme_init().                                        */

extern lv_font_t kdl_font_28;
extern lv_font_t kdl_font_20;
extern lv_font_t kdl_font_14;
extern lv_font_t kdl_font_12;
extern lv_font_t kdl_font_10;

#define KDL_FONT_VALUE    (&kdl_font_28) /**< Probe card reading        */
#define KDL_FONT_VALUE_SM (&kdl_font_20) /**< Analog reading, clock     */
#define KDL_FONT_KEY      (&kdl_font_14) /**< Key rail labels           */
#define KDL_FONT_BODY     (&kdl_font_12) /**< Titles, status bar, lists */
#define KDL_FONT_MICRO    (&kdl_font_10) /**< Ids, min/max, date        */

/* -- Shared styles ---------------------------------------------------------
 * Owned by kdl_theme.c and valid for the lifetime of the program. Pages
 * attach them with lv_obj_add_style(); they must never be modified in place
 * by a page, since every widget using them would change at once.            */

extern lv_style_t kdl_style_screen;      /**< Root screen fill                  */
extern lv_style_t kdl_style_statusbar;   /**< Inverted top bar                  */
extern lv_style_t kdl_style_panel;       /**< Bare container: no pad/border/bg  */
extern lv_style_t kdl_style_card;        /**< White probe card, 1 px ink border */
extern lv_style_t kdl_style_cell;        /**< Analog cell on surface fill       */
extern lv_style_t kdl_style_rail;        /**< Key label rail                    */
extern lv_style_t kdl_style_row;         /**< Settings row, idle                */
extern lv_style_t kdl_style_row_sel;     /**< Settings row, selected (inverted) */
extern lv_style_t kdl_style_alert;       /**< Warning banner                    */

/**
 * @brief Build the shared styles. Call once, before any page is created,
 *        with the LVGL context locked.
 */
void kdl_theme_init(void);

/**
 * @brief Apply the screen-level background and clear the default padding.
 *
 * @param screen Active screen object.
 */
void kdl_theme_apply_screen(lv_obj_t *screen);

/**
 * @brief Colour for chart series @p index, wrapping at KDL_SERIES_COLOR_COUNT.
 */
lv_color_t kdl_theme_series_color(uint8_t index);
