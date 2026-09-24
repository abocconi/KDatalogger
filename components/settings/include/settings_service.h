#pragma once

#include <stdint.h>

#include "esp_err.h"

/** Acquisition/logging period bounds, in milliseconds.
 *
 * NOTE: the MAX31855 needs ~70 ms (100 ms worst case) per conversion, so
 * below roughly 150 ms the thermocouple channels start returning repeated
 * or stale readings. The lower bound is deliberately kept at 100 ms for
 * bench testing of the acquisition mechanism itself. */
#define SETTINGS_ACQ_PERIOD_MS_MIN     100U
#define SETTINGS_ACQ_PERIOD_MS_MAX     5000U
#define SETTINGS_ACQ_PERIOD_MS_DEFAULT 500U

/** Backlight bounds, in percent. The floor is deliberately above zero: a
 *  fully dark panel is indistinguishable from a dead one, and the only way
 *  back would be to guess where the keys are. */
#define SETTINGS_BRIGHTNESS_MIN      10U
#define SETTINGS_BRIGHTNESS_MAX      100U
#define SETTINGS_BRIGHTNESS_STEP     10U
#define SETTINGS_BRIGHTNESS_DEFAULT  80U

/** Graph window, in seconds. A preset list rather than a range: the value is
 *  stepped with two keys, and round figures keep the grid readable. The
 *  window actually plotted can be wider at slow acquisition periods, see
 *  trend_service.h. */
#define SETTINGS_GRAPH_WINDOW_PRESET_COUNT 7U
#define SETTINGS_GRAPH_WINDOW_S_DEFAULT    30U

/** Selectable graph windows in seconds, ascending. */
extern const uint16_t settings_graph_window_presets_s[SETTINGS_GRAPH_WINDOW_PRESET_COUNT];

/**
 * @brief Load persisted settings from NVS into the RAM cache.
 *
 * Missing keys (or a missing namespace, i.e. first boot) are not an error:
 * the corresponding setting keeps its default. Requires nvs_flash_init() to
 * have run already.
 */
esp_err_t settings_service_init(void);

/**
 * @brief Current acquisition/logging period in milliseconds.
 *
 * Served from a RAM cache, so it is cheap enough to call once per acquisition
 * cycle to pick up runtime changes.
 */
uint32_t settings_service_get_acquisition_period_ms(void);

/**
 * @brief Validate, persist and apply a new acquisition/logging period.
 *
 * The value is normalised (see settings_service_normalize_acquisition_period_ms)
 * before being stored, so what is persisted is exactly what gets applied.
 * Writing a value equal to the current one is a no-op and does not touch flash.
 */
esp_err_t settings_service_set_acquisition_period_ms(uint32_t period_ms);

/**
 * @brief Clamp to [MIN, MAX] and round down to a whole FreeRTOS tick.
 *
 * Exposed so UI code can show the value that would actually be applied.
 */
uint32_t settings_service_normalize_acquisition_period_ms(uint32_t period_ms);

/**
 * @brief Current backlight setting in percent.
 *
 * Stored here but applied by the caller: this component has no business
 * depending on the display driver, and the value has to survive a display
 * re-init anyway.
 */
uint8_t settings_service_get_brightness_percent(void);

/**
 * @brief Validate, persist and cache a new backlight percentage.
 *
 * Writing the value already in effect is a no-op and does not touch flash.
 */
esp_err_t settings_service_set_brightness_percent(uint8_t percent);

/** @brief Clamp to [MIN, MAX] and round to a whole SETTINGS_BRIGHTNESS_STEP. */
uint8_t settings_service_normalize_brightness_percent(uint8_t percent);

/**
 * @brief Requested graph window in seconds, always one of the presets.
 *
 * Lock-free RAM cache, cheap enough to read once per acquisition cycle.
 */
uint16_t settings_service_get_graph_window_s(void);

/**
 * @brief Validate, persist and cache a new graph window.
 *
 * The value is snapped to the nearest preset before being stored. Writing the
 * value already in effect is a no-op and does not touch flash.
 */
esp_err_t settings_service_set_graph_window_s(uint16_t window_s);

/** @brief Snap to the nearest entry of settings_graph_window_presets_s. */
uint16_t settings_service_normalize_graph_window_s(uint16_t window_s);
