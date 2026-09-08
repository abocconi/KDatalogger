#include "pages.h"

#include "kdl_splash.h"

#define PAGE_SPLASH_DURATION_MS 2000

static lv_timer_t *s_timer;

static void splash_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_timer = NULL;
    /* Straight into the clock prompt: there is no RTC, so every power-up
     * starts with an unset clock. The mask times out on its own, so this
     * cannot stall an unattended restart. */
    page_datetime_configure(&page_main, true);
    page_manager_switch_to(&page_datetime);
}

static void on_show(lv_obj_t *content)
{
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *image = lv_image_create(content);
    lv_image_set_src(image, &kdl_splash_image);
    lv_obj_set_pos(image, 0, 0);

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
    .title = "",
    .fullscreen = true,
    .button_labels = {"", "", "", "", ""},
    .on_show = on_show,
    .on_hide = on_hide,
    .on_tick = NULL,
    .on_button = NULL,
};
