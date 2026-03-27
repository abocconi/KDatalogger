#include "usb_msc_service.h"

#include "esp_check.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

#include "storage_manager.h"

static const char *TAG = "usb_msc";

static tinyusb_msc_storage_handle_t s_storage_hdl;
static bool s_driver_installed;
static bool s_usb_active;

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL,
};

enum {
    EDPT_MSC_OUT = 0x01,
    EDPT_MSC_IN = 0x81,
};

static const tusb_desc_device_t s_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const uint8_t s_fs_configuration_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, 64),
};

static const char *s_string_desc_arr[] = {
    (const char[]) {0x09, 0x04},
    "KDatalogger",
    "KDatalogger MSC",
    "000001",
    "Data",
};

static void storage_mount_changed_cb(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg)
{
    (void)handle;
    (void)arg;

    switch (event->id) {
    case TINYUSB_MSC_EVENT_MOUNT_START:
        ESP_LOGI(TAG, "Changing storage owner");
        break;
    case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
        ESP_LOGI(TAG, "Storage mounted to %s",
                 event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB ? "USB host" : "application");
        break;
    case TINYUSB_MSC_EVENT_MOUNT_FAILED:
    case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
        ESP_LOGE(TAG, "Storage mount failed");
        break;
    default:
        break;
    }
}

esp_err_t usb_msc_service_init(void)
{
    if (s_storage_hdl != NULL) {
        return ESP_OK;
    }

    const tinyusb_msc_storage_config_t storage_cfg = {
        .medium.wl_handle = storage_manager_get_wl_handle(),
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,
        .fat_fs = {
            .base_path = STORAGE_MOUNT_PATH,
            .config = {
                .format_if_mount_failed = true,
                .max_files = 8,
                .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
                .disk_status_check_enable = false,
                .use_one_fat = false,
            },
            .do_not_format = false,
            .format_flags = 0,
        },
    };

    ESP_RETURN_ON_ERROR(tinyusb_msc_new_storage_spiflash(&storage_cfg, &s_storage_hdl),
                        TAG,
                        "failed to create SPI flash MSC storage");
    ESP_RETURN_ON_ERROR(tinyusb_msc_set_storage_callback(storage_mount_changed_cb, NULL),
                        TAG,
                        "failed to register storage callback");
    return ESP_OK;
}

esp_err_t usb_msc_service_start(void)
{
    if (s_usb_active) {
        return ESP_OK;
    }

    if (!s_driver_installed) {
        tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
        tusb_cfg.descriptor.device = &s_device_descriptor;
        tusb_cfg.descriptor.full_speed_config = s_fs_configuration_desc;
        tusb_cfg.descriptor.string = s_string_desc_arr;
        tusb_cfg.descriptor.string_count = sizeof(s_string_desc_arr) / sizeof(s_string_desc_arr[0]);

        ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG, "tinyusb install failed");
        s_driver_installed = true;
    }

    ESP_RETURN_ON_ERROR(tinyusb_msc_set_storage_mount_point(s_storage_hdl, TINYUSB_MSC_STORAGE_MOUNT_USB),
                        TAG,
                        "failed to expose storage over USB");
    s_usb_active = true;
    ESP_LOGI(TAG, "USB MSC active");
    return ESP_OK;
}

esp_err_t usb_msc_service_stop(void)
{
    if (!s_usb_active) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(tinyusb_msc_set_storage_mount_point(s_storage_hdl, TINYUSB_MSC_STORAGE_MOUNT_APP),
                        TAG,
                        "failed to restore storage to application");
    s_usb_active = false;
    ESP_LOGI(TAG, "USB MSC inactive");
    return ESP_OK;
}

bool usb_msc_service_is_active(void)
{
    return s_usb_active;
}
