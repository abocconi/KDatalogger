#include "app_controller.h"

#include "esp_check.h"
#include "esp_log.h"

#include "storage_manager.h"
#include "usb_msc_service.h"

static const char *TAG = "app_controller";
static app_mode_t s_app_mode = APP_MODE_NORMAL;

esp_err_t app_controller_init(void)
{
    ESP_RETURN_ON_ERROR(storage_manager_init(), TAG, "storage init failed");
    ESP_RETURN_ON_ERROR(usb_msc_service_init(), TAG, "usb msc init failed");
    s_app_mode = APP_MODE_NORMAL;
    return ESP_OK;
}

esp_err_t app_controller_start(void)
{
    ESP_LOGI(TAG, "KDatalogger services ready");
    return app_controller_enter_usb_msc_mode();
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
    s_app_mode = APP_MODE_NORMAL;
    ESP_LOGI(TAG, "Returned to normal mode");
    return ESP_OK;
}
