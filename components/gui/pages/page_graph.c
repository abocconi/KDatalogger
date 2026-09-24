#include "pages.h"

#include <stdio.h>
#include <stdint.h>

#include "acquisition_service.h"
#include "data_model.h"
#include "kdl_channels.h"
#include "kdl_text.h"
#include "kdl_theme.h"
#include "kdl_widgets.h"
#include "settings_service.h"

/*
 * Graph page geometry, same arithmetic as page_main: the content box is
 * 394 x 294 and every band is placed in absolute pixels.
 *
 *   inner        = 382 x 282 = plot (239) + GAP (5) + legend (38)
 *   legend cells = 6 of 60 px with five 4 px gaps = 380
 */
#define PAGE_GRAPH_PAD          6
#define PAGE_GRAPH_GAP          5
#define PAGE_GRAPH_INNER_W      382
#define PAGE_GRAPH_INNER_H      282
#define PAGE_GRAPH_LEGEND_H     38
#define PAGE_GRAPH_PLOT_H       (PAGE_GRAPH_INNER_H - PAGE_GRAPH_GAP - PAGE_GRAPH_LEGEND_H)
#define PAGE_GRAPH_CELL_W       60
#define PAGE_GRAPH_CELL_GAP     4

/** Channels plotted together. The palette holds exactly this many distinct
 *  colours, and the six are the exhaust and turbo probes -- the ones that
 *  share a temperature range and are worth overlaying on one scale. */
#define PAGE_GRAPH_SERIES_COUNT KDL_SERIES_COLOR_COUNT

#define PAGE_GRAPH_HISTORY_LEN  60
#define PAGE_GRAPH_RANGE_MIN    0
#define PAGE_GRAPH_RANGE_MAX    800

/* Opacity of the five channels that are not selected. Low enough that the
 * selected trace reads as the subject, high enough to keep the others
 * followable. */
#define PAGE_GRAPH_BACKGROUND_OPA 107

_Static_assert(PAGE_GRAPH_SERIES_COUNT <= KDL_THERMOCOUPLE_DISPLAY_COUNT,
               "more plotted series than thermocouple channels");
_Static_assert(6 * PAGE_GRAPH_CELL_W + 5 * PAGE_GRAPH_CELL_GAP <= PAGE_GRAPH_INNER_W,
               "legend cells overflow the content width");

static lv_obj_t *s_chart_bg;
static lv_obj_t *s_chart_fg;
static lv_chart_series_t *s_series_bg[PAGE_GRAPH_SERIES_COUNT];
static lv_chart_series_t *s_series_fg;
static lv_obj_t *s_legend_cells[PAGE_GRAPH_SERIES_COUNT];
static lv_obj_t *s_legend_ids[PAGE_GRAPH_SERIES_COUNT];
static lv_obj_t *s_legend_values[PAGE_GRAPH_SERIES_COUNT];
static lv_obj_t *s_window_label;

static uint8_t s_selected;
static bool s_hold;
static uint64_t s_last_uptime_ms = UINT64_MAX;

static void page_graph_apply_title(void)
{
    const kdl_thermocouple_desc_t *desc = kdl_channels_thermocouple(s_selected);
    char title[40];
    snprintf(title, sizeof(title), "%s \xE2\x80\xA2 %s", s_hold ? KDL_TXT_GRAPH_TITLE_HOLD : KDL_TXT_GRAPH_TITLE,
             desc != NULL ? desc->channel->name : "");
    page_manager_set_title(title);
}

static void page_graph_apply_legend_styles(void)
{
    for (uint8_t index = 0; index < PAGE_GRAPH_SERIES_COUNT; ++index)
    {
        const bool selected = (index == s_selected);
        lv_obj_set_style_bg_color(s_legend_cells[index],
                                  selected ? KDL_COLOR_INK : KDL_COLOR_CARD, 0);
        lv_obj_set_style_border_color(s_legend_cells[index],
                                      selected ? KDL_COLOR_INK : KDL_COLOR_BORDER_SOFT, 0);
        const lv_color_t ink = selected ? KDL_COLOR_PAPER : KDL_COLOR_INK;
        lv_obj_set_style_text_color(s_legend_ids[index], ink, 0);
        lv_obj_set_style_text_color(s_legend_values[index], ink, 0);
    }
}

