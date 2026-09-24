#include "settings_service.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#define SETTINGS_NAMESPACE         "kdl_settings"
#define SETTINGS_KEY_ACQ_PERIOD_MS "acq_period_ms"
#define SETTINGS_KEY_BRIGHTNESS    "brightness"
#define SETTINGS_KEY_GRAPH_WINDOW  "graph_window_s"

static const char *TAG = "settings";

const uint16_t settings_graph_window_presets_s[SETTINGS_GRAPH_WINDOW_PRESET_COUNT] = {
    10U, 20U, 30U, 60U, 120U, 300U, 600U,
};

/* Cached in RAM so the acquisition loop can read the period every cycle
 * without touching NVS. A naturally aligned 32-bit scalar is read/written
 * atomically on this core, so a concurrent set() from the GUI task can never
 * be observed half-written -- no mutex needed for this single value. */
static volatile uint32_t s_acq_period_ms = SETTINGS_ACQ_PERIOD_MS_DEFAULT;
static volatile uint8_t s_brightness_percent = SETTINGS_BRIGHTNESS_DEFAULT;
static volatile uint16_t s_graph_window_s = SETTINGS_GRAPH_WINDOW_S_DEFAULT;
static bool s_initialized;

uint32_t settings_service_normalize_acquisition_period_ms(uint32_t period_ms)
{
    if (period_ms < SETTINGS_ACQ_PERIOD_MS_MIN) {
        period_ms = SETTINGS_ACQ_PERIOD_MS_MIN;
    } else if (period_ms > SETTINGS_ACQ_PERIOD_MS_MAX) {
        period_ms = SETTINGS_ACQ_PERIOD_MS_MAX;
    }

    /* pdMS_TO_TICKS() truncates to whole ticks (10 ms at CONFIG_FREERTOS_HZ=100),
     * so round here too: the stored value is then exactly the applied one
     * instead of a request the scheduler silently cannot honour. */
    period_ms -= period_ms % (uint32_t)portTICK_PERIOD_MS;
    if (period_ms < SETTINGS_ACQ_PERIOD_MS_MIN) {
        period_ms = SETTINGS_ACQ_PERIOD_MS_MIN;
    }

    return period_ms;
}

uint8_t settings_service_normalize_brightness_percent(uint8_t percent)
{
    if (percent < SETTINGS_BRIGHTNESS_MIN) {
        percent = SETTINGS_BRIGHTNESS_MIN;
    } else if (percent > SETTINGS_BRIGHTNESS_MAX) {
        percent = SETTINGS_BRIGHTNESS_MAX;
    }

    percent -= percent % (uint8_t)SETTINGS_BRIGHTNESS_STEP;
    if (percent < SETTINGS_BRIGHTNESS_MIN) {
        percent = SETTINGS_BRIGHTNESS_MIN;
    }

    return percent;
}

uint16_t settings_service_normalize_graph_window_s(uint16_t window_s)
{
    uint16_t best = settings_graph_window_presets_s[0];
    uint16_t best_distance = UINT16_MAX;
    for (size_t index = 0; index < SETTINGS_GRAPH_WINDOW_PRESET_COUNT; ++index) {
        const uint16_t preset = settings_graph_window_presets_s[index];
        const uint16_t distance = (preset > window_s) ? (uint16_t)(preset - window_s)
                                                      : (uint16_t)(window_s - preset);
        if (distance < best_distance) {
            best = preset;
            best_distance = distance;
        }
    }
    return best;
}

esp_err_t settings_service_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* First boot: the namespace is created lazily on the first write. */
        s_initialized = true;
        ESP_LOGI(TAG, "no stored settings yet, using defaults (acq period %" PRIu32 " ms)",
                 s_acq_period_ms);
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open failed");

    uint32_t stored_period_ms = 0;
    err = nvs_get_u32(handle, SETTINGS_KEY_ACQ_PERIOD_MS, &stored_period_ms);

    uint8_t stored_brightness = 0;
    esp_err_t brightness_err = nvs_get_u8(handle, SETTINGS_KEY_BRIGHTNESS, &stored_brightness);

    uint16_t stored_window_s = 0;
    esp_err_t window_err = nvs_get_u16(handle, SETTINGS_KEY_GRAPH_WINDOW, &stored_window_s);
    nvs_close(handle);

    if (brightness_err == ESP_OK) {
        s_brightness_percent = settings_service_normalize_brightness_percent(stored_brightness);
    } else if (brightness_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "failed to read brightness, using default: %s", esp_err_to_name(brightness_err));
    }

    if (window_err == ESP_OK) {
        s_graph_window_s = settings_service_normalize_graph_window_s(stored_window_s);
    } else if (window_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "failed to read graph window, using default: %s", esp_err_to_name(window_err));
    }

    if (err == ESP_OK) {
        /* Normalise on read as well: bounds may have changed across a firmware
         * update, and a stored out-of-range value must not reach the scheduler. */
        s_acq_period_ms = settings_service_normalize_acquisition_period_ms(stored_period_ms);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "failed to read acquisition period, using default: %s", esp_err_to_name(err));
    }

    s_initialized = true;
    ESP_LOGI(TAG, "settings ready (acq period %" PRIu32 " ms, graph window %u s)",
             s_acq_period_ms, (unsigned)s_graph_window_s);
    return ESP_OK;
}

uint32_t settings_service_get_acquisition_period_ms(void)
{
    return s_acq_period_ms;
}

esp_err_t settings_service_set_acquisition_period_ms(uint32_t period_ms)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "settings not initialized");

    uint32_t normalized = settings_service_normalize_acquisition_period_ms(period_ms);
    if (normalized == s_acq_period_ms) {
        /* Nothing to do -- skip the flash write so repeated "set same value"
         * calls from the UI cannot wear out the NVS partition. */
        return ESP_OK;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open failed");

    esp_err_t err = nvs_set_u32(handle, SETTINGS_KEY_ACQ_PERIOD_MS, normalized);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_RETURN_ON_ERROR(err, TAG, "failed to persist acquisition period");

    s_acq_period_ms = normalized;
    ESP_LOGI(TAG, "acquisition period set to %" PRIu32 " ms", normalized);
    return ESP_OK;
}

uint8_t settings_service_get_brightness_percent(void)
{
    return s_brightness_percent;
}

esp_err_t settings_service_set_brightness_percent(uint8_t percent)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "settings not initialized");

    uint8_t normalized = settings_service_normalize_brightness_percent(percent);
    if (normalized == s_brightness_percent) {
        return ESP_OK;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open failed");

    esp_err_t err = nvs_set_u8(handle, SETTINGS_KEY_BRIGHTNESS, normalized);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_RETURN_ON_ERROR(err, TAG, "failed to persist brightness");

    s_brightness_percent = normalized;
    ESP_LOGI(TAG, "brightness set to %u %%", (unsigned)normalized);
    return ESP_OK;
}

uint16_t settings_service_get_graph_window_s(void)
{
    return s_graph_window_s;
}

esp_err_t settings_service_set_graph_window_s(uint16_t window_s)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "settings not initialized");

    uint16_t normalized = settings_service_normalize_graph_window_s(window_s);
    if (normalized == s_graph_window_s) {
        return ESP_OK;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open failed");

    esp_err_t err = nvs_set_u16(handle, SETTINGS_KEY_GRAPH_WINDOW, normalized);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_RETURN_ON_ERROR(err, TAG, "failed to persist graph window");

    s_graph_window_s = normalized;
    ESP_LOGI(TAG, "graph window set to %u s", (unsigned)normalized);
    return ESP_OK;
}
