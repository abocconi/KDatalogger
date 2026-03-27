#include "acquisition_service.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "data_model.h"
#include "logger_service.h"

#define ACQUISITION_TASK_NAME "acquisition"
#define ACQUISITION_TASK_STACK_SIZE 4096
#define ACQUISITION_TASK_PRIORITY 5
#define ACQUISITION_PERIOD_MS 1000

static const char *TAG = "acquisition";

static TaskHandle_t s_task_handle;
static bool s_initialized;
static bool s_run_task;
static bool s_active;
static uint32_t s_sample_index;

static uint64_t acquisition_get_uptime_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static void acquisition_fill_stub_sample(kdl_sensor_sample_t *sample)
{
    sample->uptime_ms = acquisition_get_uptime_ms();
    sample->thermocouple_valid_mask = (1U << DATA_MODEL_THERMOCOUPLE_COUNT) - 1U;
    sample->analog_valid_mask = (1U << DATA_MODEL_ANALOG_INPUT_COUNT) - 1U;
    sample->digital_valid_mask = 0x000000FFU;
    sample->digital_inputs = s_sample_index & sample->digital_valid_mask;

    for (size_t index = 0; index < DATA_MODEL_THERMOCOUPLE_COUNT; ++index) {
        int32_t phase = (int32_t)((s_sample_index + (uint32_t)(index * 3U)) % 20U);
        float offset = ((float)phase - 10.0f) * 0.25f;
        sample->thermocouples_c[index] = 25.0f + ((float)index * 5.0f) + offset;
    }

    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        uint32_t phase = (s_sample_index * 17U + (uint32_t)index * 23U) % 100U;
        sample->analog_inputs[index] = ((float)phase * 3.3f) / 100.0f;
    }
}

static void acquisition_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Acquisition task started");
    (void)logger_service_log_event("acquisition_started", "stub_source");
    s_active = true;

    while (s_run_task) {
        kdl_sensor_sample_t sample = {0};
        acquisition_fill_stub_sample(&sample);
        if (logger_service_log_sample(&sample) != ESP_OK) {
            ESP_LOGW(TAG, "Sample logging failed");
        }
        s_sample_index++;
        vTaskDelay(pdMS_TO_TICKS(ACQUISITION_PERIOD_MS));
    }

    (void)logger_service_log_event("acquisition_stopped", "service_stop");
    s_active = false;
    s_task_handle = NULL;
    ESP_LOGI(TAG, "Acquisition task stopped");
    vTaskDelete(NULL);
}

esp_err_t acquisition_service_init(void)
{
    s_task_handle = NULL;
    s_initialized = true;
    s_run_task = false;
    s_active = false;
    s_sample_index = 0;
    return ESP_OK;
}

esp_err_t acquisition_service_start(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "acquisition not initialized");
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    s_run_task = true;
    BaseType_t task_created = xTaskCreate(acquisition_task,
                                          ACQUISITION_TASK_NAME,
                                          ACQUISITION_TASK_STACK_SIZE,
                                          NULL,
                                          ACQUISITION_TASK_PRIORITY,
                                          &s_task_handle);
    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG, "failed to create acquisition task");
    return ESP_OK;
}

esp_err_t acquisition_service_stop(void)
{
    if (s_task_handle == NULL) {
        s_run_task = false;
        s_active = false;
        return ESP_OK;
    }

    s_run_task = false;
    for (uint32_t retry = 0; retry < 60U && s_task_handle != NULL; ++retry) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ESP_RETURN_ON_FALSE(s_task_handle == NULL, ESP_ERR_TIMEOUT, TAG, "timeout waiting acquisition task stop");
    return ESP_OK;
}

bool acquisition_service_is_active(void)
{
    return s_active;
}
