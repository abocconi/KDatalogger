#include "acquisition_service.h"

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "analog_inputs.h"
#include "board_config.h"
#include "data_model.h"
#include "digital_inputs.h"
#include "logger_service.h"
#include "max31855.h"
#include "settings_service.h"

#define ACQUISITION_TASK_NAME "acquisition"
#define ACQUISITION_TASK_STACK_SIZE 4096
/* Must stay below the esp_lvgl_port task priority (4, see
 * ESP_LVGL_PORT_INIT_CONFIG) so a 1s acquisition/logging cycle can never
 * preempt and stall the display flush task. */
#define ACQUISITION_TASK_PRIORITY 3
/* The per-sample sensor line is ~220 chars: at 115200 baud that is ~19 ms of
 * blocking UART writes inside the loop, which would dominate the cycle at
 * short acquisition periods. Rate-limit it so diagnostics cost stays flat
 * regardless of the configured period. */
#define ACQUISITION_VERBOSE_LOG_MIN_INTERVAL_US 1000000

static const char *TAG = "acquisition";

/* Shared between the acquisition task and the caller of start/stop. */
static TaskHandle_t volatile s_task_handle;
static bool s_initialized;
static volatile bool s_run_task;
static volatile bool s_active;
static bool s_use_max31855;
static bool s_use_digital_inputs;
static bool s_use_analog_inputs;
static int s_record_enable_gpio = -1;
static kdl_sensor_sample_t s_last_sample;
static kdl_sensor_extremes_t s_extremes;
static SemaphoreHandle_t s_sample_mutex;
/** Given by acquisition_service_stop() to cut the inter-cycle wait short. */
static SemaphoreHandle_t s_stop_signal;

#define ACQUISITION_SAMPLE_LOCK_TIMEOUT_MS 50

/** Fold one sample into the running extremes. Caller must hold s_sample_mutex. */
static void acquisition_update_extremes(const kdl_sensor_sample_t *sample)
{
    for (size_t index = 0; index < DATA_MODEL_THERMOCOUPLE_COUNT; ++index) {
        const uint16_t bit = (uint16_t)(1U << index);
        if ((sample->thermocouple_valid_mask & bit) == 0U) {
            continue;
        }

        const float value = sample->thermocouples_c[index];
        if ((s_extremes.valid_mask & bit) == 0U) {
            s_extremes.thermocouple_min_c[index] = value;
            s_extremes.thermocouple_max_c[index] = value;
            s_extremes.valid_mask |= bit;
            continue;
        }

        if (value < s_extremes.thermocouple_min_c[index]) {
            s_extremes.thermocouple_min_c[index] = value;
        }
        if (value > s_extremes.thermocouple_max_c[index]) {
            s_extremes.thermocouple_max_c[index] = value;
        }
    }
}

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

static digital_inputs_config_t acquisition_build_digital_input_config(void)
{
    digital_inputs_config_t config = {
        .channel_count = board_config_digital_input_count(),
    };

    for (size_t channel = 0; channel < config.channel_count && channel < DIGITAL_INPUTS_MAX_CHANNELS; ++channel) {
        config.gpio_num[channel] = board_config_digital_input_gpio(channel);
    }

    return config;
}

static analog_inputs_config_t acquisition_build_analog_input_config(void)
{
    analog_inputs_config_t config = {
        .channel_count = board_config_analog_input_count(),
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    for (size_t channel = 0; channel < config.channel_count && channel < ANALOG_INPUTS_MAX_CHANNELS; ++channel) {
        config.gpio_num[channel] = board_config_analog_input_gpio(channel);
    }

    return config;
}

static void acquisition_fill_max31855_thermocouples(kdl_sensor_sample_t *sample)
{
    sample->thermocouple_valid_mask = 0;
    sample->thermocouple_oc_mask   = 0;
    sample->thermocouple_scg_mask  = 0;
    sample->thermocouple_scv_mask  = 0;

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
            if (reading.open_circuit)  { sample->thermocouple_oc_mask  |= (uint16_t)(1U << index); }
            if (reading.short_to_gnd)  { sample->thermocouple_scg_mask |= (uint16_t)(1U << index); }
            if (reading.short_to_vcc)  { sample->thermocouple_scv_mask |= (uint16_t)(1U << index); }
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

static void acquisition_fill_digital_inputs(kdl_sensor_sample_t *sample)
{
    esp_err_t err = digital_inputs_read(&sample->digital_inputs, &sample->digital_valid_mask);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Digital input read failed: %s", esp_err_to_name(err));
        sample->digital_inputs = 0;
        sample->digital_valid_mask = 0;
    }
}

static void acquisition_fill_analog_inputs(kdl_sensor_sample_t *sample)
{
    uint32_t valid_mask = 0;
    esp_err_t err = analog_inputs_read(sample->analog_inputs, &valid_mask);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Analog input read failed: %s", esp_err_to_name(err));
        sample->analog_valid_mask = 0;
        return;
    }

    sample->analog_valid_mask = (uint8_t)valid_mask;
}

