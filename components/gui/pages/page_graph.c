#include "pages.h"

#include <stdint.h>

#include "acquisition_service.h"
#include "data_model.h"
#include "gui_actions.h"

#define PAGE_GRAPH_HISTORY_LEN 60
/* lv_chart uses integer coordinates: keep one decimal of temperature by
 * scaling x10 (e.g. 235 == 23.5C). */
#define PAGE_GRAPH_TEMP_SCALE 10
#define PAGE_GRAPH_RANGE_MIN 0
#define PAGE_GRAPH_RANGE_MAX 1000

static lv_obj_t *s_chart;
static lv_chart_series_t *s_series;
/* Sentinel: UINT64_MAX forces the first sample to always be added on page show. */
static uint64_t s_last_uptime_ms = UINT64_MAX;

static void on_show(lv_obj_t *content)
{
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    s_chart = lv_chart_create(content);
    lv_obj_set_size(s_chart, lv_pct(100), lv_pct(100));
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, PAGE_GRAPH_HISTORY_LEN);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, PAGE_GRAPH_RANGE_MIN, PAGE_GRAPH_RANGE_MAX);
    s_series = lv_chart_add_series(s_chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    /* Reset so we always plot the latest available sample immediately. */
    s_last_uptime_ms = UINT64_MAX;
}

static void on_hide(void)
{
    s_chart = NULL;
    s_series = NULL;
}

static void on_tick(void)
{
    kdl_sensor_sample_t sample;
    if (s_chart == NULL || acquisition_service_get_latest_sample(&sample) != ESP_OK) {
        return;
    }

    /* Guard: lv_chart_set_next_value always invalidates the whole chart widget,
     * causing a multi-strip SPI flush (tearing/flicker) on every call.  The GUI
     * tick fires at 500 ms but acquisition produces new samples at 1000 ms, so
     * without this check every second flush is a redundant full chart redraw.
     * Only add a point when the sample timestamp has advanced. */
    if (sample.uptime_ms == s_last_uptime_ms) {
        return;
    }
    s_last_uptime_ms = sample.uptime_ms;

    /* MVP: single fixed series (TC1). Per-channel selection is a follow-up
     * once the page/button layout is finalized.
     * Skip invalid samples: thermocouples_c[0] is garbage when the channel
     * has a fault and would push an out-of-range spike into the chart. */
    if ((sample.thermocouple_valid_mask & (1U << 0)) == 0U) {
        return;
    }
    lv_chart_set_next_value(s_chart, s_series, (int32_t)(sample.thermocouples_c[0] * PAGE_GRAPH_TEMP_SCALE));
}

static void on_button(uint8_t button_index)
{
    switch (button_index) {
    case 0:
        page_manager_switch_to(&page_main);
        break;
    case 4:
        gui_action_toggle_recording();
        break;
    default:
        break;
    }
}

const gui_page_t page_graph = {
    .name = "graph",
    .button_labels = {"Main", "", "", "", "REC"},
    .on_show = on_show,
    .on_hide = on_hide,
    .on_tick = on_tick,
    .on_button = on_button,
};
