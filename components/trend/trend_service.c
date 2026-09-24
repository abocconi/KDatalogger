#include "trend_service.h"

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "settings_service.h"

#define TREND_LOCK_TIMEOUT_MS 50

_Static_assert(TREND_MAX_POINTS > 0U && TREND_MAX_POINTS <= UINT16_MAX,
               "point indices are stored in uint16_t");
_Static_assert(TREND_MIN_SAMPLES > 0U, "a window must span at least one sample");
/* Largest products in trend_compute_geometry(), with every setting at its
 * bound, must fit the 32-bit arithmetic used there. */
_Static_assert((uint64_t)UINT16_MAX * 1000U <= UINT32_MAX, "window in ms overflows");
_Static_assert((uint64_t)TREND_MAX_POINTS * SETTINGS_ACQ_PERIOD_MS_MAX <= UINT32_MAX,
               "full-resolution span overflows");
_Static_assert((uint64_t)TREND_MIN_SAMPLES * SETTINGS_ACQ_PERIOD_MS_MAX <= UINT32_MAX,
               "minimum window overflows");

static const char *TAG = "trend";

typedef struct {
    uint32_t period_ms;         /**< Acquisition period this geometry was cut for */
    uint16_t requested_s;       /**< Window setting this geometry was cut for */
    uint32_t window_ms;         /**< Effective window: requested, widened if needed */
    uint16_t samples_per_point;
    uint16_t point_count;
} trend_geometry_t;

static SemaphoreHandle_t s_mutex;
static trend_geometry_t s_geometry;
/* Ring of point_count slots per channel; s_head is the slot being filled. */
static int16_t s_points_c[DATA_MODEL_THERMOCOUPLE_COUNT][TREND_MAX_POINTS];
static uint16_t s_head;
static uint16_t s_filled;
/** Samples folded into s_head so far; 0 means the next sample opens a slot. */
static uint16_t s_bucket_samples;
/* Written under s_mutex, read lock-free by trend_service_get_seq(): a naturally
 * aligned 32-bit scalar cannot be observed half-written on this core. */
static volatile uint32_t s_seq;

static uint32_t trend_div_ceil(uint32_t numerator, uint32_t denominator)
{
    return (numerator + denominator - 1U) / denominator;
}

static trend_geometry_t trend_compute_geometry(uint32_t period_ms, uint16_t requested_s)
{
    trend_geometry_t geometry = {
        .period_ms = period_ms,
        .requested_s = requested_s,
    };

    /* Settings never hand out a zero period; guard the divisions anyway. */
    const uint32_t safe_period_ms = (period_ms > 0U) ? period_ms : 1U;

    uint32_t window_ms = (uint32_t)requested_s * 1000U;
    const uint32_t min_window_ms = TREND_MIN_SAMPLES * safe_period_ms;
    if (window_ms < min_window_ms) {
        window_ms = min_window_ms;
    }

    /* Fewest samples per point that fit the window in TREND_MAX_POINTS, then
     * as many points as that takes -- rounded up, so the trace spans at least
     * the window shown in the label. */
    const uint32_t samples_per_point =
        trend_div_ceil(window_ms, TREND_MAX_POINTS * safe_period_ms);
    uint32_t point_count = trend_div_ceil(window_ms, samples_per_point * safe_period_ms);
    if (point_count > TREND_MAX_POINTS) {
        point_count = TREND_MAX_POINTS;
    }

    geometry.window_ms = window_ms;
    geometry.samples_per_point = (uint16_t)samples_per_point;
    geometry.point_count = (uint16_t)point_count;
    return geometry;
}

/** Caller must hold s_mutex. */
static void trend_clear_locked(void)
{
    for (size_t channel = 0; channel < DATA_MODEL_THERMOCOUPLE_COUNT; ++channel) {
        for (size_t point = 0; point < TREND_MAX_POINTS; ++point) {
            s_points_c[channel][point] = TREND_NO_DATA;
        }
    }
    s_head = 0;
    s_filled = 0;
    s_bucket_samples = 0;
    ++s_seq;
}

/** Caller must hold s_mutex. Returns true if the geometry changed. */
static bool trend_sync_geometry_locked(void)
{
    const uint32_t period_ms = settings_service_get_acquisition_period_ms();
    const uint16_t requested_s = settings_service_get_graph_window_s();
    if (period_ms == s_geometry.period_ms && requested_s == s_geometry.requested_s) {
        return false;
    }

    s_geometry = trend_compute_geometry(period_ms, requested_s);
    trend_clear_locked();
    return true;
}

