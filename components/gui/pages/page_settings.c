#include "pages.h"

static void on_show(lv_obj_t *content)
{
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, "Settings (TBD)");
    lv_obj_center(label);
}

static void on_button(uint8_t button_index)
{
    if (button_index == 0) {
        page_manager_switch_to(&page_main);
    }
}

const gui_page_t page_settings = {
    .name = "settings",
    .button_labels = {"Back", "", "", "", ""},
    .on_show = on_show,
    .on_hide = NULL,
    .on_tick = NULL,
    .on_button = on_button,
};
