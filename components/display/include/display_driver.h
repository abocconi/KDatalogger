#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

/**
 * @brief Bring up the ST7796 panel (SPI3 bus, backlight) and the LVGL port.
 *
 * Must be called once before any other display_driver_* function or before
 * touching the LVGL display returned by display_driver_get_lvgl_display().
 */
esp_err_t display_driver_init(void);

/**
 * @brief Set backlight brightness.
 *
 * @param percent 0-100, clamped internally.
 */
esp_err_t display_driver_set_backlight(uint8_t percent);

/**
 * @brief Lock the LVGL context for thread-safe access from tasks other than
 *        the internal LVGL task owned by esp_lvgl_port.
 *
 * @param timeout_ms 0 waits forever, matching lvgl_port_lock() semantics.
 * @return true if the lock was acquired.
 */
bool display_driver_lock(uint32_t timeout_ms);

/** @brief Release the lock acquired with display_driver_lock(). */
void display_driver_unlock(void);

/** @brief LVGL display handle, valid only after display_driver_init() succeeds. */
lv_display_t *display_driver_get_lvgl_display(void);
