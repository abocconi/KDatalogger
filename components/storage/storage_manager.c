#include "storage_manager.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs_fat.h"

static const char *TAG = "storage";

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static storage_owner_t s_owner = STORAGE_OWNER_NONE;

esp_err_t storage_manager_init(void)
{
    if (s_wl_handle != WL_INVALID_HANDLE) {
        s_owner = STORAGE_OWNER_FIRMWARE;
        return ESP_OK;
    }

    const esp_partition_t *data_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_FAT,
        NULL);
    ESP_RETURN_ON_FALSE(data_partition != NULL, ESP_ERR_NOT_FOUND, TAG, "FAT partition not found");

    ESP_RETURN_ON_ERROR(wl_mount(data_partition, &s_wl_handle), TAG, "wl_mount failed");
    s_owner = STORAGE_OWNER_FIRMWARE;
    ESP_LOGI(TAG, "Wear levelling ready");
    return ESP_OK;
}

esp_err_t storage_manager_prepare_for_usb_export(void)
{
    s_owner = STORAGE_OWNER_USB_HOST;
    return ESP_OK;
}

esp_err_t storage_manager_resume_firmware_access(void)
{
    s_owner = STORAGE_OWNER_FIRMWARE;
    return ESP_OK;
}

storage_owner_t storage_manager_get_owner(void)
{
    return s_owner;
}

wl_handle_t storage_manager_get_wl_handle(void)
{
    return s_wl_handle;
}

esp_err_t storage_manager_get_usage(uint64_t *total_bytes, uint64_t *free_bytes)
{
    ESP_RETURN_ON_FALSE(total_bytes != NULL && free_bytes != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "null output");
    ESP_RETURN_ON_FALSE(storage_manager_get_owner() == STORAGE_OWNER_FIRMWARE,
                        ESP_ERR_INVALID_STATE, TAG, "volume not owned by firmware");

    return esp_vfs_fat_info(STORAGE_MOUNT_PATH, total_bytes, free_bytes);
}
