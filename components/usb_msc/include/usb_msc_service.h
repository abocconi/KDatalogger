#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t usb_msc_service_init(void);
esp_err_t usb_msc_service_start(void);
esp_err_t usb_msc_service_stop(void);
bool usb_msc_service_is_active(void);

/**
 * @brief Whether a computer currently has the volume mounted.
 *
 * True in USB mode once a host has configured the device, until it ejects
 * the drive. Leaving USB mode in that state skips the eject on the computer.
 * A cable pull is not always seen (no VBUS sensing on this board), so the
 * caller must still offer a way out.
 */
bool usb_msc_service_host_holds_volume(void);

/**
 * @brief Whether the host gave the volume back on its own (eject or unplug).
 *
 * The filesystem is already back with the firmware when this turns true;
 * the caller should leave USB mode, which also detaches from the bus.
 */
bool usb_msc_service_host_released(void);
