#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "data_model.h"
#include "pressure_scaling.h"
#include "tach_math.h"

/**
 * @file sensor_config_parse.h
 * @brief Parser and template writer for the sensor configuration file.
 *
 * Kept apart from sensor_config.c, which owns the file and the locking, so it
 * builds on the host for the unit tests. Application code uses
 * sensor_config.h instead.
 *
 * The format is a small INI dialect written for people editing it in
 * Notepad: sections [IN3]..[IN7] (the terminal markings, from
 * data_model_analog_channels) and [GIRI]; "key = value" lines; ';' or '#'
 * starts a comment, also after a value; decimals with a comma or a dot; a
 * unit after a number ("4,5 V") is tolerated. Keys and section names are
 * case-insensitive.
 */

/** @brief Every setting read from the configuration file. */
typedef struct {
    pressure_sensor_cfg_t pressure[DATA_MODEL_ANALOG_INPUT_COUNT]; /**< Index-aligned with the sample */
    tach_cfg_t tach;
} sensor_config_t;

/** Section index of [GIRI], after the pressure channels. */
#define SENSOR_CONFIG_SECTION_TACH  DATA_MODEL_ANALOG_INPUT_COUNT
#define SENSOR_CONFIG_SECTION_COUNT (DATA_MODEL_ANALOG_INPUT_COUNT + 1)

typedef struct {
    sensor_config_t config;
    int section;              /**< Current section; -1 before any, -2 unknown one  */
    uint32_t section_line[SENSOR_CONFIG_SECTION_COUNT]; /**< Header line, 0 = absent */
    bool section_error[SENSOR_CONFIG_SECTION_COUNT];    /**< A line in it was rejected */
    uint32_t error_count;
    uint32_t first_error_line; /**< 0 = no error */
} sensor_config_parser_t;

/**
 * @brief Factory configuration: pressure inputs off, tachometer on at one
 *        pulse per revolution.
 *
 * Pressure inputs start disabled because the sensors are chosen by the
 * user: an enabled input with nothing wired would read as a fault. An
 * unwired tachometer just reads 0 rpm.
 */
void sensor_config_defaults(sensor_config_t *out);

/** @brief Reset @p parser to the defaults, ready for the first line. */
void sensor_config_parser_begin(sensor_config_parser_t *parser);

/**
 * @brief Parse one line. @p line is modified in place (trimmed, split).
 *
 * A rejected line counts as an error and marks its section: the section is
 * then disabled by sensor_config_parser_end(), since a half-read sensor
 * scaling would show a wrong pressure with nothing to flag it.
 */
void sensor_config_parser_feed(sensor_config_parser_t *parser, char *line, uint32_t line_no);

/** @brief Record an error on @p line_no that the caller detected (e.g. an over-long line). */
void sensor_config_parser_error(sensor_config_parser_t *parser, uint32_t line_no);

/**
 * @brief Validate what was read and disable every section that failed.
 *
 * @param input_max_v Highest sensor voltage the board measures.
 */
void sensor_config_parser_end(sensor_config_parser_t *parser, float input_max_v);

/**
 * @brief Write a commented configuration file holding @p config.
 *
 * UTF-8 with a byte order mark and CRLF line ends, like the log files, so
 * Notepad shows the accented letters and the line breaks.
 *
 * @return 0 on success, a negative value if a write failed.
 */
int sensor_config_write_template(FILE *file, const sensor_config_t *config, float input_max_v);
