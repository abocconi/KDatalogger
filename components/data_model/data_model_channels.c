#include "data_model_channels.h"

/* U+00B0 DEGREE SIGN encoded as UTF-8. */
#define DATA_MODEL_UNIT_CELSIUS "\xC2\xB0" "C"

const kdl_channel_info_t data_model_thermocouple_channels[DATA_MODEL_THERMOCOUPLE_COUNT] = {
    { .id = "Tc1", .name = "Cil 1",  .unit = DATA_MODEL_UNIT_CELSIUS },
    { .id = "Tc2", .name = "Cil 2",  .unit = DATA_MODEL_UNIT_CELSIUS },
    { .id = "Tc3", .name = "Cil 3",  .unit = DATA_MODEL_UNIT_CELSIUS },
    { .id = "Tc4", .name = "Cil 4",  .unit = DATA_MODEL_UNIT_CELSIUS },
    { .id = "Tc5", .name = "IC in",  .unit = DATA_MODEL_UNIT_CELSIUS },
    { .id = "Tc6", .name = "IC out", .unit = DATA_MODEL_UNIT_CELSIUS },
    { .id = "Tc7", .name = "Olio",   .unit = DATA_MODEL_UNIT_CELSIUS },
    { .id = "Tc8", .name = "Acqua",  .unit = DATA_MODEL_UNIT_CELSIUS },
};

/* IN1 is the record-enable contact and is not sampled, so the first analog
 * channel is IN2. Pressures stay in volts until the sensor scaling is known;
 * the "P" prefix keeps them apart from the intercooler temperatures. */
const kdl_channel_info_t data_model_analog_channels[DATA_MODEL_ANALOG_INPUT_COUNT] = {
    { .id = "IN2", .name = "P IC in",  .unit = "V" },
    { .id = "IN3", .name = "P IC out", .unit = "V" },
    { .id = "IN4", .name = "P scar",   .unit = "V" },
    { .id = "IN5", .name = "P benz",   .unit = "V" },
    { .id = "IN6", .name = "P olio",   .unit = "V" },
};
