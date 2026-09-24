#include "page_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"

#include "display_driver.h"
#include "gui_actions.h"
#include "kdl_theme.h"
#include "kdl_text.h"
#include "logger_service.h"
#include "timekeeping.h"

/*
 * Chrome geometry, in LVGL logical pixels.
 *
 * The five key slots are anchored to the bottom edge of the panel because the
 * physical keys are: the lowest slot must sit flush with the bottom bezel. The
 * clock block then absorbs whatever height is left between the status bar and
 * the topmost slot, which is why its height is derived rather than chosen --
 * changing PAGE_MANAGER_KEY_CELL_H to match the real key pitch automatically
 * keeps the labels aligned with the keys.
 */
#define PAGE_MANAGER_STATUSBAR_H 26
#define PAGE_MANAGER_RAIL_W      86
#define PAGE_MANAGER_KEY_CELL_H  50
#define PAGE_MANAGER_BODY_H      (DISPLAY_PANEL_VER_RES - PAGE_MANAGER_STATUSBAR_H)
#define PAGE_MANAGER_CLOCK_H     (PAGE_MANAGER_BODY_H \
                                  - PAGE_MANAGER_BUTTON_COUNT * PAGE_MANAGER_KEY_CELL_H)
#define PAGE_MANAGER_CONTENT_W   (DISPLAY_PANEL_HOR_RES - PAGE_MANAGER_RAIL_W)

_Static_assert(PAGE_MANAGER_CLOCK_H > 0,
               "key slots overflow the body: reduce PAGE_MANAGER_KEY_CELL_H");

#define PAGE_MANAGER_REC_DOT_SIZE 8
#define PAGE_MANAGER_CLOCK_TEXT_LEN 8   /* "HH:MM"    */
#define PAGE_MANAGER_DATE_TEXT_LEN 12   /* "DD.MM.YY" */
#define PAGE_MANAGER_ELAPSED_TEXT_LEN 12 /* "HH:MM:SS" */

static lv_obj_t *s_content;
static lv_obj_t *s_statusbar;
static lv_obj_t *s_body;
static lv_obj_t *s_rail;
static lv_obj_t *s_rec_dot;
static lv_obj_t *s_rec_label;
static lv_obj_t *s_elapsed_label;
static lv_obj_t *s_title_label;
static lv_obj_t *s_clock_label;
static lv_obj_t *s_date_label;
static lv_obj_t *s_key_slots[PAGE_MANAGER_BUTTON_COUNT];
static lv_obj_t *s_key_labels[PAGE_MANAGER_BUTTON_COUNT];
static lv_obj_t *s_key_ticks[PAGE_MANAGER_BUTTON_COUNT];

static const gui_page_t *s_current_page;
static const char *s_label_override[PAGE_MANAGER_BUTTON_COUNT];

static bool s_rec_blink_on;
static bool s_was_recording;
static int64_t s_session_start_us;
static int64_t s_session_frozen_us;

/** Write only on change: lv_label_set_text() invalidates unconditionally, and
 *  a needless invalidate here costs a multi-strip SPI flush every tick. */
static void page_manager_set_text(lv_obj_t *label, const char *text)
{
    if (label == NULL || text == NULL)
    {
        return;
    }

    if (strcmp(lv_label_get_text(label), text) != 0)
    {
        lv_label_set_text(label, text);
    }
}

static lv_obj_t *page_manager_create_box(lv_obj_t *parent, int32_t width, int32_t height)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_add_style(box, &kdl_style_panel, 0);
    lv_obj_set_size(box, width, height);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

