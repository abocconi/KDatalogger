#include "pages.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "kdl_theme.h"
#include "kdl_widgets.h"
#include "timekeeping.h"

/*
 * Date/time entry mask.
 *
 * Five keys have to cover "pick a field", "change it", "accept" and "get out",
 * so the field cursor gets its own key rather than being folded onto the
 * value keys. At power-up the mask also has to be escapable without input:
 * a restart mid-event must not leave the logger sitting at a dialog waiting
 * for someone to walk over to it.
 */
#define PAGE_DATETIME_PAD          6
#define PAGE_DATETIME_INNER_W      382
#define PAGE_DATETIME_FIELD_ROW_H  44
#define PAGE_DATETIME_HINT_H       16
#define PAGE_DATETIME_BOOT_TIMEOUT_S 10
#define PAGE_DATETIME_COUNTDOWN_TEXT_LEN 64

#define PAGE_DATETIME_YEAR_MIN 2026
#define PAGE_DATETIME_YEAR_MAX 2099

typedef enum {
    PAGE_DATETIME_FIELD_DAY = 0,
    PAGE_DATETIME_FIELD_MONTH,
    PAGE_DATETIME_FIELD_YEAR,
    PAGE_DATETIME_FIELD_HOUR,
    PAGE_DATETIME_FIELD_MINUTE,
    PAGE_DATETIME_FIELD_COUNT,
} page_datetime_field_t;

static lv_obj_t *s_field_labels[PAGE_DATETIME_FIELD_COUNT];
static lv_obj_t *s_countdown_label;
static lv_timer_t *s_timeout_timer;

static const gui_page_t *s_return_page;
static bool s_boot_mode;
static uint8_t s_field;
static uint32_t s_remaining_s;

/* Working copy: the system clock is only touched if the operator accepts. */
static struct tm s_edit;

static const int32_t s_field_widths[PAGE_DATETIME_FIELD_COUNT] = { 44, 44, 82, 44, 44 };

void page_datetime_configure(const gui_page_t *return_page, bool boot_mode)
{
    s_return_page = return_page;
    s_boot_mode = boot_mode;
}

static uint8_t page_datetime_days_in_month(int year, int month)
{
    static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if (month < 1 || month > 12)
    {
        return 31;
    }
    if (month == 2)
    {
        const bool leap = ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
        return leap ? 29U : 28U;
    }
    return days[month - 1];
}

/** Keep the day inside the selected month: stepping from 31 January to
 *  February must not leave an impossible date behind. */
static void page_datetime_clamp_day(void)
{
    const uint8_t limit = page_datetime_days_in_month(s_edit.tm_year + 1900, s_edit.tm_mon + 1);
    if (s_edit.tm_mday > (int)limit)
    {
        s_edit.tm_mday = (int)limit;
    }
    if (s_edit.tm_mday < 1)
    {
        s_edit.tm_mday = 1;
    }
}

static void page_datetime_refresh(void)
{
    if (s_field_labels[0] == NULL)
    {
        return;
    }

    char text[8];
    const int values[PAGE_DATETIME_FIELD_COUNT] = {
        s_edit.tm_mday, s_edit.tm_mon + 1, s_edit.tm_year + 1900,
        s_edit.tm_hour, s_edit.tm_min,
    };

    for (uint8_t index = 0; index < PAGE_DATETIME_FIELD_COUNT; ++index)
    {
        if (index == PAGE_DATETIME_FIELD_YEAR)
        {
            snprintf(text, sizeof(text), "%04u", (unsigned)values[index] % 10000U);
        }
        else
        {
            snprintf(text, sizeof(text), "%02u", (unsigned)values[index] % 100U);
        }
        kdl_widget_set_text(s_field_labels[index], text);

        const bool active = (index == s_field);
        lv_obj_set_style_text_color(s_field_labels[index],
                                    active ? KDL_COLOR_HOT : KDL_COLOR_INK, 0);
        lv_obj_set_style_border_color(s_field_labels[index], KDL_COLOR_HOT, 0);
        lv_obj_set_style_border_width(s_field_labels[index], active ? 2 : 0, 0);
        lv_obj_set_style_border_side(s_field_labels[index], LV_BORDER_SIDE_BOTTOM, 0);
    }
}

