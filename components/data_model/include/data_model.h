#pragma once

#include <stdbool.h>
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
    float analog_inputs[DATA_MODEL_ANALOG_INPUT_COUNT]; /**< Volts at the terminal (divider compensated) */
    uint8_t analog_valid_mask;
    uint8_t analog_saturated_mask;   /**< ADC at full scale: terminal above the measurable range */
    float pressures_bar[DATA_MODEL_ANALOG_INPUT_COUNT]; /**< Index-aligned with analog_inputs */
    uint8_t pressure_valid_mask;
    uint8_t pressure_fault_mask;     /**< Configured sensor reading out of its range (wiring) */
    float engine_rpm;
    bool engine_rpm_valid;           /**< False with the tachometer disabled or not running */
    uint32_t digital_inputs;
    uint32_t digital_valid_mask;
} kdl_sensor_sample_t;
