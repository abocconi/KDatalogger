#include "esp_log.h"

#include "app_controller.h"

static const char *TAG = "kdl_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Booting KDatalogger");

    ESP_ERROR_CHECK(app_controller_init());
    ESP_ERROR_CHECK(app_controller_start());
    app_controller_run();
}
