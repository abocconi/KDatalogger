#pragma once

#include <stdbool.h>

#include "data_model.h"
#include "esp_err.h"

#define LOGGER_DIRECTORY_PATH "/data/logs"

esp_err_t logger_service_init(void);
esp_err_t logger_service_start(void);
esp_err_t logger_service_stop(void);
esp_err_t logger_service_flush(void);
esp_err_t logger_service_log_event(const char *event, const char *detail);
esp_err_t logger_service_log_sample(const kdl_sensor_sample_t *sample);
bool logger_service_is_active(void);
const char *logger_service_get_current_path(void);
