#include "sensor_config.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board_config.h"
#include "data_model_channels.h"
#include "storage_manager.h"

#define SENSOR_CONFIG_PATH     STORAGE_MOUNT_PATH "/" SENSOR_CONFIG_FILE_NAME
/* Longest line kept; a longer one is reported as an error, not truncated. */
#define SENSOR_CONFIG_LINE_LEN 160U

static const char *TAG = "sensor_config";

/* s_lock guards the published copies below; s_load_mutex serialises loads,
 * which come from the boot sequence and from the GUI task (USB exit). The
 * parser and line buffer are static to keep the callers' stacks small, and
 * are only touched under s_load_mutex. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_load_mutex;
static StaticSemaphore_t s_load_mutex_buffer;
static sensor_config_t s_config;
static sensor_config_status_t s_status;
static sensor_config_parser_t s_parser;
static char s_line[SENSOR_CONFIG_LINE_LEN];

static void sensor_config_publish(const sensor_config_t *config,
                                  const sensor_config_status_t *status)
{
    portENTER_CRITICAL(&s_lock);
    s_config = *config;
    s_status = *status;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t sensor_config_init(void)
{
    if (s_load_mutex == NULL) {
        s_load_mutex = xSemaphoreCreateMutexStatic(&s_load_mutex_buffer);
        ESP_RETURN_ON_FALSE(s_load_mutex != NULL, ESP_ERR_NO_MEM, TAG, "mutex creation failed");
    }

    sensor_config_t defaults;
    sensor_config_defaults(&defaults);
    const sensor_config_status_t status = { .source = SENSOR_CONFIG_SOURCE_DEFAULTS };
    sensor_config_publish(&defaults, &status);
    return ESP_OK;
}

static esp_err_t sensor_config_write_default_file(void)
{
    sensor_config_t defaults;
    sensor_config_defaults(&defaults);

    FILE *file = fopen(SENSOR_CONFIG_PATH, "wb");
    ESP_RETURN_ON_FALSE(file != NULL, ESP_FAIL, TAG, "cannot create %s: errno=%d",
                        SENSOR_CONFIG_PATH, errno);

    const int write_result = sensor_config_write_template(file, &defaults,
                                                          board_config_analog_input_max_v());
    const int close_result = fclose(file);
    if (write_result != 0 || close_result != 0) {
        /* A truncated file would be read back with errors on every boot. */
        if (remove(SENSOR_CONFIG_PATH) != 0) {
            ESP_LOGW(TAG, "partial %s not removed: errno=%d", SENSOR_CONFIG_PATH, errno);
        }
        ESP_LOGE(TAG, "writing %s failed (write %d, close %d)", SENSOR_CONFIG_PATH,
                 write_result, close_result);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/** Parse the open file into s_parser. Caller holds s_load_mutex. */
static void sensor_config_parse_file(FILE *file)
{
    sensor_config_parser_begin(&s_parser);

    uint32_t line_no = 0;
    while (fgets(s_line, sizeof(s_line), file) != NULL) {
        ++line_no;
        const size_t len = strlen(s_line);
        if (len == sizeof(s_line) - 1U && s_line[len - 1U] != '\n' && !feof(file)) {
            sensor_config_parser_error(&s_parser, line_no);
            int c = 0;
            do {
                c = fgetc(file);
            } while (c != '\n' && c != EOF);
            continue;
        }
        sensor_config_parser_feed(&s_parser, s_line, line_no);
    }

    sensor_config_parser_end(&s_parser, board_config_analog_input_max_v());
}

static void sensor_config_log(const sensor_config_t *config, const sensor_config_status_t *status)
{
    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        const pressure_sensor_cfg_t *cfg = &config->pressure[index];
        if (cfg->enabled) {
            ESP_LOGI(TAG, "%s: %.2f-%.2f V -> %.2f-%.2f bar", data_model_analog_channels[index].id,
                     (double)cfg->v_min, (double)cfg->v_max, (double)cfg->p_min,
                     (double)cfg->p_max);
        } else {
            ESP_LOGI(TAG, "%s: off", data_model_analog_channels[index].id);
        }
    }
    if (config->tach.enabled) {
        ESP_LOGI(TAG, "tachometer: %.2f pulses/rev, full scale %" PRIu32 " rpm",
                 (double)config->tach.pulses_per_rev, config->tach.rpm_max);
    } else {
        ESP_LOGI(TAG, "tachometer: off");
    }
    if (status->error_count != 0U) {
        ESP_LOGW(TAG, "%s: %" PRIu32 " error(s), first at line %" PRIu32
                 "; the sections involved are disabled",
                 SENSOR_CONFIG_FILE_NAME, status->error_count, status->first_error_line);
    }
}

esp_err_t sensor_config_load(void)
{
    ESP_RETURN_ON_FALSE(s_load_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(storage_manager_get_owner() == STORAGE_OWNER_FIRMWARE,
                        ESP_ERR_INVALID_STATE, TAG, "storage is not owned by firmware");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_load_mutex, portMAX_DELAY) == pdTRUE, ESP_FAIL, TAG,
                        "mutex take failed");

    esp_err_t result = ESP_OK;
    sensor_config_status_t status = { 0 };
    FILE *file = fopen(SENSOR_CONFIG_PATH, "rb");
    if (file == NULL) {
        const int open_errno = errno;
        sensor_config_parser_begin(&s_parser);
        if (open_errno == ENOENT) {
            result = sensor_config_write_default_file();
            status.source = (result == ESP_OK) ? SENSOR_CONFIG_SOURCE_CREATED
                                               : SENSOR_CONFIG_SOURCE_DEFAULTS;
            if (result == ESP_OK) {
                ESP_LOGI(TAG, "%s missing: default file written", SENSOR_CONFIG_PATH);
            }
        } else {
            status.source = SENSOR_CONFIG_SOURCE_DEFAULTS;
            ESP_LOGE(TAG, "cannot open %s: errno=%d", SENSOR_CONFIG_PATH, open_errno);
            result = ESP_FAIL;
        }
    } else {
        sensor_config_parse_file(file);
        const bool read_error = ferror(file) != 0;
        (void)fclose(file); /* Read-only: nothing is lost if the close fails. */
        if (read_error) {
            /* Whatever was parsed may be a fragment: fall back to the defaults. */
            ESP_LOGE(TAG, "read error on %s", SENSOR_CONFIG_PATH);
            sensor_config_parser_begin(&s_parser);
            status.source = SENSOR_CONFIG_SOURCE_DEFAULTS;
            result = ESP_FAIL;
        } else {
            status.source = SENSOR_CONFIG_SOURCE_FILE;
            status.error_count = s_parser.error_count;
            status.first_error_line = s_parser.first_error_line;
        }
    }

    sensor_config_publish(&s_parser.config, &status);
    sensor_config_log(&s_parser.config, &status);
    xSemaphoreGive(s_load_mutex);
    return result;
}

void sensor_config_get(sensor_config_t *out)
{
    if (out == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *out = s_config;
    portEXIT_CRITICAL(&s_lock);
}

void sensor_config_get_status(sensor_config_status_t *out)
{
    if (out == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_lock);
}