static void acquisition_fill_sample(kdl_sensor_sample_t *sample)
{
    memset(sample, 0, sizeof(*sample));
    sample->uptime_ms = acquisition_get_uptime_ms();

    /* A group whose driver failed to start keeps the all-zero valid mask left
     * by the memset above, so its channels read as invalid everywhere. */
    if (s_use_analog_inputs) {
        acquisition_fill_analog_inputs(sample);
    }
    if (s_use_max31855) {
        acquisition_fill_max31855_thermocouples(sample);
    }
    if (s_use_digital_inputs) {
        acquisition_fill_digital_inputs(sample);
    }
}

/**
 * @brief Start/stop logging to follow AI1 (GPIO13), read as a digital input.
 *
 * High = recording ON, low = recording OFF. Called once per acquisition
 * cycle, so the response to a level change is bounded by
 * ACQUISITION_PERIOD_MS.
 */
static void acquisition_sync_recording_state(void)
{
    if (s_record_enable_gpio < 0) {
        return;
    }

    bool want_recording = gpio_get_level(s_record_enable_gpio) != 0;
    bool is_recording = logger_service_is_active();
    if (want_recording == is_recording) {
        return;
    }

    esp_err_t err = want_recording ? logger_service_start() : logger_service_stop();
    if (err == ESP_OK && want_recording) {
        /* A new run starts with a clean slate: min/max on screen must describe
         * this session, not whatever the probes saw while idling in the pits. */
        (void)acquisition_service_reset_extremes();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "recording %s (AI1) failed: %s",
                 want_recording ? "start" : "stop", esp_err_to_name(err));
    }
}

/**
 * @brief Sleep until the next fixed-phase deadline, or until a stop request.
 *
 * Stands in for xTaskDelayUntil(): with periods up to 5 s a stop request
 * cannot wait out the delay. The wake-up comes from a semaphore rather than
 * xTaskAbortDelay() on the task handle, because the semaphore is never freed
 * under the caller and it latches a request made while this task is still
 * busy with the current cycle -- an abort in that window would be lost.
 *
 * The period is re-read every cycle so a settings change applies to the next
 * one.
 */
static void acquisition_wait_next_cycle(TickType_t *next_wake)
{
    const uint32_t period_ms = settings_service_get_acquisition_period_ms();
    const TickType_t period_ticks = pdMS_TO_TICKS(period_ms);
    *next_wake += period_ticks;

    /* Unsigned difference: wrap-safe, and a deadline already in the past
     * shows up as a value larger than one period. */
    const TickType_t now = xTaskGetTickCount();
    TickType_t remaining = *next_wake - now;
    if (remaining > period_ticks) {
        /* The cycle overran its period. Re-anchor the phase, otherwise the
         * following waits would expire back-to-back trying to catch up and
         * starve lower-priority tasks. */
        *next_wake = now;
        remaining = 0;
        ESP_LOGW(TAG, "acquisition cycle overran the %" PRIu32 " ms period", period_ms);
    }

    /* Timing out is the normal path; taking the semaphore means a stop was
     * requested, which the caller's loop condition picks up either way. */
    (void)xSemaphoreTake(s_stop_signal, remaining);
}

