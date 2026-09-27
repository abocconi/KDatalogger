#include "pages.h"

#include <stdint.h>
#include <stdio.h>

#include "display_driver.h"
#include "acquisition_service.h"
#include "esp_app_desc.h"
#include "kdl_theme.h"
#include "kdl_text.h"
#include "kdl_widgets.h"
#include "sensor_config.h"
#include "settings_service.h"
#include "timekeeping.h"

/*
 * Settings list geometry. Rows are a fixed height and stack from the top
 * rather than sharing the content box between them: the list is short today
 * and stretching four rows over 282 px would read as a layout bug.
 */
#define PAGE_SETTINGS_PAD       6
#define PAGE_SETTINGS_INNER_W   382
#define PAGE_SETTINGS_ROW_H     34
#define PAGE_SETTINGS_ROW_GAP   3
#define PAGE_SETTINGS_CARET_W   10
#define PAGE_SETTINGS_VALUE_W   110
#define PAGE_SETTINGS_HINT_W    26
#define PAGE_SETTINGS_FOOTER_LEN 48
#define PAGE_SETTINGS_RESULT_LEN 24

/** Selectable acquisition periods, in ms. A preset list rather than a free
 *  counter: stepping one millisecond at a time through a 100-5000 range with
 *  two keys is unusable, and only a few rates are meaningful given the
 *  MAX31855 conversion time. */
static const uint32_t s_period_presets_ms[] = { 100U, 200U, 250U, 500U, 1000U, 2000U, 5000U };
#define PAGE_SETTINGS_PRESET_COUNT (sizeof(s_period_presets_ms) / sizeof(s_period_presets_ms[0]))

typedef enum {
    PAGE_SETTINGS_ROW_SAMPLE_RATE = 0,
    PAGE_SETTINGS_ROW_GRAPH_WINDOW,
    PAGE_SETTINGS_ROW_DATETIME,
    PAGE_SETTINGS_ROW_BRIGHTNESS,
    PAGE_SETTINGS_ROW_PRESSURE_ZERO,
    PAGE_SETTINGS_ROW_COUNT,
} page_settings_row_t;

static lv_obj_t *s_rows[PAGE_SETTINGS_ROW_COUNT];
static lv_obj_t *s_carets[PAGE_SETTINGS_ROW_COUNT];
static lv_obj_t *s_names[PAGE_SETTINGS_ROW_COUNT];
static lv_obj_t *s_values[PAGE_SETTINGS_ROW_COUNT];
static lv_obj_t *s_hints[PAGE_SETTINGS_ROW_COUNT];

static uint8_t s_cursor;
static bool s_editing;
/** Outcome of the last zero on this visit to the page; empty until then. */
static char s_zero_result[PAGE_SETTINGS_RESULT_LEN];

static const char *const s_row_names[PAGE_SETTINGS_ROW_COUNT] = {
    KDL_TXT_SETTINGS_PERIOD,
    KDL_TXT_SETTINGS_GRAPH_WINDOW,
    KDL_TXT_SETTINGS_DATETIME,
    KDL_TXT_SETTINGS_BRIGHTNESS,
    KDL_TXT_SETTINGS_ZERO,
};

/** Rows opening a sub-page show a single chevron; rows adjusted in place show
 *  the pair, matching the affordance used in the UI prototype. */
static const char *const s_row_hints[PAGE_SETTINGS_ROW_COUNT] = {
    LV_SYMBOL_LEFT LV_SYMBOL_RIGHT,
    LV_SYMBOL_LEFT LV_SYMBOL_RIGHT,
    LV_SYMBOL_RIGHT,
    LV_SYMBOL_LEFT LV_SYMBOL_RIGHT,
    "",
};

static uint8_t page_settings_nearest_preset(void)
{
    const uint32_t current = settings_service_get_acquisition_period_ms();
    for (uint8_t index = 0; index < PAGE_SETTINGS_PRESET_COUNT; ++index)
    {
        if (s_period_presets_ms[index] >= current)
        {
            return index;
        }
    }
    return PAGE_SETTINGS_PRESET_COUNT - 1U;
}

static uint8_t page_settings_window_preset(void)
{
    const uint16_t current = settings_service_get_graph_window_s();
    for (uint8_t index = 0; index < SETTINGS_GRAPH_WINDOW_PRESET_COUNT; ++index)
    {
        if (settings_graph_window_presets_s[index] >= current)
        {
            return index;
        }
    }
    return SETTINGS_GRAPH_WINDOW_PRESET_COUNT - 1U;
}

