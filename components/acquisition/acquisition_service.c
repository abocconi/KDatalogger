#include "acquisition_service.h"

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_config.h"
#include "data_model.h"
#include "logger_service.h"
#include "max31855.h"

#define ACQUISITION_TASK_NAME "acquisition"
#define ACQUISITION_TASK_STACK_SIZE 4096
#define ACQUISITION_TASK_PRIORITY 5
#define ACQUISITION_PERIOD_MS 1000

static const char *TAG = "acquisition";

static TaskHandle_t s_task_handle;
static bool s_initialized;
static bool s_run_task;
static bool s_active;
static bool s_use_max31855;
static uint32_t s_sample_index;

static uint64_t acquisition_get_uptime_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static max31855_config_t acquisition_build_max31855_config(void)
{
    max31855_config_t config = {
        .host = board_config_max31855_host(),
        .sclk_io = board_config_max31855_sclk_io(),
        .miso_io = board_config_max31855_miso_io(),
    };

    for (size_t channel = 0; channel < MAX31855_CHANNEL_COUNT; ++channel) {
        config.cs_io[channel] = board_config_max31855_cs_io(channel);
    }

    return config;
}

static void acquisition_fill_stub_thermocouples(kdl_sensor_sample_t *sample)
{
    sample->thermocouple_valid_mask = (1U << DATA_MODEL_THERMOCOUPLE_COUNT) - 1U;

    for (size_t index = 0; index < DATA_MODEL_THERMOCOUPLE_COUNT; ++index) {
        int32_t phase = (int32_t)((s_sample_index + (uint32_t)(index * 3U)) % 20U);
        float offset = ((float)phase - 10.0f) * 0.25f;
        sample->thermocouples_c[index] = 25.0f + ((float)index * 5.0f) + offset;
    }
}

static void acquisition_fill_stub_io(kdl_sensor_sample_t *sample)
{
    sample->analog_valid_mask = (1U << DATA_MODEL_ANALOG_INPUT_COUNT) - 1U;
    sample->digital_valid_mask = 0x000000FFU;
    sample->digital_inputs = s_sample_index & sample->digital_valid_mask;

    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        uint32_t phase = (s_sample_index * 17U + (uint32_t)index * 23U) % 100U;
        sample->analog_inputs[index] = ((float)phase * 3.3f) / 100.0f;
    }
}

static void acquisition_fill_max31855_thermocouples(kdl_sensor_sample_t *sample)
{
    sample->thermocouple_valid_mask = 0;

    for (size_t index = 0; index < DATA_MODEL_THERMOCOUPLE_COUNT; ++index) {
        max31855_reading_t reading = {0};
        esp_err_t err = max31855_read_channel(index, &reading);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "MAX31855 read failed on channel %u: %s", (unsigned)index, esp_err_to_name(err));
            continue;
        }

        sample->thermocouples_c[index] = reading.thermocouple_c;
        if (reading.valid) {
            sample->thermocouple_valid_mask |= (uint16_t)(1U << index);
        } else {
            ESP_LOGW(TAG,
                     "MAX31855 fault on channel %u raw=0x%08" PRIx32 " oc=%d scg=%d scv=%d",
                     (unsigned)index,
                     reading.raw_data,
                     reading.open_circuit,
                     reading.short_to_gnd,
                     reading.short_to_vcc);
        }
    }
}

static void acquisition_fill_sample(kdl_sensor_sample_t *sample)
{
    memset(sample, 0, sizeof(*sample));
    sample->uptime_ms = acquisition_get_uptime_ms();
    acquisition_fill_stub_io(sample);

    if (s_use_max31855) {
        acquisition_fill_max31855_thermocouples(sample);
    } else {
        acquisition_fill_stub_thermocouples(sample);
    }
}

static void acquisition_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Acquisition task started");
    (void)logger_service_log_event("acquisition_started", s_use_max31855 ? "max31855" : "stub_placeholder_pins");
    s_active = true;

    while (s_run_task) {
        kdl_sensor_sample_t sample = {0};
        acquisition_fill_sample(&sample);
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
    s_use_max31855 = false;

    max31855_config_t max31855_config = acquisition_build_max31855_config();
    if (!board_config_max31855_has_valid_pins() || !max31855_has_valid_pins(&max31855_config)) {
        ESP_LOGI(TAG, "MAX31855 pins not configured yet, acquisition will use stub thermocouples");
        return ESP_OK;
    }

    esp_err_t err = max31855_init(&max31855_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MAX31855 init failed, keeping stub thermocouples: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    s_use_max31855 = true;
    ESP_LOGI(TAG, "MAX31855 driver enabled for thermocouple acquisition");
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
