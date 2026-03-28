#include "board_config.h"

spi_host_device_t board_config_max31855_host(void)
{
    return SPI2_HOST;
}

int board_config_max31855_sclk_io(void)
{
    return BOARD_PIN_PLACEHOLDER;
}

int board_config_max31855_miso_io(void)
{
    return BOARD_PIN_PLACEHOLDER;
}

int board_config_max31855_cs_io(size_t channel)
{
    static const int cs_pins[BOARD_MAX31855_CHANNEL_COUNT] = {
        BOARD_PIN_PLACEHOLDER,
        BOARD_PIN_PLACEHOLDER,
        BOARD_PIN_PLACEHOLDER,
        BOARD_PIN_PLACEHOLDER,
        BOARD_PIN_PLACEHOLDER,
        BOARD_PIN_PLACEHOLDER,
        BOARD_PIN_PLACEHOLDER,
        BOARD_PIN_PLACEHOLDER,
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
