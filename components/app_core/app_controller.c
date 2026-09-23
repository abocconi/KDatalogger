#include "app_controller.h"

#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "timekeeping.h"

#include "acquisition_service.h"
#include "gui_actions.h"
#include "gui_service.h"
#include "logger_service.h"
#include "settings_service.h"
#include "storage_manager.h"
#include "usb_msc_service.h"

/* NOTE: software-side flicker (partial-buffer SPI tearing) has been fixed.
 * Re-enabled to verify thermocouple readings. If electrical interference
 * between SPI2 (MAX31855) and SPI3 (display) reappears it is a hardware
 * issue (decoupling/grounding/layout) unrelated to the software flicker fix. */
#define KDL_ACQUISITION_ENABLED 1

static const char *TAG = "app_controller";
static app_mode_t s_app_mode = APP_MODE_NORMAL;

static bool app_controller_is_usb_msc_mode(void)
{
    return s_app_mode == APP_MODE_USB_MSC;
}

static esp_err_t app_controller_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* Partition layout/format changed (or is worn out): the emulated
         * EEPROM contents can't be trusted, so wipe and reinit rather than
         * fail boot over it -- settings will just fall back to defaults. */
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t app_controller_init(void)
{
    ESP_RETURN_ON_ERROR(app_controller_init_nvs(), TAG, "nvs init failed");
    ESP_RETURN_ON_ERROR(settings_service_init(), TAG, "settings init failed");
    ESP_RETURN_ON_ERROR(timekeeping_init(), TAG, "timekeeping init failed");
    ESP_RETURN_ON_ERROR(storage_manager_init(), TAG, "storage init failed");
    ESP_RETURN_ON_ERROR(usb_msc_service_init(), TAG, "usb msc init failed");
    ESP_RETURN_ON_ERROR(logger_service_init(), TAG, "logger init failed");
    ESP_RETURN_ON_ERROR(acquisition_service_init(), TAG, "acquisition init failed");
    ESP_RETURN_ON_ERROR(gui_service_init(), TAG, "gui init failed");
    gui_actions_set_usb_msc_hooks(app_controller_enter_usb_msc_mode,
                                  app_controller_exit_usb_msc_mode,
                                  app_controller_is_usb_msc_mode);
    s_app_mode = APP_MODE_NORMAL;
    return ESP_OK;
}

esp_err_t app_controller_start(void)
{
    /* Recording is no longer forced on at boot: acquisition_service reads
     * AI1 (record-enable, GPIO13) every cycle and starts/stops the logger
     * to follow it. */
#if KDL_ACQUISITION_ENABLED
    ESP_RETURN_ON_ERROR(acquisition_service_start(), TAG, "acquisition start failed");
#else
    ESP_LOGW(TAG, "acquisition disabled (KDL_ACQUISITION_ENABLED=0): display/SPI2 interference under investigation");
#endif
    ESP_RETURN_ON_ERROR(gui_service_start(), TAG, "gui start failed");
    ESP_LOGI(TAG, "KDatalogger services ready in normal mode");
    return ESP_OK;
}

app_mode_t app_controller_get_mode(void)
{
    return s_app_mode;
}

esp_err_t app_controller_enter_usb_msc_mode(void)
{
    if (s_app_mode == APP_MODE_USB_MSC) {
        return ESP_OK;
    }

    if (acquisition_service_is_active()) {
        ESP_RETURN_ON_ERROR(acquisition_service_stop(), TAG, "acquisition stop failed");
    }

    if (logger_service_is_active()) {
        /* The file is closed even when the final flush fails, so the handoff
         * is still safe -- aborting here would only lock the operator out of
         * the logs that did make it to flash. */
        const esp_err_t stop_err = logger_service_stop();
        if (stop_err != ESP_OK) {
            ESP_LOGW(TAG, "logger stop failed (%s), continuing USB handoff",
                     esp_err_to_name(stop_err));
        }
    }

    ESP_RETURN_ON_ERROR(storage_manager_prepare_for_usb_export(), TAG, "storage handoff failed");
    ESP_RETURN_ON_ERROR(usb_msc_service_start(), TAG, "usb start failed");
    s_app_mode = APP_MODE_USB_MSC;
    ESP_LOGI(TAG, "Entered USB MSC mode");
    return ESP_OK;
}

esp_err_t app_controller_exit_usb_msc_mode(void)
{
    if (s_app_mode != APP_MODE_USB_MSC) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(usb_msc_service_stop(), TAG, "usb stop failed");
    ESP_RETURN_ON_ERROR(storage_manager_resume_firmware_access(), TAG, "storage resume failed");
    /* Recording resumes on its own, once acquisition_service samples AI1
     * again -- no explicit logger_service_start() here. */
#if KDL_ACQUISITION_ENABLED
    ESP_RETURN_ON_ERROR(acquisition_service_start(), TAG, "acquisition restart failed");
#endif
    s_app_mode = APP_MODE_NORMAL;
    ESP_LOGI(TAG, "Returned to normal mode");
    return ESP_OK;
}
