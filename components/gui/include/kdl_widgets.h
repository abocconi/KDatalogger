#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#include "acquisition_service.h"
#include "data_model.h"
#include "kdl_channels.h"

/**
 * @file kdl_widgets.h
 * @brief Composite readout widgets shared by the KDatalogger pages.
 *
 * Each widget separates its static skeleton (built once in _create) from the
 * few objects that change (touched in _update, and only when what they show
 * actually differs). Every child sits at a fixed pixel position inside a
 * fixed-size parent, so an update never reflows anything and the invalidated
 * region stays limited to the glyphs or bar segment that changed -- the
 * difference between a 4 ms and a 32 ms refresh of the main page. State
 * colours are applied only when the alarm level changes: setting a local
 * style invalidates the object even when the value is the same.
 */

/** @brief Cylinder columns in a bank. */
#define KDL_CYL_BANK_COUNT 4U

/** @brief Alarm level of a reading; drives every state colour. */
typedef enum {
    KDL_LEVEL_UNSET = 0, /**< Nothing applied yet: forces the first repaint  */
    KDL_LEVEL_OK,
    KDL_LEVEL_WARN,      /**< At or above the channel's warn threshold       */
    KDL_LEVEL_ALARM,     /**< At or above the channel's alarm threshold      */
    KDL_LEVEL_FAULT,     /**< No usable reading: open probe or MAX31855 fault */
} kdl_level_t;

/** @brief One cylinder column: reading, zoned vertical bar, session peak. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *value;
    lv_obj_t *fill;
    lv_obj_t *peak;
    const kdl_thermocouple_desc_t *desc;
    uint8_t channel;
    kdl_level_t level;
    int32_t fill_h; /**< Last applied fill height, px; -1 = none yet  */
    int32_t peak_y; /**< Last applied peak marker y, px; -1 = hidden  */
} kdl_cyl_column_t;

/**
 * @brief Side-by-side exhaust temperatures on one shared scale, so an
 *        imbalance between cylinders reads as a difference in bar height.
 */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *spread;
    int32_t track_h;
    kdl_cyl_column_t columns[KDL_CYL_BANK_COUNT];
} kdl_cyl_bank_t;

/** @brief One thermocouple tile: name, extremes, reading, zoned bar. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *value;
    lv_obj_t *extremes;
    lv_obj_t *fill;
    const kdl_thermocouple_desc_t *desc;
    uint8_t channel;
    kdl_level_t level;
    int32_t track_w;
    int32_t fill_w; /**< Last applied fill width, px; -1 = none yet */
} kdl_fluid_tile_t;

/** @brief One pressure readout: name, unit and reading. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *value;
    const kdl_analog_desc_t *desc;
    kdl_level_t level; /**< OK, FAULT (sensor out of range) or UNSET (input off) */
    bool applied;      /**< level has been painted at least once */
} kdl_analog_cell_t;

/** @brief Engine speed tile: name, session peak, reading, bar. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *value;
    lv_obj_t *peak;
    lv_obj_t *fill;
    float full_scale;
    int32_t track_w;
    int32_t fill_w; /**< Last applied fill width, px; -1 = none yet  */
    int8_t valid;   /**< Last applied validity; -1 = none yet        */
} kdl_rpm_tile_t;

/**
 * @brief Build a cylinder bank from thermocouple channels
 *        @p first_channel .. @p first_channel + KDL_CYL_BANK_COUNT - 1.
 *
 * The axis is labelled from the first channel's scale; every channel should
 * share it, and a mismatch is logged.
 *
 * @return ESP_ERR_INVALID_ARG on a NULL argument, a channel out of range,
 *         inconsistent thresholds or a size too small for the layout.
 */
esp_err_t kdl_cyl_bank_create(kdl_cyl_bank_t *bank, lv_obj_t *parent, uint8_t first_channel,
                              int32_t width, int32_t height);

/**
 * @brief Refresh a cylinder bank from the latest sample.
 *
 * @param extremes May be NULL, in which case the peak markers are hidden.
 */
void kdl_cyl_bank_update(kdl_cyl_bank_t *bank, const kdl_sensor_sample_t *sample,
                         const kdl_sensor_extremes_t *extremes);

/**
 * @brief Build a thermocouple tile for @p channel.
 *
 * @return ESP_ERR_INVALID_ARG on a NULL argument, a channel out of range,
 *         inconsistent thresholds or a size too small for the layout.
 */
esp_err_t kdl_fluid_tile_create(kdl_fluid_tile_t *tile, lv_obj_t *parent, uint8_t channel,
                                int32_t width, int32_t height);

/**
 * @brief Refresh a thermocouple tile from the latest sample.
 *
 * @param extremes May be NULL, in which case the min/max line shows dashes.
 */
void kdl_fluid_tile_update(kdl_fluid_tile_t *tile, const kdl_sensor_sample_t *sample,
                           const kdl_sensor_extremes_t *extremes);

/**
 * @brief Build an analog cell under @p parent.
 *
 * @return ESP_ERR_INVALID_ARG on a NULL argument or a size too small.
 */
esp_err_t kdl_analog_cell_create(kdl_analog_cell_t *cell, lv_obj_t *parent,
                                 const kdl_analog_desc_t *desc, int32_t width, int32_t height);

/**
 * @brief Refresh an analog cell from the latest sample.
 *
 * Shows the pressure, "ERR" in the alarm colour for a sensor out of its
 * range (wiring fault), dashes for an input with no sensor configured.
 */
void kdl_analog_cell_update(kdl_analog_cell_t *cell, const kdl_sensor_sample_t *sample);

/**
 * @brief Build the engine speed tile.
 *
 * @param full_scale Speed at which the bar is full, rpm.
 * @return ESP_ERR_INVALID_ARG on a NULL argument, a non-positive full scale
 *         or a size too small for the layout.
 */
esp_err_t kdl_rpm_tile_create(kdl_rpm_tile_t *tile, lv_obj_t *parent, float full_scale,
                              int32_t width, int32_t height);

/**
 * @brief Refresh the engine speed tile.
 *
 * @param extremes May be NULL, in which case the peak line shows dashes.
 */
void kdl_rpm_tile_update(kdl_rpm_tile_t *tile, const kdl_sensor_sample_t *sample,
                         const kdl_sensor_extremes_t *extremes);

/**
 * @brief Write @p text into @p label only if it differs from what is shown.
 *
 * lv_label_set_text() invalidates unconditionally, so writing an unchanged
 * string still costs a repaint of the label's area on every tick.
 */
void kdl_widget_set_text(lv_obj_t *label, const char *text);