/** Step @p index by @p direction, clamped to [0, count - 1]. */
static uint8_t page_settings_step_index(uint8_t index, int8_t direction, uint8_t count)
{
    const int32_t next = (int32_t)index + direction;
    if (next < 0)
    {
        return 0;
    }
    if (next >= (int32_t)count)
    {
        return (uint8_t)(count - 1U);
    }
    return (uint8_t)next;
}

static void page_settings_format_value(page_settings_row_t row, char *out, size_t len)
{
    switch (row)
    {
    case PAGE_SETTINGS_ROW_SAMPLE_RATE:
        snprintf(out, len, "%u ms", (unsigned)settings_service_get_acquisition_period_ms());
        break;

    case PAGE_SETTINGS_ROW_GRAPH_WINDOW:
        snprintf(out, len, "%u s", (unsigned)settings_service_get_graph_window_s());
        break;

    case PAGE_SETTINGS_ROW_DATETIME:
        if (timekeeping_is_valid())
        {
            struct tm now;
            timekeeping_get(&now);
            snprintf(out, len, "%02u.%02u.%02u  %02u:%02u",
                     (unsigned)now.tm_mday % 100U, (unsigned)(now.tm_mon + 1) % 100U,
                     (unsigned)(now.tm_year + 1900) % 100U,
                     (unsigned)now.tm_hour % 100U, (unsigned)now.tm_min % 100U);
        }
        else
        {
            snprintf(out, len, KDL_TXT_SETTINGS_NOT_SET);
        }
        break;

    case PAGE_SETTINGS_ROW_BRIGHTNESS:
        snprintf(out, len, "%u %%", (unsigned)settings_service_get_brightness_percent());
        break;

    case PAGE_SETTINGS_ROW_PRESSURE_ZERO:
        snprintf(out, len, "%s", s_zero_result);
        break;

    default:
        out[0] = '\0';
        break;
    }
}

static void page_settings_refresh(void)
{
    for (uint8_t index = 0; index < PAGE_SETTINGS_ROW_COUNT; ++index)
    {
        if (s_rows[index] == NULL)
        {
            return;
        }

        const bool selected = (index == s_cursor);
        lv_obj_set_style_bg_color(s_rows[index], selected ? KDL_COLOR_INK : KDL_COLOR_CARD, 0);
        lv_obj_set_style_border_color(s_rows[index],
                                      selected ? KDL_COLOR_INK : KDL_COLOR_BORDER, 0);

        const lv_color_t ink = selected ? KDL_COLOR_PAPER : KDL_COLOR_INK;
        lv_obj_set_style_text_color(s_names[index], ink, 0);
        lv_obj_set_style_text_color(s_carets[index], ink, 0);
        lv_obj_set_style_text_color(s_hints[index],
                                    selected ? KDL_COLOR_RAIL : KDL_COLOR_INK_FAINT, 0);
        /* While editing, the value is the only thing the keys act on, so it
         * is the only thing that changes colour. */
        lv_obj_set_style_text_color(s_values[index],
                                    (selected && s_editing) ? KDL_COLOR_WARM : ink, 0);

        kdl_widget_set_text(s_carets[index], selected ? LV_SYMBOL_RIGHT : "");

        char value[24];
        page_settings_format_value((page_settings_row_t)index, value, sizeof(value));
        kdl_widget_set_text(s_values[index], value);
    }
}

static void page_settings_apply_keys(void)
{
    if (s_editing)
    {
        page_manager_set_button_label(0, KDL_TXT_KEY_PLUS);
        page_manager_set_button_label(1, KDL_TXT_KEY_MINUS);
        page_manager_set_button_label(2, KDL_TXT_KEY_DONE);
        page_manager_set_button_label(3, KDL_TXT_KEY_CANCEL);
    }
    else
    {
        page_manager_set_button_label(0, NULL);
        page_manager_set_button_label(1, NULL);
        /* The zero row is an action, not a value to edit. */
        page_manager_set_button_label(2, ((page_settings_row_t)s_cursor
                                          == PAGE_SETTINGS_ROW_PRESSURE_ZERO)
                                             ? KDL_TXT_KEY_ZERO : NULL);
        page_manager_set_button_label(3, NULL);
    }
}

static unsigned page_settings_count_bits(uint8_t mask)
{
    unsigned count = 0;
    for (; mask != 0U; mask &= (uint8_t)(mask - 1U))
    {
        ++count;
    }
    return count;
}