static void acquisition_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Acquisition task started");
    s_active = true;

    TickType_t last_wake_time = xTaskGetTickCount();
    int64_t last_verbose_log_us = 0;

    while (s_run_task) {
        acquisition_sync_recording_state();

        kdl_sensor_sample_t sample = {0};
        acquisition_fill_sample(&sample);

        if (xSemaphoreTake(s_sample_mutex, pdMS_TO_TICKS(ACQUISITION_SAMPLE_LOCK_TIMEOUT_MS)) == pdTRUE) {
            s_last_sample = sample;
            acquisition_update_extremes(&sample);
            xSemaphoreGive(s_sample_mutex);
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us - last_verbose_log_us >= ACQUISITION_VERBOSE_LOG_MIN_INTERVAL_US) {
            last_verbose_log_us = now_us;
            ESP_LOGI(TAG,
                 "[%"PRIu64"ms] TC(C): %.2f%s %.2f%s %.2f%s %.2f%s %.2f%s %.2f%s %.2f%s %.2f%s"
                 " | AI(V): %.3f %.3f %.3f %.3f %.3f (mask=0x%02x)"
                 " | DI: 0x%02"PRIx32" (mask=0x%02"PRIx32")",
                 sample.uptime_ms,
                 sample.thermocouples_c[0], (sample.thermocouple_valid_mask & (1U << 0)) ? "*" : "!",
                 sample.thermocouples_c[1], (sample.thermocouple_valid_mask & (1U << 1)) ? "*" : "!",
                 sample.thermocouples_c[2], (sample.thermocouple_valid_mask & (1U << 2)) ? "*" : "!",
                 sample.thermocouples_c[3], (sample.thermocouple_valid_mask & (1U << 3)) ? "*" : "!",
                 sample.thermocouples_c[4], (sample.thermocouple_valid_mask & (1U << 4)) ? "*" : "!",
                 sample.thermocouples_c[5], (sample.thermocouple_valid_mask & (1U << 5)) ? "*" : "!",
                 sample.thermocouples_c[6], (sample.thermocouple_valid_mask & (1U << 6)) ? "*" : "!",
                 sample.thermocouples_c[7], (sample.thermocouple_valid_mask & (1U << 7)) ? "*" : "!",
                 sample.analog_inputs[0], sample.analog_inputs[1], sample.analog_inputs[2],
                 sample.analog_inputs[3], sample.analog_inputs[4],
                 (unsigned)sample.analog_valid_mask,
                 sample.digital_inputs, sample.digital_valid_mask);
        }

        if (logger_service_is_active() && logger_service_log_sample(&sample) != ESP_OK) {
            ESP_LOGW(TAG, "Sample logging failed");
        }

        acquisition_wait_next_cycle(&last_wake_time);
    }

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
    s_use_max31855 = false;
    s_use_digital_inputs = false;
    s_use_analog_inputs = false;
    s_record_enable_gpio = -1;
    memset(&s_last_sample, 0, sizeof(s_last_sample));

    if (s_sample_mutex == NULL) {
        s_sample_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_sample_mutex != NULL, ESP_ERR_NO_MEM, TAG, "failed to create sample mutex");
    }

    if (s_stop_signal == NULL) {
        s_stop_signal = xSemaphoreCreateBinary();
        ESP_RETURN_ON_FALSE(s_stop_signal != NULL, ESP_ERR_NO_MEM, TAG, "failed to create stop signal");
    }

    int record_enable_gpio = board_config_record_enable_gpio();
    if (record_enable_gpio < 0) {
        ESP_LOGI(TAG, "Record-enable pin not configured, logging will not follow AI1");
    } else {
        gpio_config_t record_enable_config = {
            .pin_bit_mask = 1ULL << record_enable_gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t record_enable_err = gpio_config(&record_enable_config);
        if (record_enable_err != ESP_OK) {
            ESP_LOGW(TAG, "Record-enable GPIO%d config failed, logging will not follow AI1: %s",
                     record_enable_gpio, esp_err_to_name(record_enable_err));
        } else {
            s_record_enable_gpio = record_enable_gpio;
            ESP_LOGI(TAG, "Recording follows AI1 (GPIO%d): high = ON, low = OFF", record_enable_gpio);
        }
    }

    max31855_config_t max31855_config = acquisition_build_max31855_config();
    if (!board_config_max31855_has_valid_pins() || !max31855_has_valid_pins(&max31855_config)) {
        ESP_LOGE(TAG, "MAX31855 pins not configured, thermocouples will read as invalid");
    } else {
        esp_err_t thermocouple_err = max31855_init(&max31855_config);
        if (thermocouple_err != ESP_OK) {
            ESP_LOGE(TAG, "MAX31855 init failed, thermocouples will read as invalid: %s",
                     esp_err_to_name(thermocouple_err));
        } else {
            s_use_max31855 = true;
            ESP_LOGI(TAG, "MAX31855 driver enabled for thermocouple acquisition");
        }
    }

    digital_inputs_config_t digital_config = acquisition_build_digital_input_config();
    if (!board_config_digital_inputs_has_valid_pins() || !digital_inputs_has_valid_pins(&digital_config)) {
        ESP_LOGE(TAG, "Digital input pins not configured, digital inputs will read as invalid");
    } else {
        esp_err_t digital_err = digital_inputs_init(&digital_config);
        if (digital_err != ESP_OK) {
            ESP_LOGE(TAG, "Digital input init failed, digital inputs will read as invalid: %s",
                     esp_err_to_name(digital_err));
        } else {
            s_use_digital_inputs = true;
            ESP_LOGI(TAG, "Digital input driver enabled");
        }
    }

    analog_inputs_config_t analog_config = acquisition_build_analog_input_config();
    if (!board_config_analog_inputs_has_valid_pins() || !analog_inputs_has_valid_pins(&analog_config)) {
        ESP_LOGE(TAG, "Analog input pins not configured, analog inputs will read as invalid");
        return ESP_OK;
    }

    esp_err_t analog_err = analog_inputs_init(&analog_config);
    if (analog_err != ESP_OK) {
        ESP_LOGE(TAG, "Analog input init failed, analog inputs will read as invalid: %s",
                 esp_err_to_name(analog_err));
        return ESP_OK;
    }

    s_use_analog_inputs = true;
    ESP_LOGI(TAG, "Analog input driver enabled");
    return ESP_OK;
}

