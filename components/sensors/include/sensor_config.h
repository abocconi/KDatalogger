#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "sensor_config_parse.h"

/**
 * @file sensor_config.h
 * @brief Sensor configuration read from a text file on the USB volume.
 *
 * The pressure sensors and the tachometer are chosen by the user, so their
 * scaling cannot be compiled in. The file is edited on a PC through the USB
 * volume and read back at boot and on every return from USB mode; a missing
 * file is recreated from the defaults, so a copy to edit is always there.
 */

/** File name at the root of the data volume. */
#define SENSOR_CONFIG_FILE_NAME "sensori.ini"

typedef enum {
    SENSOR_CONFIG_SOURCE_DEFAULTS = 0, /**< Nothing read yet, or the volume was unreadable */
    SENSOR_CONFIG_SOURCE_FILE,         /**< Read from the file                             */
    SENSOR_CONFIG_SOURCE_CREATED,      /**< File was missing and has been written          */
} sensor_config_source_t;

/** @brief Outcome of the last load, for the settings page. */
typedef struct {
    sensor_config_source_t source;
    uint32_t error_count;
    uint32_t first_error_line; /**< 0 = no error */
} sensor_config_status_t;

/** @brief Create the lock and apply the defaults. Call once, before any other function. */
esp_err_t sensor_config_init(void);

/**
 * @brief (Re)load the file; write the default one if it is missing.
 *
 * The data volume must be mounted and owned by the firmware. Errors in the
 * file are not a failure of this call: the offending sections are disabled
 * and counted in the status. Only an unreadable volume is.
 */
esp_err_t sensor_config_load(void);

/** @brief Copy the configuration in effect. Thread-safe. */
void sensor_config_get(sensor_config_t *out);

/** @brief Copy the outcome of the last load. Thread-safe. */
void sensor_config_get_status(sensor_config_status_t *out);
