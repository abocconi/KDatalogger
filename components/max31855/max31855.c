#include "max31855.h"

#include <string.h>

#include "esp_bit_defs.h"
#include "esp_check.h"
#include "esp_log.h"

#define MAX31855_SPI_CLOCK_HZ 1000000
#define MAX31855_FRAME_BITS 32
#define MAX31855_THERMOCOUPLE_LSB_C 0.25f
#define MAX31855_INTERNAL_LSB_C 0.0625f

static const char *TAG = "max31855";

static spi_device_handle_t s_devices[MAX31855_CHANNEL_COUNT];
static spi_host_device_t s_host;
static bool s_bus_initialized;
static bool s_initialized;

static int32_t max31855_sign_extend(uint32_t value, uint32_t bits)
{
    uint32_t sign_bit = 1UL << (bits - 1U);
    uint32_t extended = value;

    if ((value & sign_bit) != 0U) {
        extended |= ~((1UL << bits) - 1U);
    }

    return (int32_t)extended;
}

bool max31855_has_valid_pins(const max31855_config_t *config)
{
    if (config == NULL || config->sclk_io < 0 || config->miso_io < 0) {
        return false;
    }

    for (size_t channel = 0; channel < MAX31855_CHANNEL_COUNT; ++channel) {
        if (config->cs_io[channel] < 0) {
            return false;
        }
    }

    return true;
}

esp_err_t max31855_init(const max31855_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    ESP_RETURN_ON_FALSE(max31855_has_valid_pins(config), ESP_ERR_INVALID_ARG, TAG, "invalid pin configuration");

    if (s_initialized) {
        return ESP_OK;
    }

    memset(s_devices, 0, sizeof(s_devices));
    s_host = config->host;

    spi_bus_config_t bus_config = {
        .mosi_io_num = -1,
        .miso_io_num = config->miso_io,
        .sclk_io_num = config->sclk_io,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = sizeof(uint32_t),
    };

    ESP_RETURN_ON_ERROR(spi_bus_initialize(config->host, &bus_config, SPI_DMA_DISABLED), TAG, "spi bus init failed");
    s_bus_initialized = true;

    for (size_t channel = 0; channel < MAX31855_CHANNEL_COUNT; ++channel) {
        spi_device_interface_config_t device_config = {
            .clock_speed_hz = MAX31855_SPI_CLOCK_HZ,
            .mode = 0,
            .spics_io_num = config->cs_io[channel],
            .queue_size = 1,
        };

        esp_err_t err = spi_bus_add_device(config->host, &device_config, &s_devices[channel]);
        if (err != ESP_OK) {
            (void)max31855_deinit();
            return err;
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "MAX31855 bus ready on host %d", (int)config->host);
    return ESP_OK;
}

esp_err_t max31855_read_channel(size_t channel, max31855_reading_t *reading)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "driver not initialized");
    ESP_RETURN_ON_FALSE(reading != NULL, ESP_ERR_INVALID_ARG, TAG, "reading is null");
    ESP_RETURN_ON_FALSE(channel < MAX31855_CHANNEL_COUNT, ESP_ERR_INVALID_ARG, TAG, "invalid channel");

    spi_transaction_t transaction = {
        .length = MAX31855_FRAME_BITS,
        .rxlength = MAX31855_FRAME_BITS,
        .flags = SPI_TRANS_USE_RXDATA,
    };

    ESP_RETURN_ON_ERROR(spi_device_polling_transmit(s_devices[channel], &transaction), TAG, "spi transfer failed");

    uint32_t raw_data = ((uint32_t)transaction.rx_data[0] << 24) |
                        ((uint32_t)transaction.rx_data[1] << 16) |
                        ((uint32_t)transaction.rx_data[2] << 8) |
                        (uint32_t)transaction.rx_data[3];
    bool fault = (raw_data & BIT16) != 0U;
    int32_t thermocouple_raw = max31855_sign_extend((raw_data >> 18) & 0x3FFFU, 14U);
    int32_t internal_raw = max31855_sign_extend((raw_data >> 4) & 0x0FFFU, 12U);

    *reading = (max31855_reading_t){
        .valid = !fault,
        .open_circuit = (raw_data & BIT2) != 0U,
        .short_to_gnd = (raw_data & BIT1) != 0U,
        .short_to_vcc = (raw_data & BIT0) != 0U,
        .thermocouple_c = (float)thermocouple_raw * MAX31855_THERMOCOUPLE_LSB_C,
        .internal_c = (float)internal_raw * MAX31855_INTERNAL_LSB_C,
        .raw_data = raw_data,
    };

    return ESP_OK;
}

esp_err_t max31855_deinit(void)
{
    for (size_t channel = 0; channel < MAX31855_CHANNEL_COUNT; ++channel) {
        if (s_devices[channel] != NULL) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_remove_device(s_devices[channel]));
            s_devices[channel] = NULL;
        }
    }

    if (s_bus_initialized) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_free(s_host));
    }

    s_bus_initialized = false;
    s_initialized = false;
    return ESP_OK;
}

bool max31855_is_initialized(void)
{
    return s_initialized;
}
