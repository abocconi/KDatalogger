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

/**
 * @brief Per-channel extremes seen since the last reset.
 *
 * Tracked here rather than in the GUI so the figures survive page switches
 * and describe the whole session instead of the time one page happened to be
 * on screen. Only samples the MAX31855 reported as valid are folded in, so a
 * disconnected probe cannot drag a minimum to a fault value.
 */
typedef struct {
    float thermocouple_min_c[DATA_MODEL_THERMOCOUPLE_COUNT];
    float thermocouple_max_c[DATA_MODEL_THERMOCOUPLE_COUNT];
    uint16_t valid_mask; /**< Channels that have contributed at least one sample */
    float rpm_max;       /**< Highest engine speed; meaningful only if rpm_max_valid */
    bool rpm_max_valid;
} kdl_sensor_extremes_t;

/**
 * @brief Copy the per-channel extremes. Thread-safe, same locking rules as
 *        acquisition_service_get_latest_sample().
 */
esp_err_t acquisition_service_get_extremes(kdl_sensor_extremes_t *out);

/**
 * @brief Discard the accumulated extremes.
 *
 * Called automatically when a recording session starts, so the values shown
 * on screen always describe the current run; exposed for a manual reset from
 * the UI.
 */
esp_err_t acquisition_service_reset_extremes(void);

/** @brief Outcome of acquisition_service_zero_pressures(). */
typedef struct {
    uint8_t enabled_mask; /**< Pressure inputs with a sensor configured */
    uint8_t zeroed_mask;  /**< Inputs whose zero was taken now          */
} acquisition_zero_result_t;

/**
 * @brief Take the current reading of every configured pressure input as 0 bar.
 *
 * Meant for sensors vented to the atmosphere, engine off. An input is left
 * untouched when its reading is a fault or too far from 0 bar to be a
 * sensor offset (see pressure_scaling_compute_zero()), so a zero pressed
 * with pressure in the line cannot be stored. The new offsets are persisted.
 *
 * @return ESP_ERR_INVALID_STATE if no sample is available (acquisition
 *         stopped), otherwise the result of persisting the offsets.
 */
esp_err_t acquisition_service_zero_pressures(acquisition_zero_result_t *out);
