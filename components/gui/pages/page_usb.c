#include "pages.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gui_actions.h"
#include "kdl_theme.h"
#include "kdl_text.h"
#include "kdl_widgets.h"
#include "logger_service.h"
#include "storage_manager.h"
#include "usb_msc_service.h"

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
/** How long a first "Esci" press without an eject stays armed. */
#define PAGE_USB_FORCE_WINDOW_MS 5000U

typedef struct {
    char file[PAGE_USB_TEXT_LEN];
    char free_space[PAGE_USB_TEXT_LEN];
    char samples[PAGE_USB_TEXT_LEN];
} page_usb_snapshot_t;

static page_usb_snapshot_t s_snapshot;
static lv_obj_t *s_alert_text;
static bool s_force_armed;
static uint32_t s_force_armed_at_ms;


static const char *const s_stat_captions[PAGE_USB_STAT_COUNT] = {
    KDL_TXT_USB_STAT_FILE, KDL_TXT_USB_STAT_FREE, KDL_TXT_USB_STAT_SAMPLES,
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

/** Decimal units (1 MB = 1 000 000 B), as the macOS Finder counts them, so
 *  the figure matches what the operator sees on the computer; one decimal,
 *  rounded, with the Italian decimal comma. */
static void page_usb_format_bytes(uint64_t bytes, char *out, size_t len)
{
    const bool giga = bytes >= 1000000000ULL;
    const uint64_t tenth_unit = giga ? 100000000ULL : 100000ULL;
    const uint64_t tenths = (bytes + tenth_unit / 2U) / tenth_unit;
    snprintf(out, len, "%u,%u %s", (unsigned)(tenths / 10U), (unsigned)(tenths % 10U),
             giga ? "GB" : "MB");
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
        snprintf(s_snapshot.file, sizeof(s_snapshot.file), KDL_TXT_USB_NO_FILE);
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
    lv_obj_set_style_text_font(title, KDL_FONT_TITLE, 0);
    lv_label_set_text(title, KDL_TXT_USB_HEADER);

    lv_obj_t *subtitle = lv_label_create(text);
    lv_obj_set_style_text_font(subtitle, KDL_FONT_BODY, 0);
    lv_obj_set_style_text_color(subtitle, KDL_COLOR_INK_DIM, 0);
    lv_label_set_text(subtitle, KDL_TXT_USB_SUBTITLE);
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

    s_alert_text = lv_label_create(alert);
    lv_obj_set_style_text_font(s_alert_text, KDL_FONT_BODY, 0);
    lv_label_set_text(s_alert_text, KDL_TXT_USB_ALERT_EJECT);
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

    s_force_armed = false;
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
    /* Key 4 is "USB" on every other page, so the same key enters and
     * leaves the mode. */
    if (button_index != 4)
    {
        return;
    }

    /* Leaving while the computer still has the drive mounted is allowed --
     * the host may have crashed or the cable been pulled unseen -- but only on
     * a second press, so skipping the eject is never an accident. */
    if (usb_msc_service_host_holds_volume() && !s_force_armed)
    {
        s_force_armed = true;
        s_force_armed_at_ms = lv_tick_get();
        lv_label_set_text(s_alert_text, KDL_TXT_USB_ALERT_FORCE);
        return;
    }

    page_usb_resume();
}

static void on_tick(void)
{
    /* The eject on the computer is the normal way out: once the host gives
     * the volume back there is nothing left for the operator to do here. */
    if (usb_msc_service_host_released())
    {
        page_usb_resume();
        return;
    }

    if (s_force_armed && lv_tick_elaps(s_force_armed_at_ms) >= PAGE_USB_FORCE_WINDOW_MS)
    {
        s_force_armed = false;
        lv_label_set_text(s_alert_text, KDL_TXT_USB_ALERT_EJECT);
    }
}

const gui_page_t page_usb = {
    .name = "usb",
    .title = KDL_TXT_USB_TITLE,
    /* No "eject" key: the eject belongs on the computer, and a key named so
     * here is what made skipping it feel safe. */
    .button_labels = {"", "", "", "", KDL_TXT_KEY_EXIT},
    .on_show = on_show,
    .on_hide = NULL,
    .on_tick = on_tick,
    .on_button = on_button,
};
