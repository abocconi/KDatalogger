#pragma once

#include "esp_err.h"

/**
 * @file log_viewer.h
 * @brief Keeps the offline log viewer page on the data volume.
 *
 * The firmware image embeds datalogger.html (built by tools/gen_viewer.py):
 * a self-contained page that opens the CSV logs in any browser, with no
 * network and nothing else to install. It is kept at the volume root, next
 * to the logs directory, so the operator finds it on the USB drive.
 *
 * Not thread-safe: call from a single task.
 */

#define LOG_VIEWER_FILE_NAME "datalogger.html"

/**
 * @brief Make sure the volume root holds the viewer page of this firmware.
 *
 * Compares the file on the volume with the embedded copy and rewrites it only
 * when missing or different, so the flash is written once per firmware update,
 * or after the operator deleted or changed the page. Blocking: a rewrite
 * erases a few dozen flash sectors (see the log for the actual time).
 *
 * @return ESP_OK if the file is current (rewritten or not),
 *         ESP_ERR_INVALID_STATE if the firmware does not own the volume,
 *         ESP_FAIL on an I/O error (a partial file is removed).
 */
esp_err_t log_viewer_install(void);
