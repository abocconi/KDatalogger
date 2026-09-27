#include "tach_math.h"

#include <math.h>
#include <stddef.h>

#define TACH_MATH_US_PER_MIN 60000000.0f

bool tach_math_cfg_is_valid(const tach_cfg_t *cfg)
{
    if (cfg == NULL || isfinite(cfg->pulses_per_rev) == 0) {
        return false;
    }

    return cfg->pulses_per_rev >= TACH_MATH_PPR_MIN
           && cfg->pulses_per_rev <= TACH_MATH_PPR_MAX
           && cfg->rpm_max >= TACH_MATH_RPM_MAX_MIN
           && cfg->rpm_max <= TACH_MATH_RPM_MAX_MAX
           && (float)cfg->rpm_max * cfg->pulses_per_rev / 60.0f <= TACH_MATH_MAX_PULSE_HZ;
}

uint32_t tach_math_min_period_us(const tach_cfg_t *cfg)
{
    if (!tach_math_cfg_is_valid(cfg)) {
        return 0U;
    }
    const float period_us = TACH_MATH_US_PER_MIN / ((float)cfg->rpm_max * cfg->pulses_per_rev);
    return (uint32_t)(period_us * 0.5f);
}

uint32_t tach_math_timeout_us(const tach_cfg_t *cfg)
{
    if (!tach_math_cfg_is_valid(cfg)) {
        return 0U;
    }
    return (uint32_t)(TACH_MATH_US_PER_MIN / (TACH_MATH_RPM_STOPPED * cfg->pulses_per_rev));
}

float tach_math_update(const tach_cfg_t *cfg, float previous_rpm, uint32_t periods,
                       uint64_t ticks_sum, uint32_t resolution_hz, bool edge_seen,
                       int64_t since_last_edge_us)
{
    if (!tach_math_cfg_is_valid(cfg) || !edge_seen || since_last_edge_us < 0
        || since_last_edge_us >= (int64_t)tach_math_timeout_us(cfg)) {
        return 0.0f;
    }

    float estimate = previous_rpm;
    if (periods > 0U && ticks_sum > 0U && resolution_hz > 0U) {
        const double mean_period_s = (double)ticks_sum / (double)periods / (double)resolution_hz;
        estimate = (float)(60.0 / (mean_period_s * (double)cfg->pulses_per_rev));
    }

    /* The next pulse is at least since_last_edge_us away, so the speed is at
     * most one pulse in that time. */
    if (since_last_edge_us > 0) {
        const float ceiling = TACH_MATH_US_PER_MIN
                              / ((float)since_last_edge_us * cfg->pulses_per_rev);
        if (estimate > ceiling) {
            estimate = ceiling;
        }
    }

    return (estimate > 0.0f && isfinite(estimate) != 0) ? estimate : 0.0f;
}
