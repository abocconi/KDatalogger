#include "gui_actions.h"

#include <stddef.h>

#include "esp_log.h"

#include "logger_service.h"

static const char *TAG = "gui_actions";

static gui_actions_usb_msc_transition_fn_t s_usb_msc_enter_fn;
static gui_actions_usb_msc_transition_fn_t s_usb_msc_exit_fn;
static gui_actions_usb_msc_is_active_fn_t s_usb_msc_is_active_fn;

void gui_action_toggle_recording(void)
{
    esp_err_t err;

    if (logger_service_is_active()) {
        err = logger_service_stop();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "logger stop failed: %s", esp_err_to_name(err));
        }
    } else {
        err = logger_service_start();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "logger start failed: %s", esp_err_to_name(err));
        }
    }
}

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

    esp_err_t err = s_usb_msc_is_active_fn() ? s_usb_msc_exit_fn() : s_usb_msc_enter_fn();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "usb msc toggle failed: %s", esp_err_to_name(err));
    }
}

bool gui_action_is_usb_msc_active(void)
{
    return s_usb_msc_is_active_fn != NULL && s_usb_msc_is_active_fn();
}