static void page_settings_zero_pressures(void)
{
    acquisition_zero_result_t result;
    const esp_err_t err = acquisition_service_zero_pressures(&result);
    if (err != ESP_OK)
    {
        snprintf(s_zero_result, sizeof(s_zero_result), KDL_TXT_SETTINGS_ZERO_FAIL);
    }
    else if (result.enabled_mask == 0U)
    {
        snprintf(s_zero_result, sizeof(s_zero_result), KDL_TXT_SETTINGS_ZERO_NONE);
    }
    else
    {
        snprintf(s_zero_result, sizeof(s_zero_result), KDL_TXT_SETTINGS_ZERO_FMT,
                 page_settings_count_bits(result.zeroed_mask),
                 page_settings_count_bits(result.enabled_mask));
    }
    page_settings_refresh();
}

/** Outcome of the last configuration file read, for the footer. */
static void page_settings_format_config_status(char *out, size_t len, bool *is_error)
{
    sensor_config_status_t status;
    sensor_config_get_status(&status);
    *is_error = false;

    if (status.error_count != 0U)
    {
        snprintf(out, len, KDL_TXT_CONFIG_ERROR_FMT, (unsigned)status.first_error_line);
        *is_error = true;
        return;
    }
    switch (status.source)
    {
    case SENSOR_CONFIG_SOURCE_FILE:
        snprintf(out, len, KDL_TXT_CONFIG_OK);
        break;
    case SENSOR_CONFIG_SOURCE_CREATED:
        snprintf(out, len, KDL_TXT_CONFIG_CREATED);
        break;
    case SENSOR_CONFIG_SOURCE_DEFAULTS:
    default:
        snprintf(out, len, KDL_TXT_CONFIG_UNREAD);
        *is_error = true;
        break;
    }
}

static lv_obj_t *page_settings_label(lv_obj_t *parent, const lv_font_t *font,
                                     int32_t width, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    if (width > 0)
    {
        lv_obj_set_width(label, width);
    }
    lv_label_set_text(label, "");
    return label;
}