static void trend_log_geometry(void)
{
    ESP_LOGI(TAG, "window %" PRIu32 " ms at %" PRIu32 " ms: %u points of %u samples",
             s_geometry.window_ms, s_geometry.period_ms,
             (unsigned)s_geometry.point_count, (unsigned)s_geometry.samples_per_point);
}

/** Whole degrees, rounded, kept strictly above TREND_NO_DATA. */
static int16_t trend_to_point(float value_c)
{
    const float rounded = (value_c >= 0.0f) ? (value_c + 0.5f) : (value_c - 0.5f);
    if (rounded >= (float)INT16_MAX) {
        return INT16_MAX;
    }
    if (rounded <= (float)(TREND_NO_DATA + 1)) {
        return (int16_t)(TREND_NO_DATA + 1);
    }
    return (int16_t)rounded;
}

esp_err_t trend_service_init(void)
{
    if (s_mutex != NULL) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_mutex != NULL, ESP_ERR_NO_MEM, TAG, "failed to create mutex");

    /* No other task can reach the buffer before init returns, but take the
     * lock anyway so the *_locked helpers keep their contract. */
    (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_geometry = trend_compute_geometry(settings_service_get_acquisition_period_ms(),
                                        settings_service_get_graph_window_s());
    trend_clear_locked();
    xSemaphoreGive(s_mutex);

    trend_log_geometry();
    return ESP_OK;
}

esp_err_t trend_service_add_sample(const kdl_sensor_sample_t *sample)
{
    ESP_RETURN_ON_FALSE(sample != NULL, ESP_ERR_INVALID_ARG, TAG, "sample is null");
    ESP_RETURN_ON_FALSE(s_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "trend not initialized");

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(TREND_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Checked per sample rather than pushed by the settings page: settings
     * stays unaware of its consumers, and the change lands on the first sample
     * taken with the new period. */
    const bool geometry_changed = trend_sync_geometry_locked();

    if (s_bucket_samples == 0U) {
        if (s_filled > 0U) {
            s_head = (uint16_t)((s_head + 1U) % s_geometry.point_count);
        }
        for (size_t channel = 0; channel < DATA_MODEL_THERMOCOUPLE_COUNT; ++channel) {
            s_points_c[channel][s_head] = TREND_NO_DATA;
        }
        if (s_filled < s_geometry.point_count) {
            ++s_filled;
        }
    }

    for (size_t channel = 0; channel < DATA_MODEL_THERMOCOUPLE_COUNT; ++channel) {
        const uint16_t bit = (uint16_t)(1U << channel);
        const float value_c = sample->thermocouples_c[channel];
        if ((sample->thermocouple_valid_mask & bit) == 0U || !isfinite(value_c)) {
            continue;
        }

        /* TREND_NO_DATA is below every stored reading, so an empty slot needs
         * no special case: the first valid sample always wins. */
        const int16_t point = trend_to_point(value_c);
        if (point > s_points_c[channel][s_head]) {
            s_points_c[channel][s_head] = point;
        }
    }

    ++s_bucket_samples;
    if (s_bucket_samples >= s_geometry.samples_per_point) {
        s_bucket_samples = 0;
    }
    ++s_seq;

    xSemaphoreGive(s_mutex);

    if (geometry_changed) {
        trend_log_geometry();
    }
    return ESP_OK;
}

esp_err_t trend_service_reset(void)
{
    ESP_RETURN_ON_FALSE(s_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "trend not initialized");

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(TREND_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const bool geometry_changed = trend_sync_geometry_locked();
    if (!geometry_changed) {
        trend_clear_locked();
    }
    xSemaphoreGive(s_mutex);

    if (geometry_changed) {
        trend_log_geometry();
    }
    return ESP_OK;
}

uint32_t trend_service_get_seq(void)
{
    return s_seq;
}

esp_err_t trend_service_get_snapshot(trend_snapshot_t *out)
{
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "out is null");
    ESP_RETURN_ON_FALSE(s_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "trend not initialized");

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(TREND_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const uint16_t count = s_geometry.point_count;
    out->seq = s_seq;
    out->window_s = trend_div_ceil(s_geometry.window_ms, 1000U);
    out->point_count = count;
    out->filled = s_filled;

    /* The oldest slot sits s_filled - 1 positions behind the head. */
    const uint16_t oldest = (uint16_t)((s_head + count + 1U - s_filled) % count);
    for (size_t channel = 0; channel < DATA_MODEL_THERMOCOUPLE_COUNT; ++channel) {
        for (uint16_t point = 0; point < TREND_MAX_POINTS; ++point) {
            out->points_c[channel][point] =
                (point < s_filled) ? s_points_c[channel][(oldest + point) % count]
                                   : TREND_NO_DATA;
        }
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}
