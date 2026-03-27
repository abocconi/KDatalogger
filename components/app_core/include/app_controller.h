#pragma once

#include "esp_err.h"

typedef enum {
    APP_MODE_NORMAL = 0,
    APP_MODE_USB_MSC,
    APP_MODE_ERROR,
} app_mode_t;

esp_err_t app_controller_init(void);
esp_err_t app_controller_start(void);
app_mode_t app_controller_get_mode(void);
esp_err_t app_controller_enter_usb_msc_mode(void);
esp_err_t app_controller_exit_usb_msc_mode(void);
