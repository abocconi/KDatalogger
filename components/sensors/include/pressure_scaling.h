#pragma once

#include <stdbool.h>

/**
 * @file pressure_scaling.h
 * @brief Voltage-output pressure sensor: linear scaling, wiring faults, zero.
 *
 * Pure arithmetic, no ESP-IDF dependency, so it builds and runs in the host
 * unit tests. Every voltage here is at the sensor terminal, i.e. after the
 * board's input divider has been compensated.
 */

/** Smallest output span accepted in a configuration, in volts. */
#define PRESSURE_SCALING_MIN_SPAN_V    0.5f
/** Fault band beyond each end of the output range, as a fraction of the span.
 *  0.0625 is the usual 0.25 V of a 0.5-4.5 V sensor. */
#define PRESSURE_SCALING_FAULT_MARGIN  0.0625f
/** Largest zero correction accepted, as a fraction of the output span. */
#define PRESSURE_SCALING_ZERO_WINDOW   0.05f
/** Largest pressure magnitude accepted in a configuration, in bar. */
#define PRESSURE_SCALING_MAX_ABS_BAR   2000.0f

/** @brief One sensor's transfer function, straight from its datasheet. */
typedef struct {
    bool enabled;  /**< A sensor is wired to this input              */
    float v_min;   /**< Output at p_min, volts                         */
    float v_max;   /**< Output at p_max, volts                         */
    float p_min;   /**< Pressure at v_min, bar                         */
    float p_max;   /**< Pressure at v_max, bar                         */
} pressure_sensor_cfg_t;

typedef enum {
    PRESSURE_STATUS_OK = 0,
    PRESSURE_STATUS_DISABLED,   /**< No sensor configured on the input          */
    PRESSURE_STATUS_FAULT_LOW,  /**< Below the range: open wire or short to GND */
    PRESSURE_STATUS_FAULT_HIGH, /**< Above the range or input saturated         */
} pressure_status_t;

/**
 * @brief Whether @p cfg is usable on a board that measures up to @p input_max_v.
 *
 * Checks ordering, the minimum span, and that the whole output range fits
 * the input: a 0-10 V sensor on a 0-5 V input is rejected here rather than
 * reading half its range as a fault.
 */
bool pressure_scaling_cfg_is_valid(const pressure_sensor_cfg_t *cfg, float input_max_v);

/**
 * @brief Convert a sensor voltage to bar.
 *
 * Faults are judged on the raw voltage, the zero correction is applied to
 * the conversion only. A reading between the low fault threshold and v_min
 * is clamped to p_min: noise around zero is not shown as a negative
 * pressure the sensor cannot measure. Above v_max the line is extrapolated,
 * so an over-range pressure still shows up.
 *
 * @param sensor_v   Voltage at the sensor terminal.
 * @param zero_v     Zero correction from pressure_scaling_compute_zero().
 * @param saturated  The ADC input is at full scale: reported as FAULT_HIGH.
 * @param out_bar    Written only when the status is PRESSURE_STATUS_OK.
 */
pressure_status_t pressure_scaling_convert(const pressure_sensor_cfg_t *cfg, float sensor_v,
                                           float zero_v, bool saturated, float *out_bar);

/**
 * @brief Zero correction that makes the current reading 0 bar.
 *
 * Refused when 0 bar is outside the sensor range, when the reading is a
 * fault, or when the correction exceeds PRESSURE_SCALING_ZERO_WINDOW of the
 * span. The window is what stops a zero taken under pressure -- or on an
 * absolute sensor, which reads ~1 bar at rest -- from being stored.
 *
 * @return true and @p out_zero_v set on success.
 */
bool pressure_scaling_compute_zero(const pressure_sensor_cfg_t *cfg, float sensor_v,
                                   float *out_zero_v);

/**
 * @brief Digits after the decimal point that keep @p bar within four
 *        characters: 2 below 10 bar, 1 below 100, none above.
 */
int pressure_scaling_display_decimals(float bar);
