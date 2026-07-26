#include "app_controller.h"

#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"

#include "acquisition_service.h"
#include "gui_actions.h"
#include "gui_service.h"
#include "logger_service.h"
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

esp_err_t app_controller_init(void)
{
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
    /* Logger failure is non-fatal: a full filesystem or SPIFFS object-count
     * limit should not prevent the device from booting.  The rotation logic
     * in logger_service_start() handles the normal case; this guard covers
     * any residual failure (e.g. the partition is genuinely full). */
    esp_err_t logger_err = logger_service_start();
    if (logger_err != ESP_OK) {
        ESP_LOGW(TAG, "logger start failed (%s) — running without logging. "
                      "Connect USB to delete old log files.",
                 esp_err_to_name(logger_err));
    } else {
        if (logger_service_log_event("system_start", "normal_mode") != ESP_OK) {
            ESP_LOGW(TAG, "startup log event failed");
        }
    }

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
        (void)logger_service_log_event("usb_msc_requested", "handoff_to_host");
        ESP_RETURN_ON_ERROR(logger_service_stop(), TAG, "logger stop failed");
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
    esp_err_t resume_logger_err = logger_service_start();
    if (resume_logger_err != ESP_OK) {
        ESP_LOGW(TAG, "logger restart failed after USB MSC (%s)", esp_err_to_name(resume_logger_err));
    } else {
        (void)logger_service_log_event("usb_msc_released", "returned_to_firmware");
    }
#if KDL_ACQUISITION_ENABLED
    ESP_RETURN_ON_ERROR(acquisition_service_start(), TAG, "acquisition restart failed");
#endif
    s_app_mode = APP_MODE_NORMAL;
    ESP_LOGI(TAG, "Returned to normal mode");
    return ESP_OK;
}
