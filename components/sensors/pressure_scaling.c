#include "pressure_scaling.h"

#include <math.h>
#include <stddef.h>

static bool pressure_scaling_is_finite(float value)
{
    return isfinite(value) != 0;
}

bool pressure_scaling_cfg_is_valid(const pressure_sensor_cfg_t *cfg, float input_max_v)
{
    if (cfg == NULL
        || !pressure_scaling_is_finite(cfg->v_min) || !pressure_scaling_is_finite(cfg->v_max)
        || !pressure_scaling_is_finite(cfg->p_min) || !pressure_scaling_is_finite(cfg->p_max)) {
        return false;
    }

    return cfg->v_min >= 0.0f
           && cfg->v_max - cfg->v_min >= PRESSURE_SCALING_MIN_SPAN_V
           && cfg->v_max <= input_max_v
           && cfg->p_max > cfg->p_min
           && fabsf(cfg->p_min) <= PRESSURE_SCALING_MAX_ABS_BAR
           && fabsf(cfg->p_max) <= PRESSURE_SCALING_MAX_ABS_BAR;
}

static float pressure_scaling_margin_v(const pressure_sensor_cfg_t *cfg)
{
    return (cfg->v_max - cfg->v_min) * PRESSURE_SCALING_FAULT_MARGIN;
}

pressure_status_t pressure_scaling_convert(const pressure_sensor_cfg_t *cfg, float sensor_v,
                                           float zero_v, bool saturated, float *out_bar)
{
    if (cfg == NULL || out_bar == NULL || !cfg->enabled) {
        return PRESSURE_STATUS_DISABLED;
    }

    const float margin_v = pressure_scaling_margin_v(cfg);
    if (saturated || !pressure_scaling_is_finite(sensor_v) || sensor_v > cfg->v_max + margin_v) {
        return PRESSURE_STATUS_FAULT_HIGH;
    }
    /* Only a live zero can tell a broken wire from 0 bar: a 0-x V sensor
     * reads 0 V either way, so it has no low fault. */
    if (cfg->v_min >= margin_v && sensor_v < cfg->v_min - margin_v) {
        return PRESSURE_STATUS_FAULT_LOW;
    }

    const float corrected_v = sensor_v - (pressure_scaling_is_finite(zero_v) ? zero_v : 0.0f);
    if (corrected_v <= cfg->v_min) {
        *out_bar = cfg->p_min;
        return PRESSURE_STATUS_OK;
    }

    const float slope = (cfg->p_max - cfg->p_min) / (cfg->v_max - cfg->v_min);
    *out_bar = cfg->p_min + (corrected_v - cfg->v_min) * slope;
    return PRESSURE_STATUS_OK;
}

bool pressure_scaling_compute_zero(const pressure_sensor_cfg_t *cfg, float sensor_v,
                                   float *out_zero_v)
{
    if (cfg == NULL || out_zero_v == NULL || !cfg->enabled
        || cfg->p_min > 0.0f || cfg->p_max < 0.0f) {
        return false;
    }

    float unused_bar = 0.0f;
    if (pressure_scaling_convert(cfg, sensor_v, 0.0f, false, &unused_bar) != PRESSURE_STATUS_OK) {
        return false;
    }

    const float span_v = cfg->v_max - cfg->v_min;
    const float zero_bar_v = cfg->v_min + (0.0f - cfg->p_min) * span_v / (cfg->p_max - cfg->p_min);
    const float correction_v = sensor_v - zero_bar_v;
    if (fabsf(correction_v) > span_v * PRESSURE_SCALING_ZERO_WINDOW) {
        return false;
    }

    *out_zero_v = correction_v;
    return true;
}

int pressure_scaling_display_decimals(float bar)
{
    /* A minus sign takes the place of one digit. */
    const float magnitude = fabsf(bar);
    const int sign = (bar < 0.0f) ? 1 : 0;
    if (magnitude < 10.0f) {
        return 2 - sign;
    }
    if (magnitude < 100.0f) {
        return 1 - sign;
    }
    return 0;
}
