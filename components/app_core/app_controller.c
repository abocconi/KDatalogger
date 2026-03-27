#include "app_controller.h"

#include "esp_check.h"
#include "esp_log.h"

#include "acquisition_service.h"
#include "logger_service.h"
#include "storage_manager.h"
#include "usb_msc_service.h"

static const char *TAG = "app_controller";
static app_mode_t s_app_mode = APP_MODE_NORMAL;

esp_err_t app_controller_init(void)
{
    ESP_RETURN_ON_ERROR(storage_manager_init(), TAG, "storage init failed");
    ESP_RETURN_ON_ERROR(usb_msc_service_init(), TAG, "usb msc init failed");
    ESP_RETURN_ON_ERROR(logger_service_init(), TAG, "logger init failed");
    ESP_RETURN_ON_ERROR(acquisition_service_init(), TAG, "acquisition init failed");
    s_app_mode = APP_MODE_NORMAL;
    return ESP_OK;
}

esp_err_t app_controller_start(void)
{
    ESP_RETURN_ON_ERROR(logger_service_start(), TAG, "logger start failed");
    ESP_RETURN_ON_ERROR(logger_service_log_event("system_start", "normal_mode"), TAG, "startup log failed");
    ESP_RETURN_ON_ERROR(acquisition_service_start(), TAG, "acquisition start failed");
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
        ESP_RETURN_ON_ERROR(logger_service_log_event("usb_msc_requested", "handoff_to_host"), TAG, "pre-handoff log failed");
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
    ESP_RETURN_ON_ERROR(logger_service_start(), TAG, "logger restart failed");
    ESP_RETURN_ON_ERROR(logger_service_log_event("usb_msc_released", "returned_to_firmware"), TAG, "resume log failed");
    ESP_RETURN_ON_ERROR(acquisition_service_start(), TAG, "acquisition restart failed");
    s_app_mode = APP_MODE_NORMAL;
    ESP_LOGI(TAG, "Returned to normal mode");
    return ESP_OK;
}
