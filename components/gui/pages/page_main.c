#include "pages.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "acquisition_service.h"
#include "data_model.h"
#include "gui_actions.h"
#include "logger_service.h"

static lv_obj_t *s_tc_labels[DATA_MODEL_THERMOCOUPLE_COUNT];
static lv_obj_t *s_ai_labels[DATA_MODEL_ANALOG_INPUT_COUNT];
static lv_obj_t *s_rec_status_label;
static uint64_t s_last_sample_uptime_ms = UINT64_MAX;

static void on_show(lv_obj_t *content)
{
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    s_last_sample_uptime_ms = UINT64_MAX;

    lv_obj_t *tc_grid = lv_obj_create(content);
    lv_obj_set_size(tc_grid, lv_pct(100), lv_pct(60));
    lv_obj_set_flex_flow(tc_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(tc_grid, LV_OBJ_FLAG_SCROLLABLE);
    for (size_t index = 0; index < DATA_MODEL_THERMOCOUPLE_COUNT; ++index) {
        s_tc_labels[index] = lv_label_create(tc_grid);
        /* Fixed width so a text length change (e.g. "--.-" -> "23.5" or the
         * "!" invalid-sample prefix) doesn't resize the label and force the
         * ROW_WRAP container to reflow every tick -- that reflow was
         * invalidating a multi-chunk region and showing up as flicker. */
        lv_obj_set_size(s_tc_labels[index], 110, 24);
        lv_label_set_long_mode(s_tc_labels[index], LV_LABEL_LONG_CLIP);
        lv_label_set_text_fmt(s_tc_labels[index], "TC%u: --.-C", (unsigned)(index + 1));
    }

    lv_obj_t *ai_grid = lv_obj_create(content);
    lv_obj_set_size(ai_grid, lv_pct(100), lv_pct(30));
    lv_obj_set_flex_flow(ai_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(ai_grid, LV_OBJ_FLAG_SCROLLABLE);
    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        s_ai_labels[index] = lv_label_create(ai_grid);
        lv_obj_set_size(s_ai_labels[index], 110, 24);
        lv_label_set_long_mode(s_ai_labels[index], LV_LABEL_LONG_CLIP);
        lv_label_set_text_fmt(s_ai_labels[index], "AI%u: -.--V", (unsigned)(index + 1));
    }

    s_rec_status_label = lv_label_create(content);
    lv_obj_set_size(s_rec_status_label, 200, 24);
    lv_label_set_long_mode(s_rec_status_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_rec_status_label, "REC: -- | USB: --");
}

static void on_hide(void)
{
    for (size_t index = 0; index < DATA_MODEL_THERMOCOUPLE_COUNT; ++index) {
        s_tc_labels[index] = NULL;
    }
    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        s_ai_labels[index] = NULL;
    }
    s_rec_status_label = NULL;
    s_last_sample_uptime_ms = UINT64_MAX;
}

static void update_status_label(void)
{
    if (s_rec_status_label != NULL) {
        char status_text[24];
        snprintf(status_text, sizeof(status_text), "REC: %s | USB: %s",
                 logger_service_is_active() ? "ON" : "OFF",
                 gui_action_is_usb_msc_active() ? "ON" : "OFF");
        if (strcmp(lv_label_get_text(s_rec_status_label), status_text) != 0) {
            lv_label_set_text(s_rec_status_label, status_text);
        }
    }
}

static void on_tick(void)
{
    kdl_sensor_sample_t sample;
    if (acquisition_service_get_latest_sample(&sample) == ESP_OK) {
        if (sample.uptime_ms == s_last_sample_uptime_ms) {
            update_status_label();
            return;
        }
        s_last_sample_uptime_ms = sample.uptime_ms;

        char buf[24];

        for (size_t index = 0; index < DATA_MODEL_THERMOCOUPLE_COUNT; ++index) {
            if (s_tc_labels[index] == NULL) {
                continue;
            }
            uint16_t bit = (uint16_t)(1U << index);
            if (sample.thermocouple_valid_mask & bit) {
                snprintf(buf, sizeof(buf), "TC%u: %.1fC", (unsigned)(index + 1),
                         (double)sample.thermocouples_c[index]);
            } else if (sample.thermocouple_oc_mask & bit) {
                snprintf(buf, sizeof(buf), "TC%u: OC", (unsigned)(index + 1));
            } else if (sample.thermocouple_scg_mask & bit) {
                snprintf(buf, sizeof(buf), "TC%u: SCG", (unsigned)(index + 1));
            } else if (sample.thermocouple_scv_mask & bit) {
                snprintf(buf, sizeof(buf), "TC%u: SCV", (unsigned)(index + 1));
            } else {
                snprintf(buf, sizeof(buf), "TC%u: ERR", (unsigned)(index + 1));
            }
            /* Guard: lv_label_set_text always calls lv_obj_invalidate even when
             * the text is unchanged, causing a repaint every tick and visible
             * flicker with the 10-strip partial buffer. Only write on change. */
            if (strcmp(lv_label_get_text(s_tc_labels[index]), buf) != 0) {
                lv_label_set_text(s_tc_labels[index], buf);
            }
        }

        for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
            if (s_ai_labels[index] == NULL) {
                continue;
            }
            bool valid = (sample.analog_valid_mask & (1U << index)) != 0U;
            snprintf(buf, sizeof(buf), "AI%u: %s%.2fV", (unsigned)(index + 1),
                     valid ? "" : "!", (double)sample.analog_inputs[index]);
            if (strcmp(lv_label_get_text(s_ai_labels[index]), buf) != 0) {
                lv_label_set_text(s_ai_labels[index], buf);
            }
        }
    }

    update_status_label();
}

static void on_button(uint8_t button_index)
{
    switch (button_index) {
    case 0:
        page_manager_switch_to(&page_graph);
        break;
    case 1:
        page_manager_switch_to(&page_settings);
        break;
    case 3:
        gui_action_toggle_usb_msc();
        break;
    case 4:
        gui_action_toggle_recording();
        break;
    default:
        break;
    }
}

const gui_page_t page_main = {
    .name = "main",
    .button_labels = {"Graph", "Settings", "", "USB", "REC"},
    .on_show = on_show,
    .on_hide = on_hide,
    .on_tick = on_tick,
    .on_button = on_button,
};
