#include "gui_actions.h"

#include <stddef.h>

#include "esp_log.h"

#include "logger_service.h"

static const char *TAG = "gui_actions";

static gui_actions_usb_msc_transition_fn_t s_usb_msc_enter_fn;
static gui_actions_usb_msc_transition_fn_t s_usb_msc_exit_fn;
static gui_actions_usb_msc_is_active_fn_t s_usb_msc_is_active_fn;

void gui_actions_set_usb_msc_hooks(gui_actions_usb_msc_transition_fn_t enter_fn,
                                   gui_actions_usb_msc_transition_fn_t exit_fn,
                                   gui_actions_usb_msc_is_active_fn_t is_active_fn)
{
    s_usb_msc_enter_fn = enter_fn;
    s_usb_msc_exit_fn = exit_fn;
    s_usb_msc_is_active_fn = is_active_fn;
}

void gui_action_toggle_usb_msc(void)
{
    if (s_usb_msc_enter_fn == NULL || s_usb_msc_exit_fn == NULL || s_usb_msc_is_active_fn == NULL) {
        ESP_LOGW(TAG, "usb msc hooks not set");
        return;
    }

    bool usb_active = s_usb_msc_is_active_fn();
    if (!usb_active && logger_service_is_active()) {
        /* AI1 drives recording now: USB export is only allowed once it has
         * stopped, so the two can never fight over the storage/filesystem. */
        ESP_LOGW(TAG, "usb msc request ignored: recording is active (AI1)");
        return;
    }

    esp_err_t err = usb_active ? s_usb_msc_exit_fn() : s_usb_msc_enter_fn();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "usb msc toggle failed: %s", esp_err_to_name(err));
    }
}

bool gui_action_is_usb_msc_active(void)
{
    return s_usb_msc_is_active_fn != NULL && s_usb_msc_is_active_fn();
}
