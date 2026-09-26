#pragma once

#include "esp_err.h"

typedef enum {
    APP_MODE_NORMAL = 0,
    APP_MODE_USB_MSC,
    APP_MODE_FW_UPDATE,
    APP_MODE_ERROR,
} app_mode_t;

esp_err_t app_controller_init(void);
esp_err_t app_controller_start(void);
app_mode_t app_controller_get_mode(void);
esp_err_t app_controller_enter_usb_msc_mode(void);
esp_err_t app_controller_exit_usb_msc_mode(void);

/**
 * @brief Controller loop, for the task that ran app_controller_start(). Never returns.
 *
 * Confirms a freshly updated image once it has proved itself, and installs a
 * firmware update found on the volume at boot or after USB mode -- only while
 * nothing is being recorded.
 */
void app_controller_run(void);
