#pragma once

#include <stdbool.h>

#include "esp_err.h"

/** @brief Toggle CSV logging on/off, mirroring logger_service_is_active(). */
void gui_action_toggle_recording(void);

typedef esp_err_t (*gui_actions_usb_msc_transition_fn_t)(void);
typedef bool (*gui_actions_usb_msc_is_active_fn_t)(void);

/**
 * @brief Wire the USB MSC mode transition functions used by gui_action_toggle_usb_msc().
 *
 * Injected by app_core at startup so this component has no compile-time dependency on
 * app_core (which already depends on gui) -- avoids a circular component dependency.
 */
void gui_actions_set_usb_msc_hooks(gui_actions_usb_msc_transition_fn_t enter_fn,
                                   gui_actions_usb_msc_transition_fn_t exit_fn,
                                   gui_actions_usb_msc_is_active_fn_t is_active_fn);

/** @brief Toggle USB MSC export mode on/off via the hooks set by gui_actions_set_usb_msc_hooks(). */
void gui_action_toggle_usb_msc(void);

/** @brief Query USB MSC export mode state via the hooks set by gui_actions_set_usb_msc_hooks().
 *         Returns false if the hooks have not been set yet. */
bool gui_action_is_usb_msc_active(void);
