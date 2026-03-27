#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t usb_msc_service_init(void);
esp_err_t usb_msc_service_start(void);
esp_err_t usb_msc_service_stop(void);
bool usb_msc_service_is_active(void);
