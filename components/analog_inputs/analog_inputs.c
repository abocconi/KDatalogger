#include "analog_inputs.h"

#include <string.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_log.h"

#define ANALOG_INPUTS_UNIT_COUNT 2
#define ANALOG_INPUTS_FALLBACK_FULL_SCALE_MV 3300.0f
#define ANALOG_INPUTS_FALLBACK_MAX_RAW 4095.0f
/* Distance from the top code still counted as saturated: the average of a
 * clipped input dithers a few LSB below full scale. */
#define ANALOG_INPUTS_SATURATION_MARGIN_RAW 8

typedef struct {
    bool in_use;
    adc_unit_t unit_id;
    adc_oneshot_unit_handle_t handle;
} analog_unit_state_t;

typedef struct {
    bool configured;
    int gpio_num;
    adc_unit_t unit_id;
    adc_channel_t channel;
    adc_cali_handle_t cali_handle;
    bool calibrated;
} analog_channel_state_t;

static const char *TAG = "analog_inputs";

static analog_inputs_config_t s_config;
static int s_saturation_raw;
static analog_unit_state_t s_units[ANALOG_INPUTS_UNIT_COUNT];
static analog_channel_state_t s_channels[ANALOG_INPUTS_MAX_CHANNELS];
static bool s_initialized;

static int analog_inputs_unit_index(adc_unit_t unit_id)
{
    switch (unit_id) {
    case ADC_UNIT_1:
        return 0;
    case ADC_UNIT_2:
        return 1;
    default:
        return -1;
    }
}

static bool analog_inputs_calibration_init(adc_unit_t unit_id,
                                           adc_channel_t channel,
                                           adc_atten_t atten,
                                           adc_bitwidth_t bitwidth,
                                           adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t err = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit_id,
            .chan = channel,
            .atten = atten,
            .bitwidth = bitwidth,
        };
        err = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        calibrated = (err == ESP_OK);
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit_id,
            .atten = atten,
            .bitwidth = bitwidth,
        };
        err = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        calibrated = (err == ESP_OK);
    }
#endif

    *out_handle = handle;
    return calibrated;
}

static void analog_inputs_calibration_deinit(adc_cali_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_ERROR_CHECK_WITHOUT_ABORT(adc_cali_delete_scheme_curve_fitting(handle));
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_ERROR_CHECK_WITHOUT_ABORT(adc_cali_delete_scheme_line_fitting(handle));
#endif
}

static esp_err_t analog_inputs_ensure_unit(adc_unit_t unit_id, adc_oneshot_unit_handle_t *handle)
{
    int unit_index = analog_inputs_unit_index(unit_id);
    ESP_RETURN_ON_FALSE(unit_index >= 0, ESP_ERR_INVALID_ARG, TAG, "unsupported adc unit");

    if (!s_units[unit_index].in_use) {
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = unit_id,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init_config, &s_units[unit_index].handle), TAG, "adc unit init failed");
        s_units[unit_index].unit_id = unit_id;
        s_units[unit_index].in_use = true;
    }

    *handle = s_units[unit_index].handle;
    return ESP_OK;
}

bool analog_inputs_has_valid_pins(const analog_inputs_config_t *config)
{
    if (config == NULL || config->channel_count == 0 || config->channel_count > ANALOG_INPUTS_MAX_CHANNELS
        || config->samples_per_read == 0U || config->samples_per_read > ANALOG_INPUTS_MAX_SAMPLES) {
        return false;
    }

    for (size_t channel = 0; channel < config->channel_count; ++channel) {
        if (config->gpio_num[channel] < 0) {
            return false;
        }
    }

    return true;
}

