#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "data_model.h"
#include "esp_err.h"

#define LOGGER_DIRECTORY_PATH "/data/logs"

/**
 * @file logger_service.h
 * @brief CSV logger running in its own task, fed through a queue.
 *
 * File I/O never runs in the caller: an fsync on FAT + wear levelling stalls
 * for hundreds of ms, longer than the fastest acquisition period. The
 * request/submit calls only queue a message and return at once. They are
 * meant for a single producer task (acquisition).
 */

/** @brief Create the queue and start the logger task. */
esp_err_t logger_service_init(void);

/**
 * @brief Ask for a new session (a new file). Non-blocking.
 *
 * The file is opened by the logger task; if that fails it is retried once a
 * second while the session stays requested, and the fault is reported by
 * @ref logger_service_has_fault.
 *
 * @return ESP_ERR_TIMEOUT if the queue is full: the request was not sent,
 *         retry later.
 */
esp_err_t logger_service_request_start(void);

/**
 * @brief Ask to close the session once the samples already queued are written.
 *        Non-blocking.
 *
 * @return ESP_ERR_TIMEOUT if the queue is full: the request was not sent,
 *         retry later.
 */
esp_err_t logger_service_request_stop(void);

/**
 * @brief Close the session and wait until the file is flushed and closed.
 *
 * Writes out every sample queued before the call. Only call it once the
 * producer has stopped submitting (acquisition stopped): used before handing
 * the volume to USB or to a firmware update.
 *
 * @return ESP_OK, the final flush/close error, or ESP_ERR_TIMEOUT if the task
 *         did not finish in time -- the file may then still be open.
 */
esp_err_t logger_service_stop(void);

/**
 * @brief Queue one sample row for the current session. Non-blocking.
 *
 * The file is a CSV for an Italian-locale Excel (';' separator, ',' decimal
 * mark, UTF-8 BOM) with columns Data, Ora, Tempo [s], then one column per
 * thermocouple and analog input, headed "<name> [<unit>]" from
 * data_model_channels.h. Data/Ora come from the wall clock at the time of
 * this call and are empty while it is not set; Tempo counts from the first
 * sample of the session. Channels flagged invalid in the sample are written
 * as empty cells.
 *
 * @return ESP_ERR_TIMEOUT if the queue is full: the sample is dropped and
 *         counted, and the logger reports a fault. A sample queued with no
 *         session open or requested is discarded by the logger task.
 */
esp_err_t logger_service_submit_sample(const kdl_sensor_sample_t *sample);

/**
 * @brief Whether a session is requested or its file is still open.
 *
 * True from @ref logger_service_request_start until the file is closed, so a
 * session still draining its queue counts as active.
 */
bool logger_service_is_active(void);
const char *logger_service_get_current_path(void);

/**
 * @brief Number of sample rows written to the current log file.
 *
 * Reset when a session starts, so it counts the run rather than the lifetime
 * of the device. Event rows are not counted.
 */
uint32_t logger_service_get_sample_count(void);

/**
 * @brief Whether the last session failed to start or lost data.
 *
 * Set when the log file cannot be opened, a write/flush fails or samples are
 * dropped on a full queue; cleared only by the next session, so a failure
 * stays visible after recording has stopped. Safe to call from any task.
 */
bool logger_service_has_fault(void);
