#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @file tachometer.h
 * @brief Pulse period capture on one GPIO, for the engine speed.
 *
 * The MCPWM capture unit timestamps every rising edge in hardware (APB
 * clock, 12.5 ns), so interrupt latency does not show up in the period. The
 * ISR only accumulates the periods; the conversion to rpm is done by the
 * caller once per acquisition cycle (see tach_math.h).
 *
 * The ISR is built to run with the flash cache disabled
 * (CONFIG_MCPWM_ISR_CACHE_SAFE): otherwise every flash erase of the logger
 * would hold it off, a missed edge would merge two periods into one, and the
 * log would show the speed halving for a sample.
 */

/** @brief Periods captured since the previous tachometer_take(). */
typedef struct {
    uint32_t periods;       /**< Accepted periods                                */
    uint64_t ticks_sum;     /**< Their total length, capture timer ticks         */
    uint32_t resolution_hz; /**< Capture timer resolution                        */
    uint32_t glitches;      /**< Edges dropped as shorter than the min period    */
    bool edge_seen;         /**< At least one edge since init                    */
    int64_t last_edge_us;   /**< esp_timer time of the last accepted edge        */
} tachometer_reading_t;

/**
 * @brief Start capturing rising edges on @p gpio_num.
 *
 * The pin is left without internal pulls: the board's input divider sets
 * the idle level.
 */
esp_err_t tachometer_init(int gpio_num);

/**
 * @brief Set the accepted period range.
 *
 * An edge closer than @p min_period_us to the previous accepted one is a
 * glitch and is ignored, so the next real edge is still measured from the
 * right reference. A period longer than @p max_period_us is not averaged:
 * it spans a stop, and the edge just restarts the measurement. Cheap enough
 * to call every cycle.
 */
esp_err_t tachometer_set_period_limits(uint32_t min_period_us, uint32_t max_period_us);

/** @brief Copy and clear the periods accumulated since the previous call. */
esp_err_t tachometer_take(tachometer_reading_t *out);

bool tachometer_is_initialized(void);
