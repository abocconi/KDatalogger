#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "data_model.h"

/**
 * @file trend_service.h
 * @brief Recent thermocouple history for the graph page.
 *
 * Kept outside the GUI so the history survives page switches and is fed with
 * every acquired sample, not with whatever the GUI tick happens to see.
 *
 * The window is cut into at most TREND_MAX_POINTS points. When it spans more
 * samples than that, consecutive samples are folded into one point keeping
 * the highest reading: with exhaust temperatures the peaks are what matters,
 * and an average would flatten them exactly when the window is wide. Points
 * group a whole number of samples rather than a slice of time, so scheduling
 * jitter can never leave a bucket empty.
 *
 * The geometry follows the acquisition period and the graph window setting;
 * when either changes the history is discarded, since buckets of the old
 * geometry cannot be re-cut into the new one.
 */

/** Most points a trace holds: ~3 px each across the 380 px plot. */
#define TREND_MAX_POINTS  120U

/** Fewest samples a window may span. Below this the trace degrades to a few
 *  segments, so at slow acquisition periods the window is widened instead. */
#define TREND_MIN_SAMPLES 20U

/** Value of a point with no valid reading (probe disconnected for the whole
 *  bucket). Every real reading is stored above it. */
#define TREND_NO_DATA     INT16_MIN

typedef struct {
    uint32_t seq;         /**< Changes whenever any point or the geometry does */
    uint32_t window_s;    /**< Effective window, rounded up to whole seconds */
    uint16_t point_count; /**< Points in a full window */
    uint16_t filled;      /**< Points holding data, <= point_count */
    /** Per channel, oldest first, whole °C. The last point is the bucket still
     *  being filled. Entries from @c filled onwards are TREND_NO_DATA. */
    int16_t points_c[DATA_MODEL_THERMOCOUPLE_COUNT][TREND_MAX_POINTS];
} trend_snapshot_t;

/**
 * @brief Create the lock and size the history from the current settings.
 *
 * Requires settings_service_init() to have run. Idempotent.
 */
esp_err_t trend_service_init(void);

/**
 * @brief Fold one acquired sample into the history.
 *
 * Called by the acquisition task once per cycle. Only channels flagged in the
 * sample's valid mask contribute.
 *
 * @return ESP_OK, ESP_ERR_INVALID_ARG on NULL, ESP_ERR_INVALID_STATE if not
 *         initialized, ESP_ERR_TIMEOUT if the lock could not be taken in time
 *         (the sample is dropped).
 */
esp_err_t trend_service_add_sample(const kdl_sensor_sample_t *sample);

/**
 * @brief Discard the history, e.g. after a gap in acquisition.
 *
 * @return ESP_OK, ESP_ERR_INVALID_STATE if not initialized, ESP_ERR_TIMEOUT.
 */
esp_err_t trend_service_reset(void);

/**
 * @brief Current change counter, lock-free.
 *
 * Lets a consumer skip the snapshot copy when nothing has changed since the
 * last one (compare against trend_snapshot_t.seq).
 */
uint32_t trend_service_get_seq(void);

/**
 * @brief Copy the history, oldest point first.
 *
 * @return ESP_OK, ESP_ERR_INVALID_ARG on NULL, ESP_ERR_INVALID_STATE if not
 *         initialized, ESP_ERR_TIMEOUT if the lock could not be taken in time.
 */
esp_err_t trend_service_get_snapshot(trend_snapshot_t *out);
