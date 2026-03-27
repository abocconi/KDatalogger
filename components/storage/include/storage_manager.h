#pragma once

#include "esp_err.h"
#include "wear_levelling.h"

#define STORAGE_MOUNT_PATH "/data"

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
