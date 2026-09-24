#include "pages.h"

#include <stdint.h>

#include "acquisition_service.h"
#include "data_model.h"
#include "kdl_channels.h"
#include "kdl_theme.h"
#include "kdl_widgets.h"

/*
 * Main page geometry, derived from the content box the page manager hands
 * over (394 x 294) rather than from percentages, so every card lands on a
 * known pixel and a value change can never reflow the grid.
 *
 *   inner width  = 394 - 2 * PAD = 382
 *   inner height = 294 - 2 * PAD = 282 = TC grid (221) + GAP (5) + analog (56)
 *   TC grid      = 2 rows of 108 with one 5 px row gap
 *   TC columns   = 4 cards of 92 with three 4 px gaps = 380
 *   analog row   = 5 cells of 73 with four 4 px gaps  = 381
 *
 * At 73 px the analog cell has 63 inner pixels: the widest header,
 * "IN3" + "P IC out" in Montserrat 10, measures ~60 px, and "3.30" in
 * Montserrat 20 plus the "V" suffix ~51 px.
 */
#define PAGE_MAIN_PAD            6
#define PAGE_MAIN_COL_GAP        4
#define PAGE_MAIN_ROW_GAP        5
#define PAGE_MAIN_INNER_W        382
#define PAGE_MAIN_INNER_H        282
#define PAGE_MAIN_CARD_W         92
#define PAGE_MAIN_CARD_H         108
#define PAGE_MAIN_ANALOG_H       56
#define PAGE_MAIN_ANALOG_CELL_W  73
#define PAGE_MAIN_TC_GRID_H      (2 * PAGE_MAIN_CARD_H + PAGE_MAIN_ROW_GAP)

_Static_assert(PAGE_MAIN_TC_GRID_H + PAGE_MAIN_ROW_GAP + PAGE_MAIN_ANALOG_H
                   == PAGE_MAIN_INNER_H,
               "main page bands do not fill the content box exactly");
_Static_assert(4 * PAGE_MAIN_CARD_W + 3 * PAGE_MAIN_COL_GAP <= PAGE_MAIN_INNER_W,
               "four cards per row overflow the content width");
_Static_assert(KDL_THERMOCOUPLE_DISPLAY_COUNT == 8,
               "thermocouple grid is laid out as 4 columns by 2 rows");
_Static_assert(KDL_ANALOG_DISPLAY_COUNT == 5,
               "analog row is laid out as a single row of 5 cells");
_Static_assert(5 * PAGE_MAIN_ANALOG_CELL_W + 4 * PAGE_MAIN_COL_GAP <= PAGE_MAIN_INNER_W,
               "five analog cells overflow the content width");

static kdl_probe_card_t s_cards[KDL_THERMOCOUPLE_DISPLAY_COUNT];
static kdl_analog_cell_t s_cells[KDL_ANALOG_DISPLAY_COUNT];
static uint64_t s_last_sample_uptime_ms = UINT64_MAX;

static lv_obj_t *page_main_band(lv_obj_t *parent, int32_t height)
{
    lv_obj_t *band = lv_obj_create(parent);
    lv_obj_add_style(band, &kdl_style_panel, 0);
    lv_obj_set_size(band, PAGE_MAIN_INNER_W, height);
    lv_obj_clear_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(band, PAGE_MAIN_COL_GAP, 0);
    lv_obj_set_style_pad_row(band, PAGE_MAIN_ROW_GAP, 0);
    return band;
}

static void on_show(lv_obj_t *content)
{
    lv_obj_set_style_pad_all(content, PAGE_MAIN_PAD, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, PAGE_MAIN_ROW_GAP, 0);

    s_last_sample_uptime_ms = UINT64_MAX;

    lv_obj_t *grid = page_main_band(content, PAGE_MAIN_TC_GRID_H);
    for (uint8_t index = 0; index < KDL_THERMOCOUPLE_DISPLAY_COUNT; ++index)
    {
        kdl_probe_card_create(&s_cards[index], grid, kdl_channels_thermocouple(index),
                              PAGE_MAIN_CARD_W, PAGE_MAIN_CARD_H);
    }

    lv_obj_t *analog = page_main_band(content, PAGE_MAIN_ANALOG_H);
    for (uint8_t index = 0; index < KDL_ANALOG_DISPLAY_COUNT; ++index)
    {
        kdl_analog_cell_create(&s_cells[index], analog, kdl_channels_analog(index),
                               PAGE_MAIN_ANALOG_CELL_W, PAGE_MAIN_ANALOG_H);
    }
}

static void on_hide(void)
{
    /* The page manager destroys the content subtree; drop the dangling
     * handles so a late update cannot touch freed objects. */
    for (uint8_t index = 0; index < KDL_THERMOCOUPLE_DISPLAY_COUNT; ++index)
    {
        s_cards[index].root = NULL;
    }
    for (uint8_t index = 0; index < KDL_ANALOG_DISPLAY_COUNT; ++index)
    {
        s_cells[index].root = NULL;
    }
    s_last_sample_uptime_ms = UINT64_MAX;
}

static void on_tick(void)
{
    kdl_sensor_sample_t sample;
    if (acquisition_service_get_latest_sample(&sample) != ESP_OK)
    {
        return;
    }

    /* The GUI ticks faster than the acquisition period, so most ticks carry
     * the sample already on screen. Redrawing it would repaint eight cards
     * for nothing. */
    if (sample.uptime_ms == s_last_sample_uptime_ms)
    {
        return;
    }
    s_last_sample_uptime_ms = sample.uptime_ms;

    kdl_sensor_extremes_t extremes;
    const bool have_extremes = acquisition_service_get_extremes(&extremes) == ESP_OK;

    for (uint8_t index = 0; index < KDL_THERMOCOUPLE_DISPLAY_COUNT; ++index)
    {
        kdl_probe_card_update(&s_cards[index], &sample, have_extremes ? &extremes : NULL, index);
    }
    for (uint8_t index = 0; index < KDL_ANALOG_DISPLAY_COUNT; ++index)
    {
        kdl_analog_cell_update(&s_cells[index], &sample);
    }
}

static void on_button(uint8_t button_index)
{
    switch (button_index)
    {
    case 2:
        page_manager_switch_to(&page_graph);
        break;
    case 3:
        page_manager_switch_to(&page_settings);
        break;
    case 4:
        page_usb_enter();
        break;
    default:
        break;
    }
}

const gui_page_t page_main = {
    .name = "main",
    /* U+2022 BULLET and U+00B0 DEGREE SIGN, both present in the built-in
     * Montserrat faces. The unit lives here because it does not fit on
     * the cards themselves. */
    .title = "LIVE \xE2\x80\xA2 \xC2\xB0" "C",
    .button_labels = {"", "", "Graph", "Settings", "USB"},
    .on_show = on_show,
    .on_hide = on_hide,
    .on_tick = on_tick,
    .on_button = on_button,
};
