#include "kdl_theme.h"

lv_style_t kdl_style_screen;
lv_style_t kdl_style_statusbar;
lv_style_t kdl_style_panel;
lv_style_t kdl_style_tile;
lv_style_t kdl_style_rail;
lv_style_t kdl_style_keycap;
lv_style_t kdl_style_row;
lv_style_t kdl_style_row_sel;
lv_style_t kdl_style_alert;

static bool s_initialized;

/* The chart palette, in the order series are assigned. Chosen for separation
 * on a white ground at 1-2 px stroke width; do not reorder without checking
 * the graph page legend, which indexes into this table. */
static const uint32_t s_series_colors[KDL_SERIES_COLOR_COUNT] = {
    0xC8481F, 0x14161A, 0x1F6F8B, 0x7A1FA2, 0x2E7D32, 0xB57614,
};

/**
 * Strip everything the default theme adds to a plain lv_obj: padding, gaps,
 * border, radius and scrollbars. Every container in this UI starts from a
 * blank slate and opts back in, because the theme's ~16-20 px pad_all would
 * silently offset the absolute positions the rail and cards depend on.
 */
static void kdl_style_reset_container(lv_style_t *style)
{
    lv_style_set_pad_all(style, 0);
    lv_style_set_pad_row(style, 0);
    lv_style_set_pad_column(style, 0);
    lv_style_set_border_width(style, 0);
    lv_style_set_radius(style, 0);
    lv_style_set_outline_width(style, 0);
    lv_style_set_shadow_width(style, 0);
}

void kdl_theme_init(void)
{
    if (s_initialized)
    {
        return;
    }

    lv_style_init(&kdl_style_screen);
    kdl_style_reset_container(&kdl_style_screen);
    lv_style_set_bg_color(&kdl_style_screen, KDL_COLOR_PAPER);
    lv_style_set_bg_opa(&kdl_style_screen, LV_OPA_COVER);
    lv_style_set_text_color(&kdl_style_screen, KDL_COLOR_INK);
    lv_style_set_text_font(&kdl_style_screen, KDL_FONT_BODY);

    lv_style_init(&kdl_style_panel);
    kdl_style_reset_container(&kdl_style_panel);
    lv_style_set_bg_opa(&kdl_style_panel, LV_OPA_TRANSP);

    lv_style_init(&kdl_style_statusbar);
    kdl_style_reset_container(&kdl_style_statusbar);
    lv_style_set_bg_color(&kdl_style_statusbar, KDL_COLOR_INK);
    lv_style_set_bg_opa(&kdl_style_statusbar, LV_OPA_COVER);
    lv_style_set_text_color(&kdl_style_statusbar, KDL_COLOR_PAPER);
    lv_style_set_pad_left(&kdl_style_statusbar, 8);
    lv_style_set_pad_right(&kdl_style_statusbar, 8);

    lv_style_init(&kdl_style_tile);
    kdl_style_reset_container(&kdl_style_tile);
    lv_style_set_bg_color(&kdl_style_tile, KDL_COLOR_CARD);
    lv_style_set_bg_opa(&kdl_style_tile, LV_OPA_COVER);
    lv_style_set_border_color(&kdl_style_tile, KDL_COLOR_BORDER);
    lv_style_set_border_width(&kdl_style_tile, 1);
    lv_style_set_radius(&kdl_style_tile, 3);

    lv_style_init(&kdl_style_rail);
    kdl_style_reset_container(&kdl_style_rail);
    lv_style_set_bg_color(&kdl_style_rail, KDL_COLOR_RAIL);
    lv_style_set_bg_opa(&kdl_style_rail, LV_OPA_COVER);
    lv_style_set_border_color(&kdl_style_rail, KDL_COLOR_BORDER);
    lv_style_set_border_width(&kdl_style_rail, 1);
    lv_style_set_border_side(&kdl_style_rail, LV_BORDER_SIDE_LEFT);

    lv_style_init(&kdl_style_keycap);
    kdl_style_reset_container(&kdl_style_keycap);
    lv_style_set_bg_color(&kdl_style_keycap, KDL_COLOR_CARD);
    lv_style_set_bg_opa(&kdl_style_keycap, LV_OPA_COVER);
    lv_style_set_border_color(&kdl_style_keycap, KDL_COLOR_KEY_BORDER);
    lv_style_set_border_width(&kdl_style_keycap, 1);
    lv_style_set_radius(&kdl_style_keycap, 4);
    /* Leaves 53 px for the label in the 86 px rail; the widest key label,
     * "Indietro" in 14 px Barlow SemiBold, measures 48.6 px. */
    lv_style_set_pad_right(&kdl_style_keycap, 6);
    lv_style_set_pad_column(&kdl_style_keycap, 5);

    lv_style_init(&kdl_style_row);
    kdl_style_reset_container(&kdl_style_row);
    lv_style_set_bg_color(&kdl_style_row, KDL_COLOR_CARD);
    lv_style_set_bg_opa(&kdl_style_row, LV_OPA_COVER);
    lv_style_set_text_color(&kdl_style_row, KDL_COLOR_INK);
    lv_style_set_border_color(&kdl_style_row, KDL_COLOR_BORDER);
    lv_style_set_border_width(&kdl_style_row, 1);
    lv_style_set_pad_left(&kdl_style_row, 6);
    lv_style_set_pad_right(&kdl_style_row, 6);

    lv_style_init(&kdl_style_row_sel);
    kdl_style_reset_container(&kdl_style_row_sel);
    lv_style_set_bg_color(&kdl_style_row_sel, KDL_COLOR_INK);
    lv_style_set_bg_opa(&kdl_style_row_sel, LV_OPA_COVER);
    lv_style_set_text_color(&kdl_style_row_sel, KDL_COLOR_PAPER);
    lv_style_set_border_color(&kdl_style_row_sel, KDL_COLOR_INK);
    lv_style_set_border_width(&kdl_style_row_sel, 1);
    lv_style_set_pad_left(&kdl_style_row_sel, 6);
    lv_style_set_pad_right(&kdl_style_row_sel, 6);

    lv_style_init(&kdl_style_alert);
    kdl_style_reset_container(&kdl_style_alert);
    lv_style_set_bg_color(&kdl_style_alert, KDL_COLOR_ALERT_BG);
    lv_style_set_bg_opa(&kdl_style_alert, LV_OPA_COVER);
    lv_style_set_text_color(&kdl_style_alert, KDL_COLOR_ALERT_INK);
    lv_style_set_border_color(&kdl_style_alert, KDL_COLOR_ALERT_BORDER);
    lv_style_set_border_width(&kdl_style_alert, 1);
    lv_style_set_pad_left(&kdl_style_alert, 7);
    lv_style_set_pad_right(&kdl_style_alert, 7);
    lv_style_set_pad_top(&kdl_style_alert, 5);
    lv_style_set_pad_bottom(&kdl_style_alert, 5);

    s_initialized = true;
}

void kdl_theme_apply_screen(lv_obj_t *screen)
{
    lv_obj_add_style(screen, &kdl_style_screen, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

lv_color_t kdl_theme_series_color(uint8_t index)
{
    return lv_color_hex(s_series_colors[index % KDL_SERIES_COLOR_COUNT]);
}
