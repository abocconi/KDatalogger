#include "pages.h"

#define PAGE_SPLASH_DURATION_MS 2000

static lv_timer_t *s_timer;

static void splash_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_timer = NULL;
    page_manager_switch_to(&page_main);
}

static void on_show(lv_obj_t *content)
{
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, "KDatalogger");
    lv_obj_center(label);

    s_timer = lv_timer_create(splash_timer_cb, PAGE_SPLASH_DURATION_MS, NULL);
    lv_timer_set_repeat_count(s_timer, 1);
}

static void on_hide(void)
{
    if (s_timer != NULL) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
}

const gui_page_t page_splash = {
    .name = "splash",
    .button_labels = {"", "", "", "", ""},
    .on_show = on_show,
    .on_hide = on_hide,
    .on_tick = NULL,
    .on_button = NULL,
};