static void on_show(lv_obj_t *content)
{
    lv_obj_set_style_pad_all(content, PAGE_SETTINGS_PAD, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, PAGE_SETTINGS_ROW_GAP, 0);

    s_editing = false;
    s_zero_result[0] = '\0';

    for (uint8_t index = 0; index < PAGE_SETTINGS_ROW_COUNT; ++index)
    {
        lv_obj_t *row = lv_obj_create(content);
        lv_obj_add_style(row, &kdl_style_panel, 0);
        lv_obj_set_size(row, PAGE_SETTINGS_INNER_W, PAGE_SETTINGS_ROW_H);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_pad_left(row, 6, 0);
        lv_obj_set_style_pad_right(row, 6, 0);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        s_rows[index] = row;

        s_carets[index] = page_settings_label(row, KDL_FONT_BODY, PAGE_SETTINGS_CARET_W,
                                              LV_TEXT_ALIGN_LEFT);
        s_names[index] = page_settings_label(row, KDL_FONT_KEY, 0, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_flex_grow(s_names[index], 1);
        lv_label_set_text(s_names[index], s_row_names[index]);

        s_values[index] = page_settings_label(row, KDL_FONT_BODY, PAGE_SETTINGS_VALUE_W,
                                              LV_TEXT_ALIGN_RIGHT);
        s_hints[index] = page_settings_label(row, KDL_FONT_BODY, PAGE_SETTINGS_HINT_W,
                                             LV_TEXT_ALIGN_RIGHT);
        lv_label_set_text(s_hints[index], s_row_hints[index]);
    }

    /* Firmware version pinned to the bottom of the page: plain text, not a
     * row, so the cursor never stops on something that cannot be changed. */
    lv_obj_t *spacer = lv_obj_create(content);
    lv_obj_add_style(spacer, &kdl_style_panel, 0);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_flex_grow(spacer, 1);

    /* Footer: configuration file outcome on the left, firmware on the right.
     * The file is only read at boot and on the way back from USB mode, both
     * of which rebuild this page, so it is formatted once here. */
    lv_obj_t *footer_row = lv_obj_create(content);
    lv_obj_add_style(footer_row, &kdl_style_panel, 0);
    lv_obj_set_size(footer_row, PAGE_SETTINGS_INNER_W, LV_SIZE_CONTENT);
    lv_obj_clear_flag(footer_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(footer_row, LV_FLEX_FLOW_ROW);

    char footer[PAGE_SETTINGS_FOOTER_LEN];
    bool config_error = false;
    page_settings_format_config_status(footer, sizeof(footer), &config_error);
    lv_obj_t *config = page_settings_label(footer_row, KDL_FONT_KEY, 0, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_flex_grow(config, 1);
    lv_obj_set_style_text_color(config, config_error ? KDL_COLOR_HOT : KDL_COLOR_INK_MUTED, 0);
    lv_label_set_text(config, footer);

    snprintf(footer, sizeof(footer), KDL_TXT_SETTINGS_FIRMWARE_FMT,
             esp_app_get_description()->version);
    lv_obj_t *version = page_settings_label(footer_row, KDL_FONT_KEY, 0, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_width(version, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(version, KDL_COLOR_INK_MUTED, 0);
    lv_label_set_text(version, footer);

    page_settings_refresh();
    page_settings_apply_keys();
}

static void on_hide(void)
{
    for (uint8_t index = 0; index < PAGE_SETTINGS_ROW_COUNT; ++index)
    {
        s_rows[index] = NULL;
        s_carets[index] = NULL;
        s_names[index] = NULL;
        s_values[index] = NULL;
        s_hints[index] = NULL;
    }
    s_editing = false;
}

static void on_tick(void)
{
    /* The clock keeps running while the page is open, so the date/time row
     * would otherwise show the minute it was drawn at. */
    page_settings_refresh();
}

static void page_settings_adjust(int8_t direction)
{
    switch ((page_settings_row_t)s_cursor)
    {
    case PAGE_SETTINGS_ROW_SAMPLE_RATE: {
        const uint8_t index = page_settings_step_index(page_settings_nearest_preset(), direction,
                                                       (uint8_t)PAGE_SETTINGS_PRESET_COUNT);
        (void)settings_service_set_acquisition_period_ms(s_period_presets_ms[index]);
        break;
    }

    case PAGE_SETTINGS_ROW_GRAPH_WINDOW: {
        const uint8_t index = page_settings_step_index(page_settings_window_preset(), direction,
                                                       (uint8_t)SETTINGS_GRAPH_WINDOW_PRESET_COUNT);
        (void)settings_service_set_graph_window_s(settings_graph_window_presets_s[index]);
        break;
    }

    case PAGE_SETTINGS_ROW_BRIGHTNESS: {
        const int32_t current = (int32_t)settings_service_get_brightness_percent();
        int32_t next = current + direction * (int32_t)SETTINGS_BRIGHTNESS_STEP;
        if (next < (int32_t)SETTINGS_BRIGHTNESS_MIN)
        {
            next = (int32_t)SETTINGS_BRIGHTNESS_MIN;
        }
        else if (next > (int32_t)SETTINGS_BRIGHTNESS_MAX)
        {
            next = (int32_t)SETTINGS_BRIGHTNESS_MAX;
        }
        if (settings_service_set_brightness_percent((uint8_t)next) == ESP_OK)
        {
            /* Applied immediately so the key press has a visible effect; the
             * settings component stores the value but does not own the panel. */
            (void)display_driver_set_backlight((uint8_t)next);
        }
        break;
    }

    default:
        break;
    }

    page_settings_refresh();
}

static void on_button(uint8_t button_index)
{
    if (s_editing)
    {
        switch (button_index)
        {
        case 0:
            page_settings_adjust(1);
            break;
        case 1:
            page_settings_adjust(-1);
            break;
        case 2:
        case 3:
            /* Both leave edit mode. Every adjustment is committed as it is
             * made, so there is no pending change for "Cancel" to roll back;
             * the key is there to give the obvious way out its usual meaning. */
            s_editing = false;
            page_settings_apply_keys();
            page_settings_refresh();
            break;
        default:
            break;
        }
        return;
    }

    switch (button_index)
    {
    case 0:
        s_cursor = (uint8_t)((s_cursor + PAGE_SETTINGS_ROW_COUNT - 1U) % PAGE_SETTINGS_ROW_COUNT);
        page_settings_apply_keys();
        page_settings_refresh();
        break;

    case 1:
        s_cursor = (uint8_t)((s_cursor + 1U) % PAGE_SETTINGS_ROW_COUNT);
        page_settings_apply_keys();
        page_settings_refresh();
        break;

    case 2:
        if ((page_settings_row_t)s_cursor == PAGE_SETTINGS_ROW_DATETIME)
        {
            page_datetime_configure(&page_settings, false);
            page_manager_switch_to(&page_datetime);
        }
        else if ((page_settings_row_t)s_cursor == PAGE_SETTINGS_ROW_PRESSURE_ZERO)
        {
            page_settings_zero_pressures();
        }
        else
        {
            s_editing = true;
            page_settings_apply_keys();
            page_settings_refresh();
        }
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

const gui_page_t page_settings = {
    .name = "settings",
    .title = KDL_TXT_SETTINGS_TITLE,
    .button_labels = {KDL_TXT_KEY_UP, KDL_TXT_KEY_DOWN, KDL_TXT_KEY_EDIT, KDL_TXT_KEY_BACK,
                      KDL_TXT_KEY_USB},
    .on_show = on_show,
    .on_hide = on_hide,
    .on_tick = on_tick,
    .on_button = on_button,
};
