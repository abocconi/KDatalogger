#include "pages.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "fw_update.h"
#include "kdl_text.h"
#include "kdl_theme.h"
#include "kdl_widgets.h"

/*
 * Firmware update page: progress while the image is written, then -- after
 * a failure, or after the reset into a new image -- a notice that stays up
 * until the operator acknowledges it. The page only mirrors fw_update's
 * status; the install itself runs in the controller task.
 */
#define PAGE_FW_PAD           14
#define PAGE_FW_INNER_W       366
#define PAGE_FW_PERCENT_W     50
#define PAGE_FW_TRACK_W       (PAGE_FW_INNER_W - PAGE_FW_PERCENT_W - 10)
#define PAGE_FW_TRACK_H       14
#define PAGE_FW_ROW_H         20
#define PAGE_FW_MESSAGE_LEN   160
#define PAGE_FW_PERCENT_LEN   8
#define PAGE_FW_OK_KEY        4

static lv_obj_t *s_title;
static lv_obj_t *s_message;
static lv_obj_t *s_progress_row;
static lv_obj_t *s_fill;
static lv_obj_t *s_percent;
static int32_t s_fill_w;
/** Last applied visibility, -1 = none yet: flag changes invalidate even when unchanged. */
static int8_t s_progress_shown;
static int8_t s_ok_shown;

void page_fw_update_poll(void)
{
    const gui_page_t *current = page_manager_get_current();
    if (current == &page_fw_update)
    {
        return;
    }

    fw_update_status_t status;
    fw_update_get_status(&status);

    if (status.phase != FW_UPDATE_PHASE_IDLE
        || (status.notice != FW_UPDATE_NOTICE_NONE && current == &page_main))
    {
        page_manager_switch_to(&page_fw_update);
    }
}

/** Title and message of a notice; false when there is nothing to show. */
static bool page_fw_update_describe_notice(const fw_update_status_t *status, const char **title,
                                           char *message, size_t len)
{
    switch (status->notice)
    {
    case FW_UPDATE_NOTICE_INSTALLED:
        *title = KDL_TXT_FW_INSTALLED;
        snprintf(message, len, KDL_TXT_FW_INSTALLED_FMT, status->version);
        return true;
    case FW_UPDATE_NOTICE_ROLLED_BACK:
        *title = KDL_TXT_FW_ROLLED_BACK;
        snprintf(message, len, KDL_TXT_FW_ROLLED_BACK_FMT, status->version);
        return true;
    case FW_UPDATE_NOTICE_INVALID_FILE:
        *title = KDL_TXT_FW_INVALID;
        snprintf(message, len, "%s", KDL_TXT_FW_INVALID_MSG);
        return true;
    case FW_UPDATE_NOTICE_MULTIPLE_FILES:
        *title = KDL_TXT_FW_MULTIPLE;
        snprintf(message, len, "%s", KDL_TXT_FW_MULTIPLE_MSG);
        return true;
    case FW_UPDATE_NOTICE_ALREADY_INSTALLED:
        *title = KDL_TXT_FW_ALREADY;
        snprintf(message, len, KDL_TXT_FW_ALREADY_FMT, status->version);
        return true;
    case FW_UPDATE_NOTICE_WRITE_FAILED:
        *title = KDL_TXT_FW_WRITE_FAILED;
        snprintf(message, len, "%s", KDL_TXT_FW_WRITE_FAILED_MSG);
        return true;
    case FW_UPDATE_NOTICE_NONE:
    default:
        return false;
    }
}

