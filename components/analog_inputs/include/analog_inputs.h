#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

#define ANALOG_INPUTS_MAX_CHANNELS 8

typedef struct {
    size_t channel_count;
    int gpio_num[ANALOG_INPUTS_MAX_CHANNELS];
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
} analog_inputs_config_t;

bool analog_inputs_has_valid_pins(const analog_inputs_config_t *config);
esp_err_t analog_inputs_init(const analog_inputs_config_t *config);
esp_err_t analog_inputs_read(float *values_v, uint32_t *valid_mask);
esp_err_t analog_inputs_deinit(void);
bool analog_inputs_is_initialized(void);
