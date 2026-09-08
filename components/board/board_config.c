#include "board_config.h"

spi_host_device_t board_config_max31855_host(void)
{
    return SPI3_HOST;
}

int board_config_max31855_sclk_io(void)
{
    return 36;
}

int board_config_max31855_miso_io(void)
{
    return 37;
}

int board_config_max31855_cs_io(size_t channel)
{
    static const int cs_pins[BOARD_MAX31855_CHANNEL_COUNT] = {
        1,
        2,
        42,
        41,
        40,
        39,
        38,
        35,
    };

    if (channel >= BOARD_MAX31855_CHANNEL_COUNT) {
        return BOARD_PIN_PLACEHOLDER;
    }

    return cs_pins[channel];
}

bool board_config_max31855_has_valid_pins(void)
{
    if (board_config_max31855_sclk_io() < 0 || board_config_max31855_miso_io() < 0) {
        return false;
    }

    for (size_t channel = 0; channel < BOARD_MAX31855_CHANNEL_COUNT; ++channel) {
        if (board_config_max31855_cs_io(channel) < 0) {
            return false;
        }
    }

    return true;
}

size_t board_config_digital_input_count(void)
{
    return BOARD_DIGITAL_INPUT_COUNT;
}

int board_config_digital_input_gpio(size_t channel)
{
    static const int digital_input_pins[BOARD_DIGITAL_INPUT_COUNT] = {
        15,
        16,
        17,
        18,
        3,
    };

    if (channel >= BOARD_DIGITAL_INPUT_COUNT) {
        return BOARD_PIN_PLACEHOLDER;
    }

    return digital_input_pins[channel];
}

bool board_config_digital_inputs_has_valid_pins(void)
{
    for (size_t channel = 0; channel < BOARD_DIGITAL_INPUT_COUNT; ++channel) {
        if (board_config_digital_input_gpio(channel) < 0) {
            return false;
        }
    }

    return true;
}

size_t board_config_analog_input_count(void)
{
    return BOARD_ANALOG_INPUT_COUNT;
}

int board_config_analog_input_gpio(size_t channel)
{
    /* AI1 (GPIO13) is no longer part of this group: it is now the digital
     * record-enable input, see board_config_record_enable_gpio(). */
    static const int analog_input_pins[BOARD_ANALOG_INPUT_COUNT] = {
        9,
        7,
        6,
        5,
        4,
    };

    if (channel >= BOARD_ANALOG_INPUT_COUNT) {
        return BOARD_PIN_PLACEHOLDER;
    }

    return analog_input_pins[channel];
}

bool board_config_analog_inputs_has_valid_pins(void)
{
    for (size_t channel = 0; channel < BOARD_ANALOG_INPUT_COUNT; ++channel) {
        if (board_config_analog_input_gpio(channel) < 0) {
            return false;
        }
    }

    return true;
}

int board_config_record_enable_gpio(void)
{
    return 13;
}

spi_host_device_t board_config_display_host(void)
{
    /* Same high-speed display host used by ISP-HMI on this hardware.  The
     * MAX31855 bus is kept separate on SPI3_HOST. */
    return SPI2_HOST;
}

int board_config_display_mosi_io(void)
{
    return 11;
}

int board_config_display_sclk_io(void)
{
    return 12;
}

int board_config_display_cs_io(void)
{
    return 10;
}

int board_config_display_dc_io(void)
{
    return 8;
}

int board_config_display_rst_io(void)
{
    return 14;
}

int board_config_display_bl_io(void)
{
    return 21;
}

bool board_config_display_has_valid_pins(void)
{
    return board_config_display_mosi_io() >= 0 &&
           board_config_display_sclk_io() >= 0 &&
           board_config_display_cs_io() >= 0 &&
           board_config_display_dc_io() >= 0 &&
           board_config_display_rst_io() >= 0 &&
           board_config_display_bl_io() >= 0;
}
