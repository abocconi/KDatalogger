#include "pages.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gui_actions.h"
#include "kdl_theme.h"
#include "kdl_widgets.h"
#include "logger_service.h"
#include "storage_manager.h"

/*
 * USB mass-storage page. Everything it shows is a snapshot taken at the
 * moment the volume was handed over: while the host owns the partition the
 * firmware has no filesystem to query, so the figures cannot be refreshed
 * and must not pretend to be live.
 */
#define PAGE_USB_PAD          14
#define PAGE_USB_INNER_W      366
#define PAGE_USB_HEADER_H     40
#define PAGE_USB_STATS_H      46
#define PAGE_USB_ALERT_H      30
#define PAGE_USB_BADGE        34
#define PAGE_USB_STAT_COUNT   3
#define PAGE_USB_TEXT_LEN     24

typedef struct {
    char file[PAGE_USB_TEXT_LEN];
    char free_space[PAGE_USB_TEXT_LEN];
    char samples[PAGE_USB_TEXT_LEN];
} page_usb_snapshot_t;

static page_usb_snapshot_t s_snapshot;

static const char *const s_stat_captions[PAGE_USB_STAT_COUNT] = {
    "Session file", "Card free", "Samples",
};

/** Group a count in threes so a six-figure sample total stays readable at a
 *  glance. A plain space is used as the separator: it is unambiguous in every
 *  locale, unlike a dot or a comma. */
static void page_usb_format_grouped(uint32_t value, char *out, size_t len)
{
    char plain[12];
    const int digits = snprintf(plain, sizeof(plain), "%u", (unsigned)value);
    if (digits <= 0 || (size_t)digits >= sizeof(plain))
    {
        snprintf(out, len, "0");
        return;
    }

    size_t written = 0;
    for (int index = 0; index < digits && written + 2 < len; ++index)
    {
        if (index > 0 && ((digits - index) % 3) == 0)
        {
            out[written++] = ' ';
        }
        out[written++] = plain[index];
    }
    out[written] = '\0';
}

static void page_usb_format_bytes(uint64_t bytes, char *out, size_t len)
{
    if (bytes >= (1024ULL * 1024ULL * 1024ULL))
    {
        const uint64_t tenths = (bytes * 10ULL) / (1024ULL * 1024ULL * 1024ULL);
        snprintf(out, len, "%u.%u GB", (unsigned)(tenths / 10ULL), (unsigned)(tenths % 10ULL));
    }
    else
    {
        snprintf(out, len, "%u MB", (unsigned)(bytes / (1024ULL * 1024ULL)));
    }
}

static void page_usb_capture_snapshot(void)
{
    const char *path = logger_service_get_current_path();
    if (path != NULL && path[0] != '\0')
    {
        /* Only the file name: the directory is fixed and would eat the cell. */
        const char *name = strrchr(path, '/');
        snprintf(s_snapshot.file, sizeof(s_snapshot.file), "%s", name != NULL ? name + 1 : path);
    }
    else
    {
        snprintf(s_snapshot.file, sizeof(s_snapshot.file), "none");
    }

    uint64_t total = 0;
    uint64_t available = 0;
    if (storage_manager_get_usage(&total, &available) == ESP_OK)
    {
        page_usb_format_bytes(available, s_snapshot.free_space, sizeof(s_snapshot.free_space));
    }
    else
    {
        snprintf(s_snapshot.free_space, sizeof(s_snapshot.free_space), "--");
    }

    page_usb_format_grouped(logger_service_get_sample_count(), s_snapshot.samples,
                            sizeof(s_snapshot.samples));
}

void page_usb_enter(void)
{
    page_usb_capture_snapshot();
    gui_action_toggle_usb_msc();

    if (gui_action_is_usb_msc_active())
    {
        page_manager_switch_to(&page_usb);
    }
}

static lv_obj_t *page_usb_box(lv_obj_t *parent, int32_t width, int32_t height)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_add_style(box, &kdl_style_panel, 0);
    lv_obj_set_size(box, width, height);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

