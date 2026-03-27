#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_controller.h"

static const char *TAG = "kdl_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Booting KDatalogger");

    ESP_ERROR_CHECK(app_controller_init());
    ESP_ERROR_CHECK(app_controller_start());

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