/**
 * Move the selected channel's history into the foreground chart.
 *
 * Both charts hold the same number of points, so the switch is a straight
 * copy of the background series' own array rather than a private history
 * buffer -- lv_chart already stores exactly what is needed.
 */
static void page_graph_adopt_selection(void)
{
    if (s_chart_fg == NULL || s_series_fg == NULL)
    {
        return;
    }

    const int32_t *source = lv_chart_get_y_array(s_chart_bg, s_series_bg[s_selected]);
    int32_t *target = lv_chart_get_y_array(s_chart_fg, s_series_fg);
    if (source != NULL && target != NULL)
    {
        for (uint16_t point = 0; point < PAGE_GRAPH_HISTORY_LEN; ++point)
        {
            target[point] = source[point];
        }
    }

    lv_chart_set_series_color(s_chart_fg, s_series_fg, kdl_theme_series_color(s_selected));
    lv_chart_refresh(s_chart_fg);

    page_graph_apply_legend_styles();
    page_graph_apply_title();
}

static lv_obj_t *page_graph_create_chart(lv_obj_t *parent, int32_t width, int32_t height)
{
    lv_obj_t *chart = lv_chart_create(parent);
    lv_obj_add_style(chart, &kdl_style_panel, 0);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(chart, width, height);
    lv_obj_set_pos(chart, 0, 0);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, PAGE_GRAPH_HISTORY_LEN);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, PAGE_GRAPH_RANGE_MIN, PAGE_GRAPH_RANGE_MAX);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    /* No point markers: at 60 points across 380 px they merge into a band. */
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    return chart;
}