static void page_usb_build_header(lv_obj_t *parent)
{
    lv_obj_t *header = page_usb_box(parent, PAGE_USB_INNER_W, PAGE_USB_HEADER_H);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 10, 0);

    lv_obj_t *badge = page_usb_box(header, PAGE_USB_BADGE, PAGE_USB_BADGE);
    lv_obj_set_style_bg_color(badge, KDL_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(badge, KDL_COLOR_INK, 0);
    lv_obj_set_style_border_width(badge, 2, 0);
    lv_obj_t *badge_icon = lv_label_create(badge);
    lv_obj_set_style_text_font(badge_icon, KDL_FONT_KEY, 0);
    lv_label_set_text(badge_icon, LV_SYMBOL_USB);
    lv_obj_center(badge_icon);

    lv_obj_t *text = page_usb_box(header, PAGE_USB_INNER_W - PAGE_USB_BADGE - 10,
                                  PAGE_USB_HEADER_H);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(text);
    lv_obj_set_style_text_font(title, KDL_FONT_VALUE_SM, 0);
    lv_label_set_text(title, "MASS STORAGE MODE");

    lv_obj_t *subtitle = lv_label_create(text);
    lv_obj_set_style_text_font(subtitle, KDL_FONT_BODY, 0);
    lv_obj_set_style_text_color(subtitle, KDL_COLOR_INK_DIM, 0);
    lv_label_set_text(subtitle, "Acquisition stopped. Log volume mounted on the computer.");
}

static void page_usb_build_stats(lv_obj_t *parent)
{
    lv_obj_t *stats = page_usb_box(parent, PAGE_USB_INNER_W, PAGE_USB_STATS_H);
    lv_obj_set_style_bg_color(stats, KDL_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(stats, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(stats, KDL_COLOR_INK, 0);
    lv_obj_set_style_border_width(stats, 1, 0);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);

    const char *const values[PAGE_USB_STAT_COUNT] = {
        s_snapshot.file, s_snapshot.free_space, s_snapshot.samples,
    };
    const int32_t cell_w = (PAGE_USB_INNER_W - 2) / PAGE_USB_STAT_COUNT;

    for (uint8_t index = 0; index < PAGE_USB_STAT_COUNT; ++index)
    {
        lv_obj_t *cell = page_usb_box(stats, cell_w, PAGE_USB_STATS_H - 2);
        lv_obj_set_style_pad_left(cell, 7, 0);
        lv_obj_set_style_pad_right(cell, 7, 0);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(cell, 2, 0);
        if (index + 1U < PAGE_USB_STAT_COUNT)
        {
            lv_obj_set_style_border_color(cell, KDL_COLOR_BORDER, 0);
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_border_side(cell, LV_BORDER_SIDE_RIGHT, 0);
        }

        lv_obj_t *caption = lv_label_create(cell);
        lv_obj_set_style_text_font(caption, KDL_FONT_MICRO, 0);
        lv_obj_set_style_text_color(caption, KDL_COLOR_INK_MUTED, 0);
        lv_label_set_text(caption, s_stat_captions[index]);

        lv_obj_t *value = lv_label_create(cell);
        lv_obj_set_style_text_font(value, KDL_FONT_KEY, 0);
        lv_obj_set_width(value, cell_w - 14);
        lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
        lv_label_set_text(value, values[index]);
    }
}

static void page_usb_build_alert(lv_obj_t *parent)
{
    lv_obj_t *alert = page_usb_box(parent, PAGE_USB_INNER_W, PAGE_USB_ALERT_H);
    lv_obj_add_style(alert, &kdl_style_alert, 0);
    lv_obj_set_flex_flow(alert, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(alert, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(alert, 6, 0);

    lv_obj_t *icon = lv_label_create(alert);
    lv_obj_set_style_text_font(icon, KDL_FONT_BODY, 0);
    lv_obj_set_style_text_color(icon, KDL_COLOR_ALERT_BORDER, 0);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);

    lv_obj_t *text = lv_label_create(alert);
    lv_obj_set_style_text_font(text, KDL_FONT_BODY, 0);
    lv_label_set_text(text, "Eject the drive on the computer before resuming.");
}

static void on_show(lv_obj_t *content)
{
    lv_obj_set_style_pad_all(content, PAGE_USB_PAD, 0);
    lv_obj_set_style_bg_color(content, KDL_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, 9, 0);

    page_usb_build_header(content);
    page_usb_build_stats(content);
    page_usb_build_alert(content);
}

static void page_usb_resume(void)
{
    if (gui_action_is_usb_msc_active())
    {
        gui_action_toggle_usb_msc();
    }
    page_manager_switch_to(&page_main);
}

static void on_button(uint8_t button_index)
{
    /* Both the top and the bottom key leave the mode. The prototype labels
     * them differently but they do the same thing, and in the field the
     * forgiving option beats the tidy one. */
    if (button_index == 0 || button_index == 4)
    {
        page_usb_resume();
    }
}

const gui_page_t page_usb = {
    .name = "usb",
    .title = "USB MSC",
    .button_labels = {"Resume", "", "", "", "Eject"},
    .on_show = on_show,
    .on_hide = NULL,
    .on_tick = NULL,
    .on_button = on_button,
};