static void page_manager_build_statusbar(lv_obj_t *parent)
{
    lv_obj_t *bar = page_manager_create_box(parent, lv_pct(100), PAGE_MANAGER_STATUSBAR_H);
    s_statusbar = bar;
    lv_obj_add_style(bar, &kdl_style_statusbar, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);

    s_rec_dot = lv_obj_create(bar);
    lv_obj_add_style(s_rec_dot, &kdl_style_panel, 0);
    lv_obj_set_size(s_rec_dot, PAGE_MANAGER_REC_DOT_SIZE, PAGE_MANAGER_REC_DOT_SIZE);
    lv_obj_set_style_radius(s_rec_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_rec_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_rec_dot, KDL_COLOR_REC_STOPPED, 0);
    lv_obj_clear_flag(s_rec_dot, LV_OBJ_FLAG_SCROLLABLE);

    s_rec_label = lv_label_create(bar);
    lv_obj_set_style_text_font(s_rec_label, KDL_FONT_BODY, 0);
    lv_label_set_text(s_rec_label, KDL_TXT_STATUS_REC);

    s_elapsed_label = lv_label_create(bar);
    lv_obj_set_style_text_font(s_elapsed_label, KDL_FONT_BODY, 0);
    /* Fixed width: the proportional Montserrat digits would otherwise resize
     * the label every second and reflow the whole status bar. */
    lv_obj_set_width(s_elapsed_label, 70);
    lv_label_set_long_mode(s_elapsed_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_elapsed_label, "00:00:00");

    lv_obj_t *spacer = page_manager_create_box(bar, 1, 1);
    lv_obj_set_flex_grow(spacer, 1);

    s_title_label = lv_label_create(bar);
    lv_obj_set_style_text_font(s_title_label, KDL_FONT_MICRO, 0);
    lv_obj_set_style_text_color(s_title_label, KDL_COLOR_RAIL, 0);
    lv_label_set_long_mode(s_title_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_title_label, "");
}

