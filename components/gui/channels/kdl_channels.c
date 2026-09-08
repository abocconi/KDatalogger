#include "kdl_channels.h"

#include <stddef.h>

/* Names mirror the UI prototype. Thresholds and full-scale values are first
 * estimates for a tractor-pulling engine and are expected to be re-tuned on
 * the vehicle: exhaust and turbo run hot, oil and coolant do not, so a single
 * shared scale (as in the prototype) would leave the oil and coolant bars
 * pinned near zero for the whole session. */
static const kdl_thermocouple_desc_t s_thermocouples[KDL_THERMOCOUPLE_DISPLAY_COUNT] = {
    { .id = "K1", .name = "Exh 1",     .full_scale = 800.0f, .warn_threshold = 600.0f, .alarm_threshold = 700.0f },
    { .id = "K2", .name = "Exh 2",     .full_scale = 800.0f, .warn_threshold = 600.0f, .alarm_threshold = 700.0f },
    { .id = "K3", .name = "Exh 3",     .full_scale = 800.0f, .warn_threshold = 600.0f, .alarm_threshold = 700.0f },
    { .id = "K4", .name = "Exh 4",     .full_scale = 800.0f, .warn_threshold = 600.0f, .alarm_threshold = 700.0f },
    { .id = "K5", .name = "Turbo in",  .full_scale = 800.0f, .warn_threshold = 600.0f, .alarm_threshold = 700.0f },
    { .id = "K6", .name = "Turbo out", .full_scale = 700.0f, .warn_threshold = 500.0f, .alarm_threshold = 600.0f },
    { .id = "K7", .name = "Oil",       .full_scale = 160.0f, .warn_threshold = 110.0f, .alarm_threshold = 130.0f },
    { .id = "K8", .name = "Coolant",   .full_scale = 130.0f, .warn_threshold =  95.0f, .alarm_threshold = 110.0f },
};

/* Terminal markings follow the physical wiring, so the first displayed cell is
 * A2: A1 is the recording-enable contact and is deliberately absent from the
 * main page. Renumbering these to A1..A4 would make the display disagree with
 * the labels on the enclosure. */
static const kdl_analog_desc_t s_analogs[KDL_ANALOG_DISPLAY_COUNT] = {
    { .id = "A2", .name = "Fuel P", .unit = "bar", .decimals = 1, .input_index = 1 },
    { .id = "A3", .name = "Boost",  .unit = "bar", .decimals = 2, .input_index = 2 },
    { .id = "A4", .name = "Intake", .unit = "\xC2\xB0" "C", .decimals = 0, .input_index = 3 },
    { .id = "A5", .name = "Batt",   .unit = "V",   .decimals = 1, .input_index = 4 },
};

/* U+00B0 DEGREE SIGN encoded as UTF-8; present in LVGL's built-in Montserrat
 * faces, unlike the accented characters an Italian UI would need. */
static const char s_temperature_unit[] = "\xC2\xB0" "C";

_Static_assert(KDL_ANALOG_RECORD_ENABLE_INDEX < DATA_MODEL_ANALOG_INPUT_COUNT,
               "reserved analog index outside the sample");
_Static_assert(KDL_ANALOG_DISPLAY_COUNT == 4U,
               "main page analog row is laid out as a 4-column grid");

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
