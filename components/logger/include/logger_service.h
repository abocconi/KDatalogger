#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "data_model.h"
#include "esp_err.h"

#define LOGGER_DIRECTORY_PATH "/data/logs"

esp_err_t logger_service_init(void);
esp_err_t logger_service_start(void);
esp_err_t logger_service_stop(void);
esp_err_t logger_service_flush(void);

/**
 * @brief Append one sample row to the current log file.
 *
 * The file is a CSV for an Italian-locale Excel (';' separator, ',' decimal
 * mark, UTF-8 BOM) with columns Data, Ora, Tempo [s], then one column per
 * thermocouple and analog input, headed "<name> [<unit>]" from
 * data_model_channels.h. Data/Ora are empty while the wall clock is not set; Tempo
 * counts from the first sample of the session. Channels flagged invalid in
 * the sample are written as empty cells.
 */
esp_err_t logger_service_log_sample(const kdl_sensor_sample_t *sample);
bool logger_service_is_active(void);
const char *logger_service_get_current_path(void);

/**
 * @brief Number of sample rows written to the current log file.
 *
 * Reset when a session starts, so it counts the run rather than the lifetime
 * of the device. Event rows are not counted.
 */
uint32_t logger_service_get_sample_count(void);
