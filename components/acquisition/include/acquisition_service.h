#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t acquisition_service_init(void);
esp_err_t acquisition_service_start(void);
esp_err_t acquisition_service_stop(void);
bool acquisition_service_is_active(void);
