#pragma once

#include "data_model.h"

/**
 * @file data_model_channels.h
 * @brief Channel identity shared by the display and the log file.
 *
 * One table per input group, index-aligned with kdl_sensor_sample_t, so the
 * label on screen and the column header in the CSV cannot drift apart.
 * Names are compile-time: renaming a channel is a firmware change.
 */

typedef struct {
    const char *id;   /**< Terminal marking on the enclosure, e.g. "Tc1"        */
    const char *name; /**< Short label, e.g. "Cil 1"; ASCII only (display font) */
    const char *unit; /**< Unit of the value held in the sample, e.g. "V"       */
} kdl_channel_info_t;

/** @brief Thermocouple channels, index-aligned with kdl_sensor_sample_t.thermocouples_c. */
extern const kdl_channel_info_t data_model_thermocouple_channels[DATA_MODEL_THERMOCOUPLE_COUNT];

/** @brief Pressure channels on the analog inputs, index-aligned with
 *         kdl_sensor_sample_t.analog_inputs and .pressures_bar. */
extern const kdl_channel_info_t data_model_analog_channels[DATA_MODEL_ANALOG_INPUT_COUNT];

/** @brief Engine speed, kdl_sensor_sample_t.engine_rpm. */
extern const kdl_channel_info_t data_model_rpm_channel;
