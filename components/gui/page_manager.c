#include "page_manager.h"

#define PAGE_MANAGER_SIDEBAR_WIDTH 90

/*
 * Sidebar label layout — absolute pixel positions, independent of LVGL flex engine.
 *
 * The sidebar labels are positioned with lv_obj_set_pos() so that the visible
 * text centre sits at a known y coordinate regardless of LVGL theme padding or
 * flex gap defaults.  Three knobs to calibrate against physical hardware:
 *
 *   BUTTON_CELL_H  : height of each button slot in px.
 *                    (display_height − BUTTON_TOP_PAD) / PAGE_MANAGER_BUTTON_COUNT
 *   BUTTON_TOP_PAD : offset from the sidebar top to the first button slot.
 *                    Increase if all labels appear above the physical buttons.
 *   LABEL_PAD_TOP  : top padding inside each label to vertically centre the text.
 *                    ≈ (BUTTON_CELL_H − font_line_height) / 2
 *
 * Current defaults place text centres at y = 30, 90, 150, 210, 270 px.
 * Calibration: if all labels are N px above/below the buttons, adjust
 * BUTTON_TOP_PAD by N.  If spacing is wrong, adjust BUTTON_CELL_H.
 */
#define PAGE_MANAGER_BUTTON_TOP_PAD  54   /* px from sidebar top to first slot   */
#define PAGE_MANAGER_BUTTON_CELL_H   54   /* height of each button slot in px    */
#define PAGE_MANAGER_LABEL_PAD_TOP   18   /* top pad for text vertical centering */

static lv_obj_t *s_sidebar_labels[PAGE_MANAGER_BUTTON_COUNT];
static lv_obj_t *s_content;
static const gui_page_t *s_current_page;

void page_manager_init(lv_obj_t *screen)
{
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    s_content = lv_obj_create(screen);
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_height(s_content, lv_pct(100));
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sidebar = lv_obj_create(screen);
    lv_obj_set_size(sidebar, PAGE_MANAGER_SIDEBAR_WIDTH, lv_pct(100));
    lv_obj_set_style_pad_all(sidebar, 0, 0);
    lv_obj_set_style_border_width(sidebar, 0, 0);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);
    /* No flex on the sidebar: labels use lv_obj_set_pos() for absolute, known
     * pixel positions so the layout is independent of LVGL theme defaults. */

    for (uint8_t index = 0; index < PAGE_MANAGER_BUTTON_COUNT; ++index) {
        const int32_t slot_y = PAGE_MANAGER_BUTTON_TOP_PAD
                               + (int32_t)index * PAGE_MANAGER_BUTTON_CELL_H;

        s_sidebar_labels[index] = lv_label_create(sidebar);
        lv_obj_set_size(s_sidebar_labels[index], PAGE_MANAGER_SIDEBAR_WIDTH, PAGE_MANAGER_BUTTON_CELL_H);
        lv_obj_set_pos(s_sidebar_labels[index], 0, slot_y);
        lv_obj_set_style_pad_all(s_sidebar_labels[index], 0, 0);
        lv_obj_set_style_pad_top(s_sidebar_labels[index], PAGE_MANAGER_LABEL_PAD_TOP, 0);
        lv_obj_set_style_text_align(s_sidebar_labels[index], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(s_sidebar_labels[index], LV_LABEL_LONG_CLIP);
        lv_label_set_text(s_sidebar_labels[index], "");
    }

    s_current_page = NULL;
}

void page_manager_switch_to(const gui_page_t *page)
{
    if (page == NULL || page == s_current_page) {
        return;
    }

    if (s_current_page != NULL && s_current_page->on_hide != NULL) {
        s_current_page->on_hide();
    }

    lv_obj_clean(s_content);

    for (uint8_t index = 0; index < PAGE_MANAGER_BUTTON_COUNT; ++index) {
        const char *text = page->button_labels[index] != NULL ? page->button_labels[index] : "";
        lv_label_set_text(s_sidebar_labels[index], text);
    }

    s_current_page = page;

    if (page->on_show != NULL) {
        page->on_show(s_content);
    }
}

void page_manager_tick(void)
{
    if (s_current_page != NULL && s_current_page->on_tick != NULL) {
        s_current_page->on_tick();
    }
}

void page_manager_dispatch_button(uint8_t button_index)
{
    if (button_index >= PAGE_MANAGER_BUTTON_COUNT) {
        return;
    }

    if (s_current_page != NULL && s_current_page->on_button != NULL) {
        s_current_page->on_button(button_index);
    }
}
