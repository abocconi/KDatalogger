#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

/**
 * @file timekeeping.h
 * @brief Software wall clock, set by the operator, with no RTC hardware.
 *
 * The board carries no real-time clock. Wall time is entered once per power
 * cycle from the settings page and then maintained by the SoC's own timer for
 * as long as the ESP32-S3 stays powered; it is lost at power-down.
 *
 * Two consequences shape this API:
 *  - @ref timekeeping_is_valid is RAM-only state, reset on every boot. It is
 *    deliberately NOT persisted: restoring a "valid" flag would make the
 *    firmware confidently report a time that is wrong by however long the
 *    logger was switched off, which is worse than reporting no time at all.
 *  - The last value the operator entered IS persisted, but only as the
 *    pre-filled default of the setup mask, so a restart means confirming a
 *    date rather than typing one from scratch. It is written once per set,
 *    never periodically, so NVS wear is a non-issue.
 *
 * Everything is handled in a fixed UTC0 zone: the value entered is the value
 * read back, with no DST or timezone translation in between. The clock shown
 * on screen is whatever the operator called "now".
 *
 * If a backup battery is ever fitted to the SoC's RTC domain, wall time will
 * survive power-down with no change to this interface.
 */

/**
 * @brief Load the last operator-entered time from NVS as the setup default
 *        and fix the process timezone to UTC0.
 *
 * Always leaves the clock marked invalid: a fresh boot never knows the time.
 * A missing NVS key is not an error.
 */
esp_err_t timekeeping_init(void);

/** @brief Whether the operator has set the clock since this power-up. */
bool timekeeping_is_valid(void);

/**
 * @brief Apply and persist an operator-entered wall time.
 *
 * Marks the clock valid and stores the value as the next setup default.
 *
 * @param local Broken-down time; only the date and time fields are read.
 */
esp_err_t timekeeping_set(const struct tm *local);

/**
 * @brief Current wall time.
 *
 * @param out Receives the broken-down time. When the clock has never been
 *            set this is still filled with the setup default, so callers
 *            must gate display on @ref timekeeping_is_valid.
 */
void timekeeping_get(struct tm *out);

/**
 * @brief Value to pre-fill the date/time setup mask with: the last time the
 *        operator entered, or a fixed build-era default on a virgin device.
 */
void timekeeping_get_setup_default(struct tm *out);

/**
 * @brief FAT timestamp for the current wall time, in the packed format
 *        get_fattime() must return.
 *
 * Returns a fixed sentinel while the clock is invalid, so log files keep a
 * stable placeholder date rather than a random one.
 */
unsigned int timekeeping_fattime(void);
