#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "data_model.h"

esp_err_t acquisition_service_init(void);
esp_err_t acquisition_service_start(void);
esp_err_t acquisition_service_stop(void);
bool acquisition_service_is_active(void);

/**
 * @brief Copy the most recently acquired sensor sample.
 *
 * Thread-safe; intended for consumers (e.g. the GUI) reading data produced by
 * the acquisition task. Times out after a short delay instead of blocking the
 * caller indefinitely if the acquisition task holds the lock.
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if the sample could not be
 *         locked in time, ESP_ERR_INVALID_STATE if not initialized.
 */
esp_err_t acquisition_service_get_latest_sample(kdl_sensor_sample_t *out);
