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
#include "data_model_channels.h"
#include "digital_inputs.h"
#include "logger_service.h"
#include "max31855.h"
#include "pressure_scaling.h"
#include "sensor_config.h"
#include "settings_service.h"
#include "tach_math.h"
#include "tachometer.h"
#include "trend_service.h"

#define ACQUISITION_TASK_NAME "acquisition"
#define ACQUISITION_TASK_STACK_SIZE 4096
/* Must stay below the esp_lvgl_port task priority (4, see
 * ESP_LVGL_PORT_INIT_CONFIG) so a 1s acquisition/logging cycle can never
 * preempt and stall the display flush task. */
#define ACQUISITION_TASK_PRIORITY 3
/* Pinned away from the esp_lvgl_port task (core 0). Unpinned, cycles overran
 * at short acquisition periods during long LVGL renders (page switch, chart
 * redraw): once preempted on core 0, this task waited out the render there
 * instead of moving to core 1. */
#define ACQUISITION_TASK_CORE 1
/* Once-a-second line with every channel, for bench work on the inputs; off
 * in normal use. The line is ~220 chars: at 115200 baud that is ~19 ms of
 * blocking UART writes inside the loop, which would dominate the cycle at
 * short acquisition periods. Rate-limit it so diagnostics cost stays flat
 * regardless of the configured period. */
#define ACQUISITION_VERBOSE_LOG_ENABLED 0
#define ACQUISITION_VERBOSE_LOG_MIN_INTERVAL_US 1000000
/* Conversions averaged per analog reading. ~16 x 5 oneshot reads cost a few
 * ms per cycle and bring the ESP32 ADC noise down to a couple of LSB. */
#define ACQUISITION_ADC_SAMPLES 16U

static const char *TAG = "acquisition";

/* Shared between the acquisition task and the caller of start/stop. */
static TaskHandle_t volatile s_task_handle;
static bool s_initialized;
static volatile bool s_run_task;
static volatile bool s_active;
static bool s_use_max31855;
static bool s_use_digital_inputs;
static bool s_use_analog_inputs;
static bool s_use_tachometer;
static int s_record_enable_gpio = -1;
/** Recording state last handed to the logger; only touched by the acquisition task. */
static bool s_recording_requested;
static kdl_sensor_sample_t s_last_sample;
static kdl_sensor_extremes_t s_extremes;
static SemaphoreHandle_t s_sample_mutex;
/** Given by acquisition_service_stop() to cut the inter-cycle wait short. */
static SemaphoreHandle_t s_stop_signal;
/** Tachometer state, only touched by the acquisition task. */
static float s_engine_rpm;
static uint32_t s_tach_min_period_us;
static uint32_t s_tach_max_period_us;

#define ACQUISITION_SAMPLE_LOCK_TIMEOUT_MS 50

/** Per-channel thermocouple state, tracked only to log its changes. */
typedef enum {
    TC_STATE_UNKNOWN = 0, /**< No reading yet since boot */
    TC_STATE_OK,
    TC_STATE_OPEN,        /**< No probe plugged in: a normal configuration */
    TC_STATE_SHORT_GND,
    TC_STATE_SHORT_VCC,
    TC_STATE_FAULT,       /**< Fault bit without a specific cause */
    TC_STATE_READ_ERROR,  /**< SPI transfer to the MAX31855 failed */
} tc_state_t;

/* Kept across stop/start, so leaving USB mode does not repeat every
 * "probe disconnected" line. Only touched by the acquisition task. */
static tc_state_t s_tc_state[DATA_MODEL_THERMOCOUPLE_COUNT];

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

    if (sample->engine_rpm_valid
        && (!s_extremes.rpm_max_valid || sample->engine_rpm > s_extremes.rpm_max)) {
        s_extremes.rpm_max = sample->engine_rpm;
        s_extremes.rpm_max_valid = true;
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
        .samples_per_read = ACQUISITION_ADC_SAMPLES,
    };

    for (size_t channel = 0; channel < config.channel_count && channel < ANALOG_INPUTS_MAX_CHANNELS; ++channel) {
        config.gpio_num[channel] = board_config_analog_input_gpio(channel);
    }

    return config;
}

static tc_state_t acquisition_classify_reading(const max31855_reading_t *reading)
{
    if (reading->valid) {
        return TC_STATE_OK;
    }
    if (reading->short_to_vcc) {
        return TC_STATE_SHORT_VCC;
    }
    if (reading->short_to_gnd) {
        return TC_STATE_SHORT_GND;
    }
    if (reading->open_circuit) {
        return TC_STATE_OPEN;
    }
    return TC_STATE_FAULT;
}

