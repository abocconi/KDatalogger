#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "data_model.h"

/**
 * @file kdl_channels.h
 * @brief Static channel descriptors: names, units, scaling and thresholds.
 *
 * Channel naming is deliberately compile-time. An on-device text editor
 * driven by five keys was scoped out, so renaming a channel is a firmware
 * change. Full-scale and threshold values feed the card fill bar and the
 * value colour; they are calibration knobs, not physical limits.
 */

/** @brief Analog input reserved as the recording-enable contact.
 *
 * AI1 is wired to the REC switch and is never rendered as a measurement, so
 * the main page shows exactly DATA_MODEL_ANALOG_INPUT_COUNT-1 analog cells.
 * See acquisition_sync_recording_state() in acquisition_service.c. */
#define KDL_ANALOG_RECORD_ENABLE_INDEX 0U

/** @brief Analog channels actually rendered on the main page. */
#define KDL_ANALOG_DISPLAY_COUNT (DATA_MODEL_ANALOG_INPUT_COUNT - 1U)

/** @brief Thermocouple channels rendered on the main page. */
#define KDL_THERMOCOUPLE_DISPLAY_COUNT DATA_MODEL_THERMOCOUPLE_COUNT

typedef struct {
    const char *id;        /**< Terminal marking on the enclosure, e.g. "K1" */
    const char *name;      /**< Short human label, e.g. "Exh 1"              */
    float full_scale;      /**< Fill-bar 100 % point, in the channel's unit  */
    float warn_threshold;  /**< Value colour turns amber at or above this    */
    float alarm_threshold; /**< Value colour turns red at or above this      */
} kdl_thermocouple_desc_t;

typedef struct {
    const char *id;      /**< Terminal marking, e.g. "A2"                 */
    const char *name;    /**< Short human label, e.g. "Boost"             */
    const char *unit;    /**< Unit suffix, e.g. "bar"                     */
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
 * Display index 0 is the first non-reserved input, so the returned descriptor
 * carries its own input_index for indexing into the sample.
 * @return NULL if @p index is out of range.
 */
const kdl_analog_desc_t *kdl_channels_analog(uint8_t index);

/**
 * @brief Shared unit suffix for every thermocouple channel.
 *
 * The MAX31855 is a Type K part, so the unit is fixed at compile time; the
 * degree sign is present in LVGL's built-in Montserrat faces.
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
