#pragma once

#include <stdint.h>

#define DATA_MODEL_THERMOCOUPLE_COUNT 8
#define DATA_MODEL_ANALOG_INPUT_COUNT 5

typedef struct {
    uint64_t uptime_ms;
    float thermocouples_c[DATA_MODEL_THERMOCOUPLE_COUNT];
    uint16_t thermocouple_valid_mask;
    uint16_t thermocouple_oc_mask;   /**< Open-circuit fault per channel */
    uint16_t thermocouple_scg_mask;  /**< Short-to-GND fault per channel */
    uint16_t thermocouple_scv_mask;  /**< Short-to-VCC fault per channel */
    float analog_inputs[DATA_MODEL_ANALOG_INPUT_COUNT];
    uint8_t analog_valid_mask;
    uint32_t digital_inputs;
    uint32_t digital_valid_mask;
} kdl_sensor_sample_t;