/**
 * Log a channel only when its state changes: logging the state every cycle
 * flooded the console with one line per unplugged probe per period, and
 * blocked the task on the UART for tens of ms. An unplugged probe is a normal
 * setup, so it is reported as info; shorts and read errors stay warnings.
 */
static void acquisition_report_tc_state(size_t channel, tc_state_t state, uint32_t raw_data,
                                        esp_err_t read_err)
{
    const tc_state_t previous = s_tc_state[channel];
    if (state == previous) {
        return;
    }
    s_tc_state[channel] = state;

    switch (state) {
    case TC_STATE_OK:
        if (previous != TC_STATE_UNKNOWN) {
            ESP_LOGI(TAG, "Thermocouple %u: probe OK", (unsigned)channel);
        }
        break;
    case TC_STATE_OPEN:
        ESP_LOGI(TAG, "Thermocouple %u: probe disconnected", (unsigned)channel);
        break;
    case TC_STATE_SHORT_GND:
    case TC_STATE_SHORT_VCC:
    case TC_STATE_FAULT:
        ESP_LOGW(TAG, "Thermocouple %u: MAX31855 fault %s (raw=0x%08" PRIx32 ")",
                 (unsigned)channel,
                 state == TC_STATE_SHORT_GND ? "short to GND"
                 : state == TC_STATE_SHORT_VCC ? "short to VCC" : "unspecified",
                 raw_data);
        break;
    case TC_STATE_READ_ERROR:
        ESP_LOGW(TAG, "Thermocouple %u: MAX31855 read failed: %s", (unsigned)channel,
                 esp_err_to_name(read_err));
        break;
    case TC_STATE_UNKNOWN:
    default:
        break;
    }
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
            acquisition_report_tc_state(index, TC_STATE_READ_ERROR, 0, err);
            continue;
        }
        acquisition_report_tc_state(index, acquisition_classify_reading(&reading), reading.raw_data,
                                    ESP_OK);

        sample->thermocouples_c[index] = reading.thermocouple_c;
        if (reading.valid) {
            sample->thermocouple_valid_mask |= (uint16_t)(1U << index);
        } else {
            if (reading.open_circuit)  { sample->thermocouple_oc_mask  |= (uint16_t)(1U << index); }
            if (reading.short_to_gnd)  { sample->thermocouple_scg_mask |= (uint16_t)(1U << index); }
            if (reading.short_to_vcc)  { sample->thermocouple_scv_mask |= (uint16_t)(1U << index); }
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
    uint32_t saturated_mask = 0;
    esp_err_t err = analog_inputs_read(sample->analog_inputs, &valid_mask, &saturated_mask);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Analog input read failed: %s", esp_err_to_name(err));
        sample->analog_valid_mask = 0;
        return;
    }

    /* Pin voltage back to the terminal, where the sensor datasheet applies. */
    const float ratio = board_config_analog_input_divider_ratio();
    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        sample->analog_inputs[index] /= ratio;
    }
    sample->analog_valid_mask = (uint8_t)valid_mask;
    sample->analog_saturated_mask = (uint8_t)saturated_mask;
}

static void acquisition_fill_pressures(kdl_sensor_sample_t *sample, const sensor_config_t *config)
{
    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        const uint8_t bit = (uint8_t)(1U << index);
        if ((sample->analog_valid_mask & bit) == 0U) {
            continue;
        }

        float bar = 0.0f;
        const pressure_status_t status = pressure_scaling_convert(
            &config->pressure[index], sample->analog_inputs[index],
            settings_service_get_pressure_zero_v(index),
            (sample->analog_saturated_mask & bit) != 0U, &bar);
        switch (status) {
        case PRESSURE_STATUS_OK:
            sample->pressures_bar[index] = bar;
            sample->pressure_valid_mask |= bit;
            break;
        case PRESSURE_STATUS_FAULT_LOW:
        case PRESSURE_STATUS_FAULT_HIGH:
            sample->pressure_fault_mask |= bit;
            break;
        case PRESSURE_STATUS_DISABLED:
        default:
            break;
        }
    }
}

