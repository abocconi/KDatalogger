#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_err.h"

#define MAX31855_CHANNEL_COUNT 8

typedef struct {
    spi_host_device_t host;
    int sclk_io;
    int miso_io;
    int cs_io[MAX31855_CHANNEL_COUNT];
} max31855_config_t;

typedef struct {
    bool valid;
    bool open_circuit;
    bool short_to_gnd;
    bool short_to_vcc;
    float thermocouple_c;
    float internal_c;
    uint32_t raw_data;
} max31855_reading_t;

bool max31855_has_valid_pins(const max31855_config_t *config);
esp_err_t max31855_init(const max31855_config_t *config);
esp_err_t max31855_read_channel(size_t channel, max31855_reading_t *reading);
esp_err_t max31855_deinit(void);
bool max31855_is_initialized(void);
