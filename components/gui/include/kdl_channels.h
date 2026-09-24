#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "data_model.h"
#include "data_model_channels.h"

/**
 * @file kdl_channels.h
 * @brief Display descriptors: channel identity plus rendering calibration.
 *
 * Id, name and unit come from data_model_channels.h, shared with the log
 * file. What is added here is display-only: full-scale and threshold values
 * feed the fill bars, their zone bands and the state colours; they are
 * calibration knobs, not physical limits. Channels 1-4 share one axis on the
 * main page, so keep their three values identical.
 */

/** @brief Analog channels rendered on the main page (IN2..IN6).
 *
 * IN1 is the recording-enable contact and is not part of the sampled analog
 * group at all, so every sampled channel is shown. */
#define KDL_ANALOG_DISPLAY_COUNT DATA_MODEL_ANALOG_INPUT_COUNT

/** @brief Thermocouple channels rendered on the main page. */
#define KDL_THERMOCOUPLE_DISPLAY_COUNT DATA_MODEL_THERMOCOUPLE_COUNT

typedef struct {
    const kdl_channel_info_t *channel; /**< Shared id/name/unit              */
    float full_scale;      /**< Fill-bar 100 % point, in the channel's unit  */
    float warn_threshold;  /**< Amber from here up; start of the warning band */
    float alarm_threshold; /**< Red from here up; start of the alarm band     */
} kdl_thermocouple_desc_t;

typedef struct {
    const kdl_channel_info_t *channel; /**< Shared id/name/unit          */
    uint8_t decimals;    /**< Digits after the decimal point when printed */
    uint8_t input_index; /**< Index into kdl_sensor_sample_t.analog_inputs */
} kdl_analog_desc_t;

/**
 * @brief Descriptor for thermocouple channel @p index (0-based).
 * @return NULL if @p index is out of range.
 */
const kdl_thermocouple_desc_t *kdl_channels_thermocouple(uint8_t index);

/**
 * @brief Descriptor for the @p index-th *displayed* analog channel (0-based).
 *
 * The returned descriptor carries its own input_index for indexing into the
 * sample.
 * @return NULL if @p index is out of range.
 */
const kdl_analog_desc_t *kdl_channels_analog(uint8_t index);

/**
 * @brief Shared unit suffix for every thermocouple channel.
 *
 * The MAX31855 is a Type K part, so the unit is fixed at compile time; the
 * degree sign is present in the text fonts (see kdl_theme.h).
 */
const char *kdl_channels_temperature_unit(void);

/**
 * @brief Whether a thermocouple channel currently has no probe attached.
 *
 * Open-circuit is the MAX31855's own unplugged-probe report, so it is used
 * directly as the "channel not connected" signal instead of a settings flag:
 * the main page dims such channels and the graph page skips them.
 */
bool kdl_channels_is_disconnected(const kdl_sensor_sample_t *sample, uint8_t index);

/**
 * @brief Whether a thermocouple channel is reporting a usable reading.
 */
bool kdl_channels_is_valid(const kdl_sensor_sample_t *sample, uint8_t index);