static void acquisition_fill_engine_rpm(kdl_sensor_sample_t *sample, const tach_cfg_t *config)
{
    if (!s_use_tachometer || !config->enabled) {
        s_engine_rpm = 0.0f;
        return;
    }

    /* Limits follow the configuration, re-read every cycle like the period. */
    const uint32_t min_period_us = tach_math_min_period_us(config);
    const uint32_t max_period_us = tach_math_timeout_us(config);
    if (min_period_us != s_tach_min_period_us || max_period_us != s_tach_max_period_us) {
        const esp_err_t limit_err = tachometer_set_period_limits(min_period_us, max_period_us);
        if (limit_err != ESP_OK) {
            ESP_LOGW(TAG, "tachometer limits not applied: %s", esp_err_to_name(limit_err));
            return;
        }
        s_tach_min_period_us = min_period_us;
        s_tach_max_period_us = max_period_us;
    }

    tachometer_reading_t reading;
    const esp_err_t err = tachometer_take(&reading);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tachometer read failed: %s", esp_err_to_name(err));
        return;
    }

    const int64_t since_last_edge_us = esp_timer_get_time() - reading.last_edge_us;
    s_engine_rpm = tach_math_update(config, s_engine_rpm, reading.periods, reading.ticks_sum,
                                    reading.resolution_hz, reading.edge_seen, since_last_edge_us);
    sample->engine_rpm = s_engine_rpm;
    sample->engine_rpm_valid = true;
}

