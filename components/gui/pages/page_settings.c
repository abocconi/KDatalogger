#include "pages.h"

#include <stdint.h>
#include <stdio.h>

#include "display_driver.h"
#include "kdl_theme.h"
#include "kdl_widgets.h"
#include "settings_service.h"
#include "timekeeping.h"

/*
 * Settings list geometry. Rows are a fixed height and stack from the top
 * rather than sharing the content box between them: the list is short today
 * and stretching three rows over 282 px would read as a layout bug.
 */
#define PAGE_SETTINGS_PAD       6
#define PAGE_SETTINGS_INNER_W   382
#define PAGE_SETTINGS_ROW_H     34
#define PAGE_SETTINGS_ROW_GAP   3
#define PAGE_SETTINGS_CARET_W   10
#define PAGE_SETTINGS_VALUE_W   110
#define PAGE_SETTINGS_HINT_W    26

/** Selectable acquisition periods, in ms. A preset list rather than a free
 *  counter: stepping one millisecond at a time through a 100-5000 range with
 *  two keys is unusable, and only a few rates are meaningful given the
 *  MAX31855 conversion time. */
static const uint32_t s_period_presets_ms[] = { 100U, 200U, 250U, 500U, 1000U, 2000U, 5000U };
#define PAGE_SETTINGS_PRESET_COUNT (sizeof(s_period_presets_ms) / sizeof(s_period_presets_ms[0]))

typedef enum {
    PAGE_SETTINGS_ROW_SAMPLE_RATE = 0,
    PAGE_SETTINGS_ROW_DATETIME,
    PAGE_SETTINGS_ROW_BRIGHTNESS,
    PAGE_SETTINGS_ROW_COUNT,
} page_settings_row_t;

static lv_obj_t *s_rows[PAGE_SETTINGS_ROW_COUNT];
static lv_obj_t *s_carets[PAGE_SETTINGS_ROW_COUNT];
static lv_obj_t *s_names[PAGE_SETTINGS_ROW_COUNT];
static lv_obj_t *s_values[PAGE_SETTINGS_ROW_COUNT];
static lv_obj_t *s_hints[PAGE_SETTINGS_ROW_COUNT];

static uint8_t s_cursor;
static bool s_editing;

static const char *const s_row_names[PAGE_SETTINGS_ROW_COUNT] = {
    "Sample rate",
    "Date / time",
    "Display brightness",
};

/** Rows opening a sub-page show a single chevron; rows adjusted in place show
 *  the pair, matching the affordance used in the UI prototype. */
static const char *const s_row_hints[PAGE_SETTINGS_ROW_COUNT] = {
    LV_SYMBOL_LEFT LV_SYMBOL_RIGHT,
    LV_SYMBOL_RIGHT,
    LV_SYMBOL_LEFT LV_SYMBOL_RIGHT,
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

static void page_settings_format_value(page_settings_row_t row, char *out, size_t len)
{
    switch (row)
    {
    case PAGE_SETTINGS_ROW_SAMPLE_RATE:
        snprintf(out, len, "%u ms", (unsigned)settings_service_get_acquisition_period_ms());
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
            snprintf(out, len, "not set");
        }
        break;

    case PAGE_SETTINGS_ROW_BRIGHTNESS:
        snprintf(out, len, "%u %%", (unsigned)settings_service_get_brightness_percent());
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
        page_manager_set_button_label(0, "+");
        page_manager_set_button_label(1, "-");
        page_manager_set_button_label(2, "Done");
        page_manager_set_button_label(3, "Cancel");
    }
    else
    {
        page_manager_set_button_label(0, NULL);
        page_manager_set_button_label(1, NULL);
        page_manager_set_button_label(2, NULL);
        page_manager_set_button_label(3, NULL);
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
        int32_t index = (int32_t)page_settings_nearest_preset() + direction;
        if (index < 0)
        {
            index = 0;
        }
        else if (index >= (int32_t)PAGE_SETTINGS_PRESET_COUNT)
        {
            index = (int32_t)PAGE_SETTINGS_PRESET_COUNT - 1;
        }
        (void)settings_service_set_acquisition_period_ms(s_period_presets_ms[index]);
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
        page_settings_refresh();
        break;

    case 1:
        s_cursor = (uint8_t)((s_cursor + 1U) % PAGE_SETTINGS_ROW_COUNT);
        page_settings_refresh();
        break;

    case 2:
        if ((page_settings_row_t)s_cursor == PAGE_SETTINGS_ROW_DATETIME)
        {
            page_datetime_configure(&page_settings, false);
            page_manager_switch_to(&page_datetime);
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
    .title = "SETTINGS",
    .button_labels = {"Up", "Down", "Edit", "Back", "USB"},
    .on_show = on_show,
    .on_hide = on_hide,
    .on_tick = on_tick,
    .on_button = on_button,
};