static void page_datetime_dismiss(void)
{
    const gui_page_t *target = (s_return_page != NULL) ? s_return_page : &page_main;
    page_manager_switch_to(target);
}

/** The remaining seconds are reduced modulo 100 before printing so the
 *  compiler can bound the field; the timeout is a two-digit value anyway. */
static void page_datetime_render_countdown(void)
{
    if (s_countdown_label == NULL)
    {
        return;
    }

    char text[PAGE_DATETIME_COUNTDOWN_TEXT_LEN];
    snprintf(text, sizeof(text), "Continuing without setting the clock in %u s",
             (unsigned)(s_remaining_s % 100U));
    kdl_widget_set_text(s_countdown_label, text);
}

static void page_datetime_timeout_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_remaining_s > 0U)
    {
        s_remaining_s--;
    }

    if (s_remaining_s == 0U)
    {
        page_datetime_dismiss();
        return;
    }

    page_datetime_render_countdown();
}

static lv_obj_t *page_datetime_separator(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, KDL_FONT_VALUE, 0);
    lv_obj_set_style_text_color(label, KDL_COLOR_INK_FAINT, 0);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *page_datetime_field(lv_obj_t *parent, uint8_t index)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, KDL_FONT_VALUE, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(label, s_field_widths[index], PAGE_DATETIME_FIELD_ROW_H);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, "00");
    s_field_labels[index] = label;
    return label;
}

static void on_show(lv_obj_t *content)
{
    lv_obj_set_style_pad_all(content, PAGE_DATETIME_PAD, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 12, 0);

    /* Seed from the last value the operator entered, so a restart means
     * confirming a date rather than typing one from scratch. */
    timekeeping_get_setup_default(&s_edit);
    s_edit.tm_sec = 0;
    s_edit.tm_isdst = 0;
    if (s_edit.tm_year + 1900 < PAGE_DATETIME_YEAR_MIN)
    {
        s_edit.tm_year = PAGE_DATETIME_YEAR_MIN - 1900;
    }
    s_field = PAGE_DATETIME_FIELD_DAY;

    lv_obj_t *row = lv_obj_create(content);
    lv_obj_add_style(row, &kdl_style_panel, 0);
    lv_obj_set_size(row, PAGE_DATETIME_INNER_W, PAGE_DATETIME_FIELD_ROW_H);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 2, 0);

    page_datetime_field(row, PAGE_DATETIME_FIELD_DAY);
    page_datetime_separator(row, ".");
    page_datetime_field(row, PAGE_DATETIME_FIELD_MONTH);
    page_datetime_separator(row, ".");
    page_datetime_field(row, PAGE_DATETIME_FIELD_YEAR);
    page_datetime_separator(row, "  ");
    page_datetime_field(row, PAGE_DATETIME_FIELD_HOUR);
    page_datetime_separator(row, ":");
    page_datetime_field(row, PAGE_DATETIME_FIELD_MINUTE);

    lv_obj_t *hint = lv_label_create(content);
    lv_obj_set_style_text_font(hint, KDL_FONT_BODY, 0);
    lv_obj_set_style_text_color(hint, KDL_COLOR_INK_MUTED, 0);
    lv_obj_set_size(hint, PAGE_DATETIME_INNER_W, PAGE_DATETIME_HINT_H);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint, "No battery-backed clock: the time is lost at power-down.");

    s_countdown_label = lv_label_create(content);
    lv_obj_set_style_text_font(s_countdown_label, KDL_FONT_BODY, 0);
    lv_obj_set_style_text_color(s_countdown_label, KDL_COLOR_HOT, 0);
    lv_obj_set_size(s_countdown_label, PAGE_DATETIME_INNER_W, PAGE_DATETIME_HINT_H);
    lv_obj_set_style_text_align(s_countdown_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_countdown_label, "");

    if (s_boot_mode)
    {
        page_manager_set_button_label(4, "Skip");
        s_remaining_s = PAGE_DATETIME_BOOT_TIMEOUT_S;
        page_datetime_render_countdown();
        s_timeout_timer = lv_timer_create(page_datetime_timeout_cb, 1000, NULL);
    }
    else
    {
        page_manager_set_button_label(4, "Cancel");
    }

    page_datetime_refresh();
}

