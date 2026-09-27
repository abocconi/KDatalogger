#include "tachometer.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/mcpwm_cap.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#if !CONFIG_MCPWM_ISR_CACHE_SAFE
#warning "CONFIG_MCPWM_ISR_CACHE_SAFE is off: edges are lost during flash writes"
#endif

static const char *TAG = "tachometer";

static mcpwm_cap_timer_handle_t s_timer;
static mcpwm_cap_channel_handle_t s_channel;
static uint32_t s_resolution_hz;
static bool s_initialized;

/* Shared with the capture ISR; every access is under s_lock. Plain statics
 * land in internal DRAM, reachable with the cache off. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_min_period_ticks;
static uint32_t s_max_period_ticks = UINT32_MAX;
static uint32_t s_last_capture;
static bool s_have_reference;
static bool s_edge_seen;
static int64_t s_last_edge_us;
static uint32_t s_periods;
static uint64_t s_ticks_sum;
static uint32_t s_glitches;

static bool IRAM_ATTR tachometer_on_capture(mcpwm_cap_channel_handle_t channel,
                                            const mcpwm_capture_event_data_t *edata,
                                            void *user_ctx)
{
    (void)channel;
    (void)user_ctx;

    const uint32_t capture = edata->cap_value;
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL_ISR(&s_lock);
    if (s_have_reference) {
        /* Unsigned difference: correct across the 32-bit counter wrap
         * (53 s at 80 MHz), far beyond any accepted period. */
        const uint32_t period = capture - s_last_capture;
        if (period < s_min_period_ticks) {
            s_glitches++;
            portEXIT_CRITICAL_ISR(&s_lock);
            return false;
        }
        if (period <= s_max_period_ticks) {
            s_ticks_sum += period;
            s_periods++;
        }
    }
    s_last_capture = capture;
    s_have_reference = true;
    s_edge_seen = true;
    s_last_edge_us = now_us;
    portEXIT_CRITICAL_ISR(&s_lock);

    return false;
}

esp_err_t tachometer_init(int gpio_num)
{
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(gpio_num), ESP_ERR_INVALID_ARG, TAG,
                        "invalid GPIO %d", gpio_num);
    if (s_initialized) {
        return ESP_OK;
    }

    const mcpwm_capture_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(mcpwm_new_capture_timer(&timer_config, &s_timer), TAG,
                        "capture timer creation failed");

    esp_err_t err = mcpwm_capture_timer_get_resolution(s_timer, &s_resolution_hz);
    if (err != ESP_OK) {
        goto fail_timer;
    }

    const mcpwm_capture_channel_config_t channel_config = {
        .gpio_num = gpio_num,
        .prescale = 1,
        .flags.pos_edge = true,
        .flags.neg_edge = false,
    };
    err = mcpwm_new_capture_channel(s_timer, &channel_config, &s_channel);
    if (err != ESP_OK) {
        goto fail_timer;
    }

    err = gpio_set_pull_mode(gpio_num, GPIO_FLOATING);
    if (err == ESP_OK) {
        const mcpwm_capture_event_callbacks_t callbacks = {
            .on_cap = tachometer_on_capture,
        };
        err = mcpwm_capture_channel_register_event_callbacks(s_channel, &callbacks, NULL);
    }
    if (err == ESP_OK) {
        err = mcpwm_capture_channel_enable(s_channel);
    }
    if (err != ESP_OK) {
        goto fail_channel;
    }

    err = mcpwm_capture_timer_enable(s_timer);
    if (err == ESP_OK) {
        err = mcpwm_capture_timer_start(s_timer);
        if (err != ESP_OK) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_capture_timer_disable(s_timer));
        }
    }
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_capture_channel_disable(s_channel));
        goto fail_channel;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "capturing rising edges on GPIO%d at %lu Hz", gpio_num,
             (unsigned long)s_resolution_hz);
    return ESP_OK;

fail_channel:
    ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_del_capture_channel(s_channel));
    s_channel = NULL;
fail_timer:
    ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_del_capture_timer(s_timer));
    s_timer = NULL;
    ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
    return err;
}

static uint32_t tachometer_us_to_ticks(uint32_t period_us)
{
    const uint64_t ticks = (uint64_t)period_us * s_resolution_hz / 1000000ULL;
    return (ticks > UINT32_MAX) ? UINT32_MAX : (uint32_t)ticks;
}

esp_err_t tachometer_set_period_limits(uint32_t min_period_us, uint32_t max_period_us)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(min_period_us < max_period_us, ESP_ERR_INVALID_ARG, TAG,
                        "min period %lu >= max period %lu", (unsigned long)min_period_us,
                        (unsigned long)max_period_us);

    const uint32_t min_ticks = tachometer_us_to_ticks(min_period_us);
    const uint32_t max_ticks = tachometer_us_to_ticks(max_period_us);

    portENTER_CRITICAL(&s_lock);
    s_min_period_ticks = min_ticks;
    s_max_period_ticks = max_ticks;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t tachometer_take(tachometer_reading_t *out)
{
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "out is null");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    portENTER_CRITICAL(&s_lock);
    out->periods = s_periods;
    out->ticks_sum = s_ticks_sum;
    out->glitches = s_glitches;
    out->edge_seen = s_edge_seen;
    out->last_edge_us = s_last_edge_us;
    s_periods = 0;
    s_ticks_sum = 0;
    s_glitches = 0;
    portEXIT_CRITICAL(&s_lock);

    out->resolution_hz = s_resolution_hz;
    return ESP_OK;
}

bool tachometer_is_initialized(void)
{
    return s_initialized;
}
