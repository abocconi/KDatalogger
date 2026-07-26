#pragma once

#include "esp_err.h"

/**
 * @brief Bring up the display and build the page tree, showing the splash page.
 *
 * Requires acquisition_service_init() to already have run: the GUI reads
 * sensor data via acquisition_service_get_latest_sample().
 */
esp_err_t gui_service_init(void);

/** @brief Start button polling and periodic data refresh. */
esp_err_t gui_service_start(void);

/** @brief Stop button polling and periodic data refresh. */
esp_err_t gui_service_stop(void);