static void acquisition_fill_sample(kdl_sensor_sample_t *sample)
{
    memset(sample, 0, sizeof(*sample));
    sample->uptime_ms = acquisition_get_uptime_ms();

    /* A group whose driver failed to start keeps the all-zero valid mask left
     * by the memset above, so its channels read as invalid everywhere. */
    sensor_config_t config;
    sensor_config_get(&config);

    if (s_use_analog_inputs) {
        acquisition_fill_analog_inputs(sample);
        acquisition_fill_pressures(sample, &config);
    }
    acquisition_fill_engine_rpm(sample, &config.tach);
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
 * ACQUISITION_PERIOD_MS. Only the edge is sent: the logger task opens and
 * closes the file on its own time, and retries an open that fails.
 */
static void acquisition_sync_recording_state(void)
{
    if (s_record_enable_gpio < 0) {
        return;
    }

    const bool want_recording = gpio_get_level(s_record_enable_gpio) != 0;
    if (want_recording == s_recording_requested) {
        return;
    }

    const esp_err_t err = want_recording ? logger_service_request_start()
                                         : logger_service_request_stop();
    if (err != ESP_OK) {
        /* State left unchanged: the edge is sent again next cycle. */
        ESP_LOGW(TAG, "recording %s (AI1) not requested: %s",
                 want_recording ? "start" : "stop", esp_err_to_name(err));
        return;
    }

    s_recording_requested = want_recording;
    if (want_recording) {
        /* A new run starts with a clean slate: min/max on screen must describe
         * this session, not whatever the probes saw while idling in the pits. */
        (void)acquisition_service_reset_extremes();
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
    /* Normally false here (USB and update handoffs stop the logger after
     * acquisition); a session left open is then closed by the next AI1 check. */
    s_recording_requested = logger_service_is_active();

    TickType_t last_wake_time = xTaskGetTickCount();
#if ACQUISITION_VERBOSE_LOG_ENABLED
    int64_t last_verbose_log_us = 0;
#endif

    while (s_run_task) {
        acquisition_sync_recording_state();

        kdl_sensor_sample_t sample = {0};
        acquisition_fill_sample(&sample);

        if (xSemaphoreTake(s_sample_mutex, pdMS_TO_TICKS(ACQUISITION_SAMPLE_LOCK_TIMEOUT_MS)) == pdTRUE) {
            s_last_sample = sample;
            acquisition_update_extremes(&sample);
            xSemaphoreGive(s_sample_mutex);
        }

        esp_err_t trend_err = trend_service_add_sample(&sample);
        if (trend_err != ESP_OK) {
            ESP_LOGW(TAG, "trend update failed: %s", esp_err_to_name(trend_err));
        }

#if ACQUISITION_VERBOSE_LOG_ENABLED
        int64_t now_us = esp_timer_get_time();
        if (now_us - last_verbose_log_us >= ACQUISITION_VERBOSE_LOG_MIN_INTERVAL_US) {
            last_verbose_log_us = now_us;
            ESP_LOGI(TAG,
                 "[%"PRIu64"ms] TC(C): %.2f%s %.2f%s %.2f%s %.2f%s %.2f%s %.2f%s %.2f%s %.2f%s"
                 " | AI(V): %.3f %.3f %.3f %.3f %.3f (mask=0x%02x sat=0x%02x)"
                 " | P(bar): %.2f %.2f %.2f %.2f %.2f (mask=0x%02x fault=0x%02x)"
                 " | RPM: %.0f%s"
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
                 (unsigned)sample.analog_valid_mask, (unsigned)sample.analog_saturated_mask,
                 sample.pressures_bar[0], sample.pressures_bar[1], sample.pressures_bar[2],
                 sample.pressures_bar[3], sample.pressures_bar[4],
                 (unsigned)sample.pressure_valid_mask, (unsigned)sample.pressure_fault_mask,
                 sample.engine_rpm, sample.engine_rpm_valid ? "*" : "!",
                 sample.digital_inputs, sample.digital_valid_mask);
        }
#endif

        if (s_recording_requested) {
            /* Only queued here: a full queue is reported by the logger itself. */
            const esp_err_t log_err = logger_service_submit_sample(&sample);
            if (log_err != ESP_OK && log_err != ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Sample logging failed: %s", esp_err_to_name(log_err));
            }
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
    s_use_tachometer = false;
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

    const int tachometer_gpio = board_config_tachometer_gpio();
    if (tachometer_gpio < 0) {
        ESP_LOGW(TAG, "Tachometer pin not configured, engine speed will read as invalid");
    } else {
        esp_err_t tach_err = tachometer_init(tachometer_gpio);
        if (tach_err != ESP_OK) {
            ESP_LOGE(TAG, "Tachometer init failed, engine speed will read as invalid: %s",
                     esp_err_to_name(tach_err));
        } else {
            s_use_tachometer = true;
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

    /* The trace would otherwise join the samples before and after the pause
     * (USB mode) as if they were contiguous. Not fatal: the worst case is a
     * graph spanning the gap. */
    esp_err_t trend_err = trend_service_reset();
    if (trend_err != ESP_OK) {
        ESP_LOGW(TAG, "trend reset failed: %s", esp_err_to_name(trend_err));
    }

    /* The capture ISR kept accumulating while acquisition was stopped (USB
     * mode): averaged into the first cycle, those periods would show a speed
     * from minutes ago. */
    if (s_use_tachometer) {
        tachometer_reading_t stale;
        esp_err_t tach_err = tachometer_take(&stale);
        if (tach_err != ESP_OK) {
            ESP_LOGW(TAG, "tachometer reset failed: %s", esp_err_to_name(tach_err));
        }
    }
    s_engine_rpm = 0.0f;

    s_run_task = true;
    TaskHandle_t task_handle = NULL;
    BaseType_t task_created = xTaskCreatePinnedToCore(acquisition_task,
                                                      ACQUISITION_TASK_NAME,
                                                      ACQUISITION_TASK_STACK_SIZE,
                                                      NULL,
                                                      ACQUISITION_TASK_PRIORITY,
                                                      &task_handle,
                                                      ACQUISITION_TASK_CORE);
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

esp_err_t acquisition_service_zero_pressures(acquisition_zero_result_t *out)
{
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "out is null");
    memset(out, 0, sizeof(*out));
    ESP_RETURN_ON_FALSE(s_active, ESP_ERR_INVALID_STATE, TAG, "acquisition not running");

    kdl_sensor_sample_t sample;
    ESP_RETURN_ON_ERROR(acquisition_service_get_latest_sample(&sample), TAG, "no sample");
    ESP_RETURN_ON_FALSE(sample.uptime_ms != 0U, ESP_ERR_INVALID_STATE, TAG, "no sample yet");

    sensor_config_t config;
    sensor_config_get(&config);

    float zero_v[DATA_MODEL_ANALOG_INPUT_COUNT];
    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        const uint8_t bit = (uint8_t)(1U << index);
        zero_v[index] = settings_service_get_pressure_zero_v(index);
        if (!config.pressure[index].enabled) {
            continue;
        }
        out->enabled_mask |= bit;

        float candidate_v = 0.0f;
        if ((sample.analog_valid_mask & bit) != 0U && (sample.analog_saturated_mask & bit) == 0U
            && pressure_scaling_compute_zero(&config.pressure[index], sample.analog_inputs[index],
                                             &candidate_v)) {
            zero_v[index] = candidate_v;
            out->zeroed_mask |= bit;
            ESP_LOGI(TAG, "%s zero: %+.3f V", data_model_analog_channels[index].id,
                     (double)candidate_v);
        } else {
            ESP_LOGW(TAG, "%s not zeroed: reading %.3f V is a fault or not near 0 bar",
                     data_model_analog_channels[index].id, (double)sample.analog_inputs[index]);
        }
    }

    if (out->zeroed_mask == 0U) {
        return ESP_OK;
    }
    return settings_service_set_pressure_zero_v(zero_v);
}