esp_err_t analog_inputs_init(const analog_inputs_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    ESP_RETURN_ON_FALSE(analog_inputs_has_valid_pins(config), ESP_ERR_INVALID_ARG, TAG, "invalid pin configuration");

    if (s_initialized) {
        return ESP_OK;
    }

    memset(&s_config, 0, sizeof(s_config));
    memset(s_units, 0, sizeof(s_units));
    memset(s_channels, 0, sizeof(s_channels));
    s_config = *config;

    /* ADC_BITWIDTH_DEFAULT is the widest the chip offers, 12 bits here. */
    const int bits = (s_config.bitwidth == ADC_BITWIDTH_DEFAULT) ? 12 : (int)s_config.bitwidth;
    s_saturation_raw = ((1 << bits) - 1) - ANALOG_INPUTS_SATURATION_MARGIN_RAW;

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = s_config.atten,
        .bitwidth = s_config.bitwidth,
    };

    for (size_t index = 0; index < s_config.channel_count; ++index) {
        adc_unit_t unit_id = 0;
        adc_channel_t channel = 0;
        esp_err_t map_err = adc_oneshot_io_to_channel(s_config.gpio_num[index], &unit_id, &channel);
        if (map_err != ESP_OK) {
            ESP_LOGE(TAG, "GPIO%d (ch%u) is not ADC-capable: %s",
                     s_config.gpio_num[index], (unsigned)index, esp_err_to_name(map_err));
            return map_err;
        }
        ESP_LOGI(TAG, "ch%u: GPIO%d -> ADC%u ch%u",
                 (unsigned)index, s_config.gpio_num[index], (unsigned)unit_id + 1U, (unsigned)channel);

        adc_oneshot_unit_handle_t unit_handle = NULL;
        ESP_RETURN_ON_ERROR(analog_inputs_ensure_unit(unit_id, &unit_handle), TAG, "adc unit setup failed");
        ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(unit_handle, channel, &channel_config), TAG, "adc channel config failed");

        s_channels[index].configured = true;
        s_channels[index].gpio_num = s_config.gpio_num[index];
        s_channels[index].unit_id = unit_id;
        s_channels[index].channel = channel;
        s_channels[index].calibrated = analog_inputs_calibration_init(unit_id,
                                                                      channel,
                                                                      s_config.atten,
                                                                      s_config.bitwidth,
                                                                      &s_channels[index].cali_handle);
        ESP_LOGI(TAG, "ch%u: calibration %s", (unsigned)index,
                 s_channels[index].calibrated ? "ok" : "unavailable (raw fallback)");
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Analog input driver ready with %u channels", (unsigned)s_config.channel_count);
    return ESP_OK;
}

esp_err_t analog_inputs_read(float *values_v, uint32_t *valid_mask, uint32_t *saturated_mask)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "driver not initialized");
    ESP_RETURN_ON_FALSE(values_v != NULL, ESP_ERR_INVALID_ARG, TAG, "values_v is null");
    ESP_RETURN_ON_FALSE(valid_mask != NULL, ESP_ERR_INVALID_ARG, TAG, "valid_mask is null");

    uint32_t mask = 0;
    uint32_t saturated = 0;
    for (size_t index = 0; index < s_config.channel_count; ++index) {
        int unit_index = analog_inputs_unit_index(s_channels[index].unit_id);
        ESP_RETURN_ON_FALSE(unit_index >= 0, ESP_ERR_INVALID_STATE, TAG, "invalid unit state");

        /* Averaging the codes, then converting once: the calibration curve
         * is smooth enough over a few LSB of noise. */
        int32_t raw_sum = 0;
        esp_err_t err = ESP_OK;
        for (uint32_t sample = 0; sample < s_config.samples_per_read && err == ESP_OK; ++sample) {
            int raw_sample = 0;
            err = adc_oneshot_read(s_units[unit_index].handle, s_channels[index].channel, &raw_sample);
            raw_sum += raw_sample;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ADC read failed on channel %u: %s", (unsigned)index, esp_err_to_name(err));
            continue;
        }
        const int raw = (int)((raw_sum + (int32_t)(s_config.samples_per_read / 2U))
                              / (int32_t)s_config.samples_per_read);
        if (raw >= s_saturation_raw) {
            saturated |= 1UL << index;
        }

        if (s_channels[index].calibrated && s_channels[index].cali_handle != NULL) {
            int voltage_mv = 0;
            err = adc_cali_raw_to_voltage(s_channels[index].cali_handle, raw, &voltage_mv);
            if (err == ESP_OK) {
                values_v[index] = (float)voltage_mv / 1000.0f;
            } else {
                ESP_LOGW(TAG, "ADC calibration failed on channel %u: %s", (unsigned)index, esp_err_to_name(err));
                values_v[index] = ((float)raw * ANALOG_INPUTS_FALLBACK_FULL_SCALE_MV) / ANALOG_INPUTS_FALLBACK_MAX_RAW / 1000.0f;
            }
        } else {
            values_v[index] = ((float)raw * ANALOG_INPUTS_FALLBACK_FULL_SCALE_MV) / ANALOG_INPUTS_FALLBACK_MAX_RAW / 1000.0f;
        }

        mask |= 1UL << index;
    }

    *valid_mask = mask;
    if (saturated_mask != NULL) {
        *saturated_mask = saturated;
    }
    return ESP_OK;
}

esp_err_t analog_inputs_deinit(void)
{
    for (size_t index = 0; index < ANALOG_INPUTS_MAX_CHANNELS; ++index) {
        if (s_channels[index].calibrated) {
            analog_inputs_calibration_deinit(s_channels[index].cali_handle);
        }
    }

    for (size_t unit_index = 0; unit_index < ANALOG_INPUTS_UNIT_COUNT; ++unit_index) {
        if (s_units[unit_index].in_use) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(adc_oneshot_del_unit(s_units[unit_index].handle));
        }
    }

    memset(&s_config, 0, sizeof(s_config));
    memset(s_units, 0, sizeof(s_units));
    memset(s_channels, 0, sizeof(s_channels));
    s_initialized = false;
    return ESP_OK;
}

bool analog_inputs_is_initialized(void)
{
    return s_initialized;
}
