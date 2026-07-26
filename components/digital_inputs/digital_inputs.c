#include "digital_inputs.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "digital_inputs";

static digital_inputs_config_t s_config;
static bool s_initialized;

bool digital_inputs_has_valid_pins(const digital_inputs_config_t *config)
{
    if (config == NULL || config->channel_count == 0 || config->channel_count > DIGITAL_INPUTS_MAX_CHANNELS) {
        return false;
    }

    for (size_t channel = 0; channel < config->channel_count; ++channel) {
        if (config->gpio_num[channel] < 0) {
            return false;
        }
    }

    return true;
}

esp_err_t digital_inputs_init(const digital_inputs_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    ESP_RETURN_ON_FALSE(digital_inputs_has_valid_pins(config), ESP_ERR_INVALID_ARG, TAG, "invalid pin configuration");

    if (s_initialized) {
        return ESP_OK;
    }

    memset(&s_config, 0, sizeof(s_config));
    s_config = *config;

    for (size_t channel = 0; channel < s_config.channel_count; ++channel) {
        gpio_config_t io_config = {
            .pin_bit_mask = 1ULL << s_config.gpio_num[channel],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "gpio init failed");
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Digital input driver ready with %u channels", (unsigned)s_config.channel_count);
    return ESP_OK;
}

esp_err_t digital_inputs_read(uint32_t *inputs, uint32_t *valid_mask)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "driver not initialized");
    ESP_RETURN_ON_FALSE(inputs != NULL, ESP_ERR_INVALID_ARG, TAG, "inputs is null");
    ESP_RETURN_ON_FALSE(valid_mask != NULL, ESP_ERR_INVALID_ARG, TAG, "valid_mask is null");

    uint32_t input_bits = 0;
    uint32_t mask_bits = 0;

    for (size_t channel = 0; channel < s_config.channel_count; ++channel) {
        int level = gpio_get_level(s_config.gpio_num[channel]);
        if (level < 0) {
            ESP_LOGW(TAG, "gpio read failed on channel %u", (unsigned)channel);
            continue;
        }

        if (level != 0) {
            input_bits |= 1UL << channel;
        }
        mask_bits |= 1UL << channel;
    }

    *inputs = input_bits;
    *valid_mask = mask_bits;
    return ESP_OK;
}

esp_err_t digital_inputs_deinit(void)
{
    memset(&s_config, 0, sizeof(s_config));
    s_initialized = false;
    return ESP_OK;
}

bool digital_inputs_is_initialized(void)
{
    return s_initialized;
}
