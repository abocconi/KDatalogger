#include "pages.h"

#include <stdio.h>

#include "esp_app_desc.h"
#include "kdl_splash.h"
#include "kdl_text.h"
#include "kdl_theme.h"

#define PAGE_SPLASH_DURATION_MS 2000
/** Version label inset from the bottom-right corner, in the dark band below
 *  the "MEASURE ANALYZE IMPROVE" rule of the artwork. */
#define PAGE_SPLASH_VERSION_INSET_X (-8)
#define PAGE_SPLASH_VERSION_INSET_Y (-4)
#define PAGE_SPLASH_VERSION_LEN 40

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

    /* Shown at every boot, so right after an update the new version is on
     * screen at once -- this is the confirmation that it took. */
    char text[PAGE_SPLASH_VERSION_LEN];
    snprintf(text, sizeof(text), KDL_TXT_SPLASH_VERSION_FMT, esp_app_get_description()->version);
    lv_obj_t *version = lv_label_create(content);
    lv_obj_set_style_text_font(version, KDL_FONT_BODY, 0);
    lv_obj_set_style_text_color(version, KDL_COLOR_STATUS_CAPTION, 0);
    lv_label_set_text(version, text);
    lv_obj_align(version, LV_ALIGN_BOTTOM_RIGHT, PAGE_SPLASH_VERSION_INSET_X,
                 PAGE_SPLASH_VERSION_INSET_Y);

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