esp_err_t acquisition_service_start(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "acquisition not initialized");
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    /* Drop a stop signal left pending by the previous run -- given after the
     * task had already left its loop -- or the first wait would return early. */
    (void)xSemaphoreTake(s_stop_signal, 0);

    s_run_task = true;
    TaskHandle_t task_handle = NULL;
    BaseType_t task_created = xTaskCreate(acquisition_task,
                                          ACQUISITION_TASK_NAME,
                                          ACQUISITION_TASK_STACK_SIZE,
                                          NULL,
                                          ACQUISITION_TASK_PRIORITY,
                                          &task_handle);
    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG, "failed to create acquisition task");
    s_task_handle = task_handle;
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
    /* Wake the task from its inter-cycle wait: the wait below then only covers
     * the cycle in progress, not a whole acquisition period. A failed give
     * just means a signal is already pending. */
    (void)xSemaphoreGive(s_stop_signal);
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

esp_err_t acquisition_service_get_latest_sample(kdl_sensor_sample_t *out)
{
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "out is null");
    ESP_RETURN_ON_FALSE(s_initialized && s_sample_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "acquisition not initialized");

    if (xSemaphoreTake(s_sample_mutex, pdMS_TO_TICKS(ACQUISITION_SAMPLE_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *out = s_last_sample;
    xSemaphoreGive(s_sample_mutex);
    return ESP_OK;
}

esp_err_t acquisition_service_get_extremes(kdl_sensor_extremes_t *out)
{
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "out is null");
    ESP_RETURN_ON_FALSE(s_initialized && s_sample_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "acquisition not initialized");

    if (xSemaphoreTake(s_sample_mutex, pdMS_TO_TICKS(ACQUISITION_SAMPLE_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *out = s_extremes;
    xSemaphoreGive(s_sample_mutex);
    return ESP_OK;
}

esp_err_t acquisition_service_reset_extremes(void)
{
    ESP_RETURN_ON_FALSE(s_initialized && s_sample_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "acquisition not initialized");

    if (xSemaphoreTake(s_sample_mutex, pdMS_TO_TICKS(ACQUISITION_SAMPLE_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(&s_extremes, 0, sizeof(s_extremes));
    xSemaphoreGive(s_sample_mutex);
    return ESP_OK;
}