static void page_manager_build_clock_block(lv_obj_t *rail)
{
    lv_obj_t *block = page_manager_create_box(rail, lv_pct(100), PAGE_MANAGER_CLOCK_H);
    lv_obj_set_style_bg_color(block, KDL_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(block, KDL_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(block, 1, 0);
    lv_obj_set_style_border_side(block, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_right(block, 6, 0);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    s_clock_label = lv_label_create(block);
    lv_obj_set_style_text_font(s_clock_label, KDL_FONT_VALUE_SM, 0);
    lv_obj_set_style_text_align(s_clock_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_clock_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_clock_label, "--:--");

    s_date_label = lv_label_create(block);
    lv_obj_set_style_text_font(s_date_label, KDL_FONT_MICRO, 0);
    lv_obj_set_style_text_color(s_date_label, KDL_COLOR_INK_MUTED, 0);
    lv_obj_set_style_text_align(s_date_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_date_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_date_label, "--.--.--");
}

static void page_manager_build_rail(lv_obj_t *parent)
{
    lv_obj_t *rail = page_manager_create_box(parent, PAGE_MANAGER_RAIL_W, lv_pct(100));
    s_rail = rail;
    lv_obj_add_style(rail, &kdl_style_rail, 0);
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);

    page_manager_build_clock_block(rail);

    for (uint8_t index = 0; index < PAGE_MANAGER_BUTTON_COUNT; ++index)
    {
        lv_obj_t *slot = page_manager_create_box(rail, lv_pct(100), PAGE_MANAGER_KEY_CELL_H);
        lv_obj_set_style_bg_opa(slot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(slot, KDL_COLOR_BORDER, 0);
        lv_obj_set_style_border_width(slot, 1, 0);
        lv_obj_set_style_border_side(slot, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_pad_left(slot, 5, 0);
        lv_obj_set_style_pad_right(slot, 6, 0);
        lv_obj_set_style_pad_column(slot, 5, 0);
        lv_obj_set_flex_flow(slot, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(slot, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        s_key_slots[index] = slot;

        s_key_labels[index] = lv_label_create(slot);
        lv_obj_set_flex_grow(s_key_labels[index], 1);
        lv_obj_set_style_text_font(s_key_labels[index], KDL_FONT_KEY, 0);
        lv_obj_set_style_text_align(s_key_labels[index], LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(s_key_labels[index], LV_LABEL_LONG_CLIP);
        lv_label_set_text(s_key_labels[index], "");

        /* Filled right-pointing triangle aimed at the physical key. */
        s_key_ticks[index] = lv_label_create(slot);
        lv_obj_set_style_text_font(s_key_ticks[index], KDL_FONT_MICRO, 0);
        lv_label_set_text(s_key_ticks[index], LV_SYMBOL_PLAY);
    }
}

static void page_manager_apply_key_label(uint8_t index, const char *text)
{
    const bool assigned = (text != NULL) && (text[0] != '\0');
    const bool is_last = (index == PAGE_MANAGER_BUTTON_COUNT - 1U);

    page_manager_set_text(s_key_labels[index], assigned ? text : "");
    lv_obj_set_style_bg_color(s_key_slots[index],
                              assigned ? (is_last ? KDL_COLOR_KEY_BG_LAST : KDL_COLOR_KEY_BG)
                                       : KDL_COLOR_KEY_BG_EMPTY,
                              0);
    lv_obj_set_style_text_color(s_key_labels[index],
                                assigned ? KDL_COLOR_INK : KDL_COLOR_KEY_INK_EMPTY, 0);
    lv_obj_set_style_text_color(s_key_ticks[index],
                                assigned ? KDL_COLOR_KEY_TICK : KDL_COLOR_KEY_TICK_OFF, 0);
}

static const char *page_manager_effective_label(uint8_t index)
{
    if (s_label_override[index] != NULL)
    {
        return s_label_override[index];
    }
    return (s_current_page != NULL) ? s_current_page->button_labels[index] : "";
}

static void page_manager_refresh_clock(void)
{
    if (!timekeeping_is_valid())
    {
        page_manager_set_text(s_clock_label, "--:--");
        page_manager_set_text(s_date_label, "--.--.--");
        return;
    }

    struct tm now;
    timekeeping_get(&now);

    /* Every field is reduced modulo 100 before printing: struct tm carries
     * plain ints, so without an explicit bound the compiler must assume a
     * 10-digit field and rejects the fixed-size buffers. */
    char clock_text[PAGE_MANAGER_CLOCK_TEXT_LEN];
    snprintf(clock_text, sizeof(clock_text), "%02u:%02u",
             (unsigned)now.tm_hour % 100U, (unsigned)now.tm_min % 100U);
    page_manager_set_text(s_clock_label, clock_text);

    char date_text[PAGE_MANAGER_DATE_TEXT_LEN];
    snprintf(date_text, sizeof(date_text), "%02u.%02u.%02u",
             (unsigned)now.tm_mday % 100U, (unsigned)(now.tm_mon + 1) % 100U,
             (unsigned)(now.tm_year + 1900) % 100U);
    page_manager_set_text(s_date_label, date_text);
}

static void page_manager_refresh_recording(void)
{
    const bool usb_active = gui_action_is_usb_msc_active();
    const bool recording = logger_service_is_active();

    if (recording && !s_was_recording)
    {
        s_session_start_us = esp_timer_get_time();
    }
    else if (!recording && s_was_recording)
    {
        s_session_frozen_us = esp_timer_get_time() - s_session_start_us;
    }
    s_was_recording = recording;

    const int64_t elapsed_us = recording ? (esp_timer_get_time() - s_session_start_us)
                                         : s_session_frozen_us;
    const uint32_t elapsed_s = (uint32_t)(elapsed_us / 1000000);

    /* Saturate rather than wrap: a session past 99 h is not expected, and a
     * pinned 99:59:59 is less misleading than a counter that rolls to zero.
     * The clamp also gives the compiler the two-digit bound it needs. */
    unsigned hours = (unsigned)(elapsed_s / 3600U);
    if (hours > 99U)
    {
        hours = 99U;
    }

    char elapsed_text[PAGE_MANAGER_ELAPSED_TEXT_LEN];
    snprintf(elapsed_text, sizeof(elapsed_text), "%02u:%02u:%02u", hours,
             (unsigned)((elapsed_s / 60U) % 60U), (unsigned)(elapsed_s % 60U));
    page_manager_set_text(s_elapsed_label, elapsed_text);

    /* A logging failure outranks the recording state: a session that looks
     * like it is recording while nothing reaches the file is the worst case,
     * and the operator has no other way to notice it. */
    const bool log_fault = logger_service_has_fault();
    const char *rec_text = recording ? KDL_TXT_STATUS_REC : KDL_TXT_STATUS_STOPPED;
    page_manager_set_text(s_rec_label, log_fault ? KDL_TXT_STATUS_LOG_ERROR : rec_text);

    lv_color_t dot_color = KDL_COLOR_REC_STOPPED;
    if (log_fault)
    {
        s_rec_blink_on = false;
        dot_color = KDL_COLOR_HOT;
    }
    else if (recording && !usb_active)
    {
        s_rec_blink_on = !s_rec_blink_on;
        dot_color = s_rec_blink_on ? KDL_COLOR_REC_ON : KDL_COLOR_REC_OFF;
    }
    else
    {
        s_rec_blink_on = false;
    }
    lv_obj_set_style_bg_color(s_rec_dot, dot_color, 0);
}

void page_manager_init(lv_obj_t *screen)
{
    kdl_theme_init();
    kdl_theme_apply_screen(screen);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);

    page_manager_build_statusbar(screen);

    s_body = page_manager_create_box(screen, lv_pct(100), PAGE_MANAGER_BODY_H);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_ROW);

    s_content = page_manager_create_box(s_body, PAGE_MANAGER_CONTENT_W, lv_pct(100));

    page_manager_build_rail(s_body);

    s_current_page = NULL;
    s_session_start_us = esp_timer_get_time();
}

/**
 * Show or hide the chrome and resize the content box to match.
 *
 * Hidden objects are skipped by the flex layout, so the body has to be grown
 * to the full panel height as well: otherwise it would keep the 294 px it was
 * given and leave the status bar's 26 px as a dead strip at the bottom.
 */
static void page_manager_apply_chrome(bool fullscreen)
{
    if (fullscreen)
    {
        lv_obj_add_flag(s_statusbar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_rail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(s_body, DISPLAY_PANEL_VER_RES);
        lv_obj_set_size(s_content, DISPLAY_PANEL_HOR_RES, lv_pct(100));
    }
    else
    {
        lv_obj_remove_flag(s_statusbar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_rail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(s_body, PAGE_MANAGER_BODY_H);
        lv_obj_set_size(s_content, PAGE_MANAGER_CONTENT_W, lv_pct(100));
    }
}

void page_manager_switch_to(const gui_page_t *page)
{
    if (page == NULL || page == s_current_page)
    {
        return;
    }

    if (s_current_page != NULL && s_current_page->on_hide != NULL)
    {
        s_current_page->on_hide();
    }

    lv_obj_clean(s_content);
    /* A page configures the shared content box with local styles (layout,
     * padding, background); wipe them so the next page starts from the same
     * blank slate. remove_style_all() also drops the geometry, which is
     * itself a local style, so the box has to be re-established here. */
    lv_obj_remove_style_all(s_content);
    lv_obj_add_style(s_content, &kdl_style_panel, 0);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    page_manager_apply_chrome(page->fullscreen);

    for (uint8_t index = 0; index < PAGE_MANAGER_BUTTON_COUNT; ++index)
    {
        s_label_override[index] = NULL;
    }

    s_current_page = page;

    page_manager_set_text(s_title_label, page->title != NULL ? page->title : "");
    for (uint8_t index = 0; index < PAGE_MANAGER_BUTTON_COUNT; ++index)
    {
        page_manager_apply_key_label(index, page_manager_effective_label(index));
    }

    if (page->on_show != NULL)
    {
        page->on_show(s_content);
    }
}

void page_manager_tick(void)
{
    /* Nothing to refresh while the chrome is hidden, and writing into
     * invisible labels would still cost an invalidate. */
    if (s_current_page != NULL && !s_current_page->fullscreen)
    {
        page_manager_refresh_recording();
        page_manager_refresh_clock();
    }

    if (s_current_page != NULL && s_current_page->on_tick != NULL)
    {
        s_current_page->on_tick();
    }
}

void page_manager_dispatch_button(uint8_t button_index)
{
    if (button_index >= PAGE_MANAGER_BUTTON_COUNT)
    {
        return;
    }

    if (s_current_page != NULL && s_current_page->on_button != NULL)
    {
        s_current_page->on_button(button_index);
    }
}

void page_manager_set_title(const char *title)
{
    const char *effective = title;
    if (effective == NULL)
    {
        effective = (s_current_page != NULL && s_current_page->title != NULL)
                    ? s_current_page->title : "";
    }
    page_manager_set_text(s_title_label, effective);
}

void page_manager_set_button_label(uint8_t button_index, const char *label)
{
    if (button_index >= PAGE_MANAGER_BUTTON_COUNT)
    {
        return;
    }

    s_label_override[button_index] = label;
    page_manager_apply_key_label(button_index, page_manager_effective_label(button_index));
}
