#pragma once

#include "lvgl.h"

#include "acquisition_service.h"
#include "data_model.h"
#include "kdl_channels.h"

/**
 * @file kdl_widgets.h
 * @brief Composite readout widgets shared by the KDatalogger pages.
 *
 * Each widget separates its static skeleton (built once in _create) from the
 * few labels that change (rewritten in _update, and only when the rendered
 * text actually differs). Every child has a fixed size, so an update never
 * reflows the parent and the invalidated region stays limited to the glyphs
 * that changed -- the difference between a 4 ms and a 32 ms refresh of the
 * main page.
 */

/** @brief One thermocouple readout: name, reading, fill bar, session extremes. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *value;
    lv_obj_t *bar_fill;
    lv_obj_t *extremes;
    const kdl_thermocouple_desc_t *desc;
    int32_t bar_percent; /**< Last applied fill width, to skip redundant writes */
} kdl_probe_card_t;

/** @brief One analog readout: name and scaled reading with its unit. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *value;
    const kdl_analog_desc_t *desc;
} kdl_analog_cell_t;

/**
 * @brief Build a thermocouple card under @p parent.
 *
 * @param card   Receives the widget handles; owned by the caller.
 * @param desc   Channel descriptor, kept by reference for the widget's life.
 */
void kdl_probe_card_create(kdl_probe_card_t *card, lv_obj_t *parent,
                           const kdl_thermocouple_desc_t *desc,
                           int32_t width, int32_t height);

/**
 * @brief Refresh a thermocouple card from the latest sample.
 *
 * @param extremes May be NULL, in which case the min/max line is left blank.
 * @param index    Thermocouple channel index this card renders.
 */
void kdl_probe_card_update(kdl_probe_card_t *card,
                           const kdl_sensor_sample_t *sample,
                           const kdl_sensor_extremes_t *extremes,
                           uint8_t index);

/** @brief Build an analog cell under @p parent. */
void kdl_analog_cell_create(kdl_analog_cell_t *cell, lv_obj_t *parent,
                            const kdl_analog_desc_t *desc,
                            int32_t width, int32_t height);

/** @brief Refresh an analog cell from the latest sample. */
void kdl_analog_cell_update(kdl_analog_cell_t *cell, const kdl_sensor_sample_t *sample);

/**
 * @brief Write @p text into @p label only if it differs from what is shown.
 *
 * lv_label_set_text() invalidates unconditionally, so writing an unchanged
 * string still costs a repaint of the label's area on every tick.
 */
void kdl_widget_set_text(lv_obj_t *label, const char *text);
