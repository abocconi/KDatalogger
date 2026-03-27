#pragma once

#include <stdint.h>

#define DATA_MODEL_THERMOCOUPLE_COUNT 8
#define DATA_MODEL_ANALOG_INPUT_COUNT 4

typedef struct {
    uint64_t uptime_ms;
    float thermocouples_c[DATA_MODEL_THERMOCOUPLE_COUNT];
    uint16_t thermocouple_valid_mask;
    float analog_inputs[DATA_MODEL_ANALOG_INPUT_COUNT];
    uint8_t analog_valid_mask;
    uint32_t digital_inputs;
    uint32_t digital_valid_mask;
} kdl_sensor_sample_t;
