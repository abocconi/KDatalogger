#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

#define ANALOG_INPUTS_MAX_CHANNELS 8
#define ANALOG_INPUTS_MAX_SAMPLES  64U

typedef struct {
    size_t channel_count;
    int gpio_num[ANALOG_INPUTS_MAX_CHANNELS];
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
    uint32_t samples_per_read; /**< Conversions averaged per reading, 1..ANALOG_INPUTS_MAX_SAMPLES */
} analog_inputs_config_t;

bool analog_inputs_has_valid_pins(const analog_inputs_config_t *config);
esp_err_t analog_inputs_init(const analog_inputs_config_t *config);

/**
 * @brief Read every channel, averaging samples_per_read conversions each.
 *
 * @param values_v       Pin voltage per channel.
 * @param valid_mask     Channels read without error.
 * @param saturated_mask Channels whose average sits at the top of the ADC
 *                       range, i.e. whose real voltage is unknown. May be NULL.
 */
esp_err_t analog_inputs_read(float *values_v, uint32_t *valid_mask, uint32_t *saturated_mask);
esp_err_t analog_inputs_deinit(void);
bool analog_inputs_is_initialized(void);
