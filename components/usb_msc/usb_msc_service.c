#include "usb_msc_service.h"

#include "esp_check.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "tusb.h"

#include "storage_manager.h"

static const char *TAG = "usb_msc";

/* A buffer larger than a WL sector makes esp_tinyusb pass multi-sector ranges
 * to wl_read()/wl_write(), which WL_Flash (IDF 6.1) maps wrongly when they
 * cross a 4 KB page: corrupted reads, silently lost writes. Safe only while
 * storage wraps those calls (wl_page_split.c); without the wrapper the buffer
 * must go back to one sector. See sdkconfig.defaults. */
_Static_assert(STORAGE_WL_SPLITS_PAGES || CONFIG_TINYUSB_MSC_BUFSIZE == CONFIG_WL_SECTOR_SIZE,
               "MSC buffer larger than a WL sector needs the wl_page_split wrapper");
_Static_assert(CONFIG_TINYUSB_MSC_BUFSIZE % CONFIG_WL_SECTOR_SIZE == 0,
               "MSC buffer must be a multiple of the WL sector size");

static tinyusb_msc_storage_handle_t s_storage_hdl;
static bool s_driver_installed;
/* The three flags below are written from the TinyUSB task (callbacks) and
 * read from the GUI task. */
static volatile bool s_usb_active;
/** A host has configured the device since the last connect. */
static volatile bool s_host_attached;
/** The host gave the volume back on its own: eject or unplug. */
static volatile bool s_host_released;

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
        /* usb_msc_service_stop() clears s_usb_active before remounting, so an
         * APP mount while still active can only come from esp_tinyusb itself:
         * the host sent an eject (SCSI START STOP UNIT) or was unplugged. */
        if (event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP && s_usb_active) {
            s_host_released = true;
            ESP_LOGI(TAG, "Host released the volume (eject or unplug)");
        } else if (event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB && !s_usb_active) {
            /* Must never happen while detached from the bus: it would pull the
             * filesystem from under an open log file. */
            ESP_LOGE(TAG, "Volume taken by the USB host outside USB mode");
        }
        break;
    case TINYUSB_MSC_EVENT_MOUNT_FAILED:
    case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
        ESP_LOGE(TAG, "Storage mount failed");
        break;
    default:
        break;
    }
}

static void usb_msc_device_event_cb(tinyusb_event_t *event, void *arg)
{
    (void)arg;

    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        s_host_attached = true;
        break;
    case TINYUSB_EVENT_DETACHED:
        s_host_attached = false;
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

    /* Set before anything can reach the host, so the mount callbacks see the
     * hand-over as requested rather than as a volume grab. */
    s_host_released = false;
    s_usb_active = true;

    if (!s_driver_installed) {
        tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
        tusb_cfg.descriptor.device = &s_device_descriptor;
        tusb_cfg.descriptor.full_speed_config = s_fs_configuration_desc;
        tusb_cfg.descriptor.string = s_string_desc_arr;
        tusb_cfg.descriptor.string_count = sizeof(s_string_desc_arr) / sizeof(s_string_desc_arr[0]);
        tusb_cfg.event_cb = usb_msc_device_event_cb;

        const esp_err_t install_err = tinyusb_driver_install(&tusb_cfg);
        if (install_err != ESP_OK) {
            s_usb_active = false;
            ESP_LOGE(TAG, "tinyusb install failed: %s", esp_err_to_name(install_err));
            return install_err;
        }
        s_driver_installed = true;
    }

    const esp_err_t mount_err = tinyusb_msc_set_storage_mount_point(s_storage_hdl,
                                                                    TINYUSB_MSC_STORAGE_MOUNT_USB);
    if (mount_err != ESP_OK) {
        s_usb_active = false;
        ESP_LOGE(TAG, "failed to expose storage over USB: %s", esp_err_to_name(mount_err));
        return mount_err;
    }

    /* Re-attach after a previous usb_msc_service_stop(); a no-op on the first
     * start, where the driver install already connected. */
    (void)tud_connect();
    ESP_LOGI(TAG, "USB MSC active");
    return ESP_OK;
}

esp_err_t usb_msc_service_stop(void)
{
    if (!s_usb_active) {
        return ESP_OK;
    }

    /* Detach from the bus before taking the volume back. Merely remounting to
     * the application would leave the device enumerated: the host could still
     * flush stale cached sectors, and any re-enumeration (bus reset, wake from
     * sleep) would make esp_tinyusb's auto-mount pull the filesystem from
     * under the logger -- the zero-byte log files seen after skipping the
     * eject on the computer. To the host this looks like the cable being
     * pulled. */
    s_usb_active = false;
    (void)tud_disconnect();
    s_host_attached = false;
    s_host_released = false;

    ESP_RETURN_ON_ERROR(tinyusb_msc_set_storage_mount_point(s_storage_hdl, TINYUSB_MSC_STORAGE_MOUNT_APP),
                        TAG,
                        "failed to restore storage to application");
    ESP_LOGI(TAG, "USB MSC inactive");
    return ESP_OK;
}

bool usb_msc_service_is_active(void)
{
    return s_usb_active;
}

bool usb_msc_service_host_holds_volume(void)
{
    return s_usb_active && s_host_attached && !s_host_released;
}

bool usb_msc_service_host_released(void)
{
    return s_usb_active && s_host_released;
}
