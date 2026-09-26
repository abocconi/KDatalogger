#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "wear_levelling.h"

#define STORAGE_MOUNT_PATH "/data"

/**
 * wl_read()/wl_write() are wrapped at link time to split requests at 4 KB
 * page boundaries (wl_page_split.c, IDF 6.1 wear levelling bug), so callers
 * may pass multi-sector ranges at any sector-aligned address. Remove together
 * with the wrapper.
 */
#define STORAGE_WL_SPLITS_PAGES 1

typedef enum {
    STORAGE_OWNER_NONE = 0,
    STORAGE_OWNER_FIRMWARE,
    STORAGE_OWNER_USB_HOST,
} storage_owner_t;

esp_err_t storage_manager_init(void);
esp_err_t storage_manager_prepare_for_usb_export(void);
esp_err_t storage_manager_resume_firmware_access(void);
storage_owner_t storage_manager_get_owner(void);
wl_handle_t storage_manager_get_wl_handle(void);

/**
 * @brief Total and free bytes on the mounted data volume.
 *
 * Only valid while the firmware owns the volume: once it has been handed to
 * the USB host the filesystem is unmounted and this fails. Callers that need
 * the figure on the USB screen must sample it before the handover.
 */
esp_err_t storage_manager_get_usage(uint64_t *total_bytes, uint64_t *free_bytes);