static void page_graph_build_plot(lv_obj_t *content)
{
    lv_obj_t *plot = lv_obj_create(content);
    lv_obj_add_style(plot, &kdl_style_panel, 0);
    lv_obj_set_size(plot, PAGE_GRAPH_INNER_W, PAGE_GRAPH_PLOT_H);
    lv_obj_clear_flag(plot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(plot, KDL_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(plot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(plot, KDL_COLOR_INK, 0);
    lv_obj_set_style_border_width(plot, 1, 0);

    const int32_t plot_w = PAGE_GRAPH_INNER_W - 2;
    const int32_t plot_h = PAGE_GRAPH_PLOT_H - 2;

    /* Two overlaid charts. lv_chart applies the LV_PART_ITEMS line style to
     * every series it owns, so per-series width and opacity are impossible
     * within one widget: the five context traces live in the thin, faded
     * background chart and the selected one in the opaque foreground chart. */
    s_chart_bg = page_graph_create_chart(plot, plot_w, plot_h);
    lv_chart_set_div_line_count(s_chart_bg, 5, 6);
    lv_obj_set_style_line_color(s_chart_bg, KDL_COLOR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart_bg, 1, LV_PART_ITEMS);
    lv_obj_set_style_line_opa(s_chart_bg, PAGE_GRAPH_BACKGROUND_OPA, LV_PART_ITEMS);

    for (uint8_t index = 0; index < PAGE_GRAPH_SERIES_COUNT; ++index)
    {
        s_series_bg[index] = lv_chart_add_series(s_chart_bg, kdl_theme_series_color(index),
                                                 LV_CHART_AXIS_PRIMARY_Y);
    }

    s_chart_fg = page_graph_create_chart(plot, plot_w, plot_h);
    lv_chart_set_div_line_count(s_chart_fg, 0, 0);
    lv_obj_set_style_bg_opa(s_chart_fg, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart_fg, 2, LV_PART_ITEMS);
    s_series_fg = lv_chart_add_series(s_chart_fg, kdl_theme_series_color(0),
                                      LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *y_top = lv_label_create(plot);
    lv_obj_add_flag(y_top, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_font(y_top, KDL_FONT_MICRO, 0);
    lv_obj_set_style_text_color(y_top, KDL_COLOR_INK_MUTED, 0);
    lv_obj_set_pos(y_top, 4, 3);
    lv_label_set_text_fmt(y_top, "%d", PAGE_GRAPH_RANGE_MAX);

    lv_obj_t *y_bottom = lv_label_create(plot);
    lv_obj_add_flag(y_bottom, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_font(y_bottom, KDL_FONT_MICRO, 0);
    lv_obj_set_style_text_color(y_bottom, KDL_COLOR_INK_MUTED, 0);
    lv_obj_align(y_bottom, LV_ALIGN_BOTTOM_LEFT, 4, -3);
    lv_label_set_text_fmt(y_bottom, "%d", PAGE_GRAPH_RANGE_MIN);

    s_window_label = lv_label_create(plot);
    lv_obj_add_flag(s_window_label, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_font(s_window_label, KDL_FONT_MICRO, 0);
    lv_obj_set_style_text_color(s_window_label, KDL_COLOR_INK_MUTED, 0);
    lv_obj_align(s_window_label, LV_ALIGN_BOTTOM_RIGHT, -4, -3);

    /* The window is however long 60 samples take at the configured period, so
     * it moves with the sample rate instead of being a fixed caption. */
    const uint32_t window_s = (PAGE_GRAPH_HISTORY_LEN
                               * settings_service_get_acquisition_period_ms()) / 1000U;
    lv_label_set_text_fmt(s_window_label, "%u s window", (unsigned)window_s);
}

static void page_graph_build_legend(lv_obj_t *content)
{
    lv_obj_t *legend = lv_obj_create(content);
    lv_obj_add_style(legend, &kdl_style_panel, 0);
    lv_obj_set_size(legend, PAGE_GRAPH_INNER_W, PAGE_GRAPH_LEGEND_H);
    lv_obj_clear_flag(legend, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(legend, PAGE_GRAPH_CELL_GAP, 0);

    for (uint8_t index = 0; index < PAGE_GRAPH_SERIES_COUNT; ++index)
    {
        lv_obj_t *cell = lv_obj_create(legend);
        lv_obj_add_style(cell, &kdl_style_panel, 0);
        lv_obj_set_size(cell, PAGE_GRAPH_CELL_W, PAGE_GRAPH_LEGEND_H);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_pad_all(cell, 4, 0);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(cell, 2, 0);
        s_legend_cells[index] = cell;

        lv_obj_t *header = lv_obj_create(cell);
        lv_obj_add_style(header, &kdl_style_panel, 0);
        lv_obj_set_size(header, PAGE_GRAPH_CELL_W - 10, 10);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(header, 3, 0);

        lv_obj_t *swatch = lv_obj_create(header);
        lv_obj_add_style(swatch, &kdl_style_panel, 0);
        lv_obj_set_size(swatch, 9, 3);
        lv_obj_clear_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(swatch, kdl_theme_series_color(index), 0);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);

        /* The legend keys the plot, so it carries the terminal marking; the
         * full channel name would not fit at 60 px and is shown in the status
         * bar for the selected trace instead. */
        const kdl_thermocouple_desc_t *desc = kdl_channels_thermocouple(index);
        s_legend_ids[index] = lv_label_create(header);
        lv_obj_set_style_text_font(s_legend_ids[index], KDL_FONT_MICRO, 0);
        lv_label_set_long_mode(s_legend_ids[index], LV_LABEL_LONG_CLIP);
        lv_label_set_text(s_legend_ids[index], desc != NULL ? desc->channel->id : "");

        s_legend_values[index] = lv_label_create(cell);
        lv_obj_set_size(s_legend_values[index], PAGE_GRAPH_CELL_W - 10, 16);
        lv_obj_set_style_text_font(s_legend_values[index], KDL_FONT_KEY, 0);
        lv_label_set_long_mode(s_legend_values[index], LV_LABEL_LONG_CLIP);
        lv_label_set_text(s_legend_values[index], "---");
    }
}

static void on_show(lv_obj_t *content)
{
    lv_obj_set_style_pad_all(content, PAGE_GRAPH_PAD, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, PAGE_GRAPH_GAP, 0);

    s_last_uptime_ms = UINT64_MAX;

    page_graph_build_plot(content);
    page_graph_build_legend(content);
    page_graph_adopt_selection();
    page_manager_set_button_label(2, s_hold ? KDL_TXT_KEY_RUN : KDL_TXT_KEY_HOLD);
}

static void on_hide(void)
{
    s_chart_bg = NULL;
    s_chart_fg = NULL;
    s_series_fg = NULL;
    s_window_label = NULL;
    for (uint8_t index = 0; index < PAGE_GRAPH_SERIES_COUNT; ++index)
    {
        s_series_bg[index] = NULL;
        s_legend_cells[index] = NULL;
        s_legend_ids[index] = NULL;
        s_legend_values[index] = NULL;
    }
    s_last_uptime_ms = UINT64_MAX;
}

static void on_tick(void)
{
    if (s_chart_bg == NULL || s_hold)
    {
        return;
    }

    kdl_sensor_sample_t sample;
    if (acquisition_service_get_latest_sample(&sample) != ESP_OK)
    {
        return;
    }

    /* Appending a point invalidates the whole chart, and the GUI ticks faster
     * than acquisition produces samples: without this guard every other tick
     * would repaint the plot for data that has not changed. */
    if (sample.uptime_ms == s_last_uptime_ms)
    {
        return;
    }
    s_last_uptime_ms = sample.uptime_ms;

    char text[8];
    for (uint8_t index = 0; index < PAGE_GRAPH_SERIES_COUNT; ++index)
    {
        const bool valid = kdl_channels_is_valid(&sample, index);
        /* A disconnected probe leaves a gap in the trace rather than a line
         * dropping to zero, and reads "---" in the legend. */
        const int32_t point = valid ? (int32_t)sample.thermocouples_c[index]
                                    : LV_CHART_POINT_NONE;

        lv_chart_set_next_value(s_chart_bg, s_series_bg[index], point);
        if (index == s_selected)
        {
            lv_chart_set_next_value(s_chart_fg, s_series_fg, point);
        }

        if (valid)
        {
            snprintf(text, sizeof(text), "%d", (int)sample.thermocouples_c[index]);
        }
        else
        {
            snprintf(text, sizeof(text), "---");
        }
        kdl_widget_set_text(s_legend_values[index], text);
    }
}

/** Step the selection by @p direction, skipping channels with no probe on
 *  them so the keys never land on a trace that is not being drawn. */
static void page_graph_step_selection(int8_t direction)
{
    kdl_sensor_sample_t sample;
    const bool have_sample = acquisition_service_get_latest_sample(&sample) == ESP_OK;

    for (uint8_t step = 1; step <= PAGE_GRAPH_SERIES_COUNT; ++step)
    {
        const uint8_t candidate = (uint8_t)((s_selected + PAGE_GRAPH_SERIES_COUNT
                                             + direction * (int8_t)step)
                                            % PAGE_GRAPH_SERIES_COUNT);
        if (!have_sample || !kdl_channels_is_disconnected(&sample, candidate))
        {
            s_selected = candidate;
            page_graph_adopt_selection();
            return;
        }
    }
}

static void on_button(uint8_t button_index)
{
    switch (button_index)
    {
    case 0:
        page_graph_step_selection(-1);
        break;
    case 1:
        page_graph_step_selection(1);
        break;
    case 2:
        s_hold = !s_hold;
        page_manager_set_button_label(2, s_hold ? KDL_TXT_KEY_RUN : KDL_TXT_KEY_HOLD);
        page_graph_apply_title();
        break;
    case 3:
        page_manager_switch_to(&page_main);
        break;
    case 4:
        page_usb_enter();
        break;
    default:
        break;
    }
}

const gui_page_t page_graph = {
    .name = "graph",
    .title = KDL_TXT_GRAPH_TITLE,
    .button_labels = {KDL_TXT_KEY_PREV_CHANNEL, KDL_TXT_KEY_NEXT_CHANNEL, KDL_TXT_KEY_HOLD,
                      KDL_TXT_KEY_BACK, KDL_TXT_KEY_USB},
    .on_show = on_show,
    .on_hide = on_hide,
    .on_tick = on_tick,
    .on_button = on_button,
};
