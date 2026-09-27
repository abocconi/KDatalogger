#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @file tach_math.h
 * @brief Engine speed from pulse periods. Pure arithmetic, host-testable.
 *
 * Speed comes from the time between pulses, not from counting them: at one
 * pulse per revolution and 1000 rpm a 500 ms cycle sees about 8 pulses, a
 * ±12 % resolution. The periods captured in a cycle are averaged instead.
 */

/** Below this speed the engine counts as stopped; sets the no-pulse timeout. */
#define TACH_MATH_RPM_STOPPED     100.0f
/** Accepted pulses per revolution: 0.5 (camshaft) up to a flywheel ring gear. */
#define TACH_MATH_PPR_MIN         0.5f
#define TACH_MATH_PPR_MAX         200.0f
/** Accepted full-scale speed, rpm. */
#define TACH_MATH_RPM_MAX_MIN     1000U
#define TACH_MATH_RPM_MAX_MAX     20000U
/** Highest pulse rate accepted at full scale: keeps the capture ISR load small. */
#define TACH_MATH_MAX_PULSE_HZ    20000.0f

/** @brief Tachometer settings from the configuration file. */
typedef struct {
    bool enabled;          /**< A sensor is wired to the input                 */
    float pulses_per_rev;  /**< Pulses per crankshaft revolution               */
    uint32_t rpm_max;      /**< Full scale of the bar; sets the glitch filter  */
} tach_cfg_t;

/** @brief Whether @p cfg is inside the accepted ranges. */
bool tach_math_cfg_is_valid(const tach_cfg_t *cfg);

/**
 * @brief Shortest pulse period accepted, in µs: half the period at rpm_max.
 *
 * Anything shorter is a glitch (ringing on a slow edge, ignition noise) and
 * is dropped by the capture driver. A speed up to twice the full scale is
 * still measured.
 */
uint32_t tach_math_min_period_us(const tach_cfg_t *cfg);

/** @brief Pulse period at TACH_MATH_RPM_STOPPED, in µs: past it, speed is 0. */
uint32_t tach_math_timeout_us(const tach_cfg_t *cfg);

/**
 * @brief Speed for one acquisition cycle.
 *
 * With periods captured in the cycle the estimate is their mean; without,
 * the previous value is kept. The estimate is then capped by the speed the
 * time since the last pulse still allows, so a stopping engine winds down
 * on screen instead of freezing at its last reading, and reaches 0 once
 * that time exceeds tach_math_timeout_us().
 *
 * @param previous_rpm        Value returned for the previous cycle.
 * @param periods             Periods captured in this cycle.
 * @param ticks_sum           Their total length, in capture timer ticks.
 * @param resolution_hz       Capture timer resolution.
 * @param edge_seen           At least one pulse since the driver started.
 * @param since_last_edge_us  Time from the last pulse to now.
 */
float tach_math_update(const tach_cfg_t *cfg, float previous_rpm, uint32_t periods,
                       uint64_t ticks_sum, uint32_t resolution_hz, bool edge_seen,
                       int64_t since_last_edge_us);
