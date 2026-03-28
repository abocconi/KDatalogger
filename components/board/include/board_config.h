#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "driver/spi_master.h"

#define BOARD_MAX31855_CHANNEL_COUNT 8
#define BOARD_DIGITAL_INPUT_COUNT 8
#define BOARD_ANALOG_INPUT_COUNT 4
#define BOARD_PIN_PLACEHOLDER (-1)

spi_host_device_t board_config_max31855_host(void);
int board_config_max31855_sclk_io(void);
int board_config_max31855_miso_io(void);
int board_config_max31855_cs_io(size_t channel);
bool board_config_max31855_has_valid_pins(void);

size_t board_config_digital_input_count(void);
int board_config_digital_input_gpio(size_t channel);
bool board_config_digital_inputs_has_valid_pins(void);

size_t board_config_analog_input_count(void);
int board_config_analog_input_gpio(size_t channel);
bool board_config_analog_inputs_has_valid_pins(void);