static void page_fw_update_show_progress(bool show, uint8_t percent)
{
    if (s_progress_shown != (int8_t)show)
    {
        s_progress_shown = (int8_t)show;
        if (show)
        {
            lv_obj_remove_flag(s_progress_row, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_progress_row, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!show)
    {
        return;
    }

    /* The track has a 1 px border on each side. */
    const int32_t fill_w = ((PAGE_FW_TRACK_W - 2) * (int32_t)percent) / 100;
    if (fill_w != s_fill_w)
    {
        s_fill_w = fill_w;
        lv_obj_set_width(s_fill, fill_w);
    }

    char text[PAGE_FW_PERCENT_LEN];
    snprintf(text, sizeof(text), KDL_TXT_FW_PROGRESS_FMT, (unsigned)percent);
    kdl_widget_set_text(s_percent, text);
}

static void page_fw_update_show_ok_key(bool show)
{
    if (s_ok_shown != (int8_t)show)
    {
        s_ok_shown = (int8_t)show;
        page_manager_set_button_label(PAGE_FW_OK_KEY, show ? KDL_TXT_KEY_OK : NULL);
    }
}

static void page_fw_update_refresh(void)
{
    fw_update_status_t status;
    fw_update_get_status(&status);

    char message[PAGE_FW_MESSAGE_LEN];
    const char *title = "";

    switch (status.phase)
    {
    case FW_UPDATE_PHASE_WRITING:
        title = KDL_TXT_FW_WRITING;
        snprintf(message, sizeof(message), KDL_TXT_FW_WRITING_FMT, status.version);
        page_fw_update_show_progress(true, status.progress_percent);
        page_fw_update_show_ok_key(false);
        break;
    case FW_UPDATE_PHASE_RESTARTING:
        title = KDL_TXT_FW_RESTARTING;
        snprintf(message, sizeof(message), "%s", KDL_TXT_FW_RESTARTING_MSG);
        page_fw_update_show_progress(true, 100);
        page_fw_update_show_ok_key(false);
        break;
    case FW_UPDATE_PHASE_IDLE:
    default:
        if (!page_fw_update_describe_notice(&status, &title, message, sizeof(message)))
        {
            /* Acknowledged or never raised: nothing left to show here. */
            page_manager_switch_to(&page_main);
            return;
        }
        page_fw_update_show_progress(false, 0);
        page_fw_update_show_ok_key(true);
        break;
    }

    kdl_widget_set_text(s_title, title);
    kdl_widget_set_text(s_message, message);
}

static void page_fw_update_build_progress(lv_obj_t *parent)
{
    s_progress_row = lv_obj_create(parent);
    lv_obj_add_style(s_progress_row, &kdl_style_panel, 0);
    lv_obj_set_size(s_progress_row, PAGE_FW_INNER_W, PAGE_FW_ROW_H);
    lv_obj_clear_flag(s_progress_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_progress_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_progress_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_progress_row, 10, 0);

    lv_obj_t *track = lv_obj_create(s_progress_row);
    lv_obj_add_style(track, &kdl_style_panel, 0);
    lv_obj_set_size(track, PAGE_FW_TRACK_W, PAGE_FW_TRACK_H);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(track, KDL_COLOR_TRACK, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(track, KDL_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(track, 1, 0);
    lv_obj_set_style_radius(track, 3, 0);

    s_fill = lv_obj_create(track);
    lv_obj_add_style(s_fill, &kdl_style_panel, 0);
    lv_obj_set_size(s_fill, 0, lv_pct(100));
    lv_obj_clear_flag(s_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_fill, KDL_COLOR_INK, 0);
    lv_obj_set_style_bg_opa(s_fill, LV_OPA_COVER, 0);

    s_percent = lv_label_create(s_progress_row);
    lv_obj_set_style_text_font(s_percent, KDL_FONT_KEY, 0);
    lv_obj_set_width(s_percent, PAGE_FW_PERCENT_W);
    lv_obj_set_style_text_align(s_percent, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_percent, "");
}

static void on_show(lv_obj_t *content)
{
    lv_obj_set_style_pad_all(content, PAGE_FW_PAD, 0);
    lv_obj_set_style_bg_color(content, KDL_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, 12, 0);

    s_title = lv_label_create(content);
    lv_obj_set_style_text_font(s_title, KDL_FONT_TITLE, 0);
    lv_label_set_text(s_title, "");

    s_message = lv_label_create(content);
    lv_obj_set_style_text_font(s_message, KDL_FONT_BODY, 0);
    lv_obj_set_style_text_color(s_message, KDL_COLOR_INK_DIM, 0);
    lv_obj_set_width(s_message, PAGE_FW_INNER_W);
    lv_label_set_long_mode(s_message, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_message, "");

    page_fw_update_build_progress(content);

    s_fill_w = -1;
    s_progress_shown = -1;
    s_ok_shown = -1;
    page_fw_update_refresh();
}

static void on_tick(void)
{
    page_fw_update_refresh();
}

static void on_button(uint8_t button_index)
{
    if (button_index != PAGE_FW_OK_KEY || s_ok_shown != 1)
    {
        return;
    }

    fw_update_ack_notice();
    page_manager_switch_to(&page_main);
}

const gui_page_t page_fw_update = {
    .name = "fw_update",
    .title = KDL_TXT_FW_TITLE,
    /* No keys while writing: the install cannot be cancelled halfway, and
     * USB mode must stay out until it is over. "OK" appears with a notice. */
    .button_labels = {"", "", "", "", ""},
    .on_show = on_show,
    .on_hide = NULL,
    .on_tick = on_tick,
    .on_button = on_button,
};