static void on_hide(void)
{
    if (s_timeout_timer != NULL)
    {
        lv_timer_del(s_timeout_timer);
        s_timeout_timer = NULL;
    }

    for (uint8_t index = 0; index < PAGE_DATETIME_FIELD_COUNT; ++index)
    {
        s_field_labels[index] = NULL;
    }
    s_countdown_label = NULL;
    s_boot_mode = false;
}

/** Any key press means someone is at the panel, so the unattended-restart
 *  escape hatch is no longer wanted. */
static void page_datetime_cancel_timeout(void)
{
    if (s_timeout_timer != NULL)
    {
        lv_timer_del(s_timeout_timer);
        s_timeout_timer = NULL;
        kdl_widget_set_text(s_countdown_label, "");
    }
}

static void page_datetime_step(int8_t direction)
{
    switch ((page_datetime_field_t)s_field)
    {
    case PAGE_DATETIME_FIELD_DAY: {
        const int limit = (int)page_datetime_days_in_month(s_edit.tm_year + 1900,
                                                           s_edit.tm_mon + 1);
        s_edit.tm_mday += direction;
        if (s_edit.tm_mday < 1)
        {
            s_edit.tm_mday = limit;
        }
        else if (s_edit.tm_mday > limit)
        {
            s_edit.tm_mday = 1;
        }
        break;
    }

    case PAGE_DATETIME_FIELD_MONTH:
        s_edit.tm_mon += direction;
        if (s_edit.tm_mon < 0)
        {
            s_edit.tm_mon = 11;
        }
        else if (s_edit.tm_mon > 11)
        {
            s_edit.tm_mon = 0;
        }
        page_datetime_clamp_day();
        break;

    case PAGE_DATETIME_FIELD_YEAR: {
        int year = s_edit.tm_year + 1900 + direction;
        if (year < PAGE_DATETIME_YEAR_MIN)
        {
            year = PAGE_DATETIME_YEAR_MAX;
        }
        else if (year > PAGE_DATETIME_YEAR_MAX)
        {
            year = PAGE_DATETIME_YEAR_MIN;
        }
        s_edit.tm_year = year - 1900;
        page_datetime_clamp_day();
        break;
    }

    case PAGE_DATETIME_FIELD_HOUR:
        s_edit.tm_hour = (s_edit.tm_hour + direction + 24) % 24;
        break;

    case PAGE_DATETIME_FIELD_MINUTE:
        s_edit.tm_min = (s_edit.tm_min + direction + 60) % 60;
        break;

    default:
        break;
    }

    page_datetime_refresh();
}

static void on_button(uint8_t button_index)
{
    page_datetime_cancel_timeout();

    switch (button_index)
    {
    case 0:
        page_datetime_step(1);
        break;

    case 1:
        page_datetime_step(-1);
        break;

    case 2:
        s_field = (uint8_t)((s_field + 1U) % PAGE_DATETIME_FIELD_COUNT);
        page_datetime_refresh();
        break;

    case 3:
        (void)timekeeping_set(&s_edit);
        page_datetime_dismiss();
        break;

    case 4:
        page_datetime_dismiss();
        break;

    default:
        break;
    }
}

const gui_page_t page_datetime = {
    .name = "datetime",
    .title = "SET DATE / TIME",
    .button_labels = {"+", "-", "Field", "OK", "Cancel"},
    .on_show = on_show,
    .on_hide = on_hide,
    .on_tick = NULL,
    .on_button = on_button,
};
