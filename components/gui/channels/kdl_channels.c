#include "kdl_channels.h"

#include <stddef.h>

/* Thresholds and full-scale values are first estimates for a tractor-pulling
 * engine and are expected to be re-tuned on the vehicle: cylinder exhaust
 * runs hot, intercooler, oil and coolant do not, so a single shared scale
 * would leave the cooler channels' bars pinned near zero for the whole
 * session. Intercooler values: alarm at ~80 % of full scale. */
#define KDL_TC(index) (&data_model_thermocouple_channels[(index)])
static const kdl_thermocouple_desc_t s_thermocouples[KDL_THERMOCOUPLE_DISPLAY_COUNT] = {
    { .channel = KDL_TC(0), .full_scale = 800.0f, .warn_threshold = 600.0f, .alarm_threshold = 700.0f },
    { .channel = KDL_TC(1), .full_scale = 800.0f, .warn_threshold = 600.0f, .alarm_threshold = 700.0f },
    { .channel = KDL_TC(2), .full_scale = 800.0f, .warn_threshold = 600.0f, .alarm_threshold = 700.0f },
    { .channel = KDL_TC(3), .full_scale = 800.0f, .warn_threshold = 600.0f, .alarm_threshold = 700.0f },
    { .channel = KDL_TC(4), .full_scale = 250.0f, .warn_threshold = 175.0f, .alarm_threshold = 200.0f },
    { .channel = KDL_TC(5), .full_scale = 100.0f, .warn_threshold =  70.0f, .alarm_threshold =  80.0f },
    { .channel = KDL_TC(6), .full_scale = 160.0f, .warn_threshold = 110.0f, .alarm_threshold = 130.0f },
    { .channel = KDL_TC(7), .full_scale = 130.0f, .warn_threshold =  95.0f, .alarm_threshold = 110.0f },
};

/* Raw volts until the pressure sensor scaling is known, hence 2 decimals. */
#define KDL_AI(index) .channel = &data_model_analog_channels[(index)], .input_index = (index)
static const kdl_analog_desc_t s_analogs[KDL_ANALOG_DISPLAY_COUNT] = {
    { KDL_AI(0), .decimals = 2 },
    { KDL_AI(1), .decimals = 2 },
    { KDL_AI(2), .decimals = 2 },
    { KDL_AI(3), .decimals = 2 },
    { KDL_AI(4), .decimals = 2 },
};

/* U+00B0 DEGREE SIGN encoded as UTF-8; present in LVGL's built-in Montserrat
 * faces, unlike the accented characters an Italian UI would need. */
static const char s_temperature_unit[] = "\xC2\xB0" "C";


const kdl_thermocouple_desc_t *kdl_channels_thermocouple(uint8_t index)
{
    if (index >= KDL_THERMOCOUPLE_DISPLAY_COUNT)
    {
        return NULL;
    }

    return &s_thermocouples[index];
}

const kdl_analog_desc_t *kdl_channels_analog(uint8_t index)
{
    if (index >= KDL_ANALOG_DISPLAY_COUNT)
    {
        return NULL;
    }

    return &s_analogs[index];
}

const char *kdl_channels_temperature_unit(void)
{
    return s_temperature_unit;
}

bool kdl_channels_is_disconnected(const kdl_sensor_sample_t *sample, uint8_t index)
{
    if (sample == NULL || index >= KDL_THERMOCOUPLE_DISPLAY_COUNT)
    {
        return true;
    }

    return (sample->thermocouple_oc_mask & (uint16_t)(1U << index)) != 0U;
}

bool kdl_channels_is_valid(const kdl_sensor_sample_t *sample, uint8_t index)
{
    if (sample == NULL || index >= KDL_THERMOCOUPLE_DISPLAY_COUNT)
    {
        return false;
    }

    return (sample->thermocouple_valid_mask & (uint16_t)(1U << index)) != 0U;
}
