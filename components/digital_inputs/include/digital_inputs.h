#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define DIGITAL_INPUTS_MAX_CHANNELS 32

typedef struct {
    size_t channel_count;
    int gpio_num[DIGITAL_INPUTS_MAX_CHANNELS];
} digital_inputs_config_t;

bool digital_inputs_has_valid_pins(const digital_inputs_config_t *config);
esp_err_t digital_inputs_init(const digital_inputs_config_t *config);
esp_err_t digital_inputs_read(uint32_t *inputs, uint32_t *valid_mask);
esp_err_t digital_inputs_deinit(void);
bool digital_inputs_is_initialized(void);
