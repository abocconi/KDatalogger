#include "logger_service.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "storage_manager.h"

#define LOGGER_FILE_PREFIX "log_"
#define LOGGER_FILE_EXTENSION ".csv"
#define LOGGER_PATH_MAX_LEN 96
#define LOGGER_SAMPLE_FLUSH_PERIOD 5U
#define LOGGER_EMPTY_SAMPLE_FIELDS 16U

static const char *TAG = "logger";

static FILE *s_log_file;
static bool s_initialized;
static bool s_active;
static char s_current_path[LOGGER_PATH_MAX_LEN];
static uint32_t s_samples_since_flush;

static uint64_t logger_get_uptime_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static bool logger_parse_index(const char *name, uint32_t *index)
{
    const size_t prefix_len = strlen(LOGGER_FILE_PREFIX);
    const size_t ext_len = strlen(LOGGER_FILE_EXTENSION);
    const size_t name_len = strlen(name);

    if (name_len <= prefix_len + ext_len) {
        return false;
    }
    if (strncmp(name, LOGGER_FILE_PREFIX, prefix_len) != 0) {
        return false;
    }
    if (strcmp(name + name_len - ext_len, LOGGER_FILE_EXTENSION) != 0) {
        return false;
    }

    char digits[16] = {0};
    const size_t digits_len = name_len - prefix_len - ext_len;
    if (digits_len >= sizeof(digits)) {
        return false;
    }

    memcpy(digits, name + prefix_len, digits_len);
    char *end = NULL;
    unsigned long parsed = strtoul(digits, &end, 10);
    if (end == NULL || *end != '\0') {
        return false;
    }

    *index = (uint32_t)parsed;
    return true;
}

static esp_err_t logger_ensure_directory(void)
{
    if (mkdir(LOGGER_DIRECTORY_PATH, 0755) == 0 || errno == EEXIST) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to create %s: errno=%d", LOGGER_DIRECTORY_PATH, errno);
    return ESP_FAIL;
}

static esp_err_t logger_resolve_next_path(void)
{
    DIR *dir = opendir(LOGGER_DIRECTORY_PATH);
    uint32_t max_index = 0;

    if (dir != NULL) {
        struct dirent *entry = NULL;
        while ((entry = readdir(dir)) != NULL) {
            uint32_t parsed_index = 0;
            if (logger_parse_index(entry->d_name, &parsed_index) && parsed_index > max_index) {
                max_index = parsed_index;
            }
        }
        closedir(dir);
    }

    int written = snprintf(s_current_path,
                           sizeof(s_current_path),
                           "%s/%s%04" PRIu32 "%s",
                           LOGGER_DIRECTORY_PATH,
                           LOGGER_FILE_PREFIX,
                           max_index + 1,
                           LOGGER_FILE_EXTENSION);
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)sizeof(s_current_path), ESP_ERR_INVALID_SIZE, TAG, "log path too long");
    return ESP_OK;
}

static void logger_write_csv_field(FILE *file, const char *text)
{
    fputc('"', file);
    if (text != NULL) {
        for (const char *cursor = text; *cursor != '\0'; ++cursor) {
            if (*cursor == '"') {
                fputc('"', file);
            }
            fputc(*cursor, file);
        }
    }
    fputc('"', file);
}

static void logger_write_empty_fields(FILE *file, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        fputc(',', file);
    }
}

static esp_err_t logger_commit_row(bool force_flush)
{
    if (ferror(s_log_file) != 0) {
        clearerr(s_log_file);
        ESP_LOGE(TAG, "write failed for %s", s_current_path);
        return ESP_FAIL;
    }

    if (force_flush || s_samples_since_flush >= LOGGER_SAMPLE_FLUSH_PERIOD) {
        ESP_RETURN_ON_ERROR(logger_service_flush(), TAG, "flush failed");
        s_samples_since_flush = 0;
    }

    return ESP_OK;
}

esp_err_t logger_service_init(void)
{
    s_log_file = NULL;
    s_active = false;
    s_initialized = true;
    s_current_path[0] = '\0';
    s_samples_since_flush = 0;
    return ESP_OK;
}

esp_err_t logger_service_start(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "logger not initialized");
    ESP_RETURN_ON_FALSE(storage_manager_get_owner() == STORAGE_OWNER_FIRMWARE,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "storage is not owned by firmware");

    if (s_active) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(logger_ensure_directory(), TAG, "directory init failed");
    ESP_RETURN_ON_ERROR(logger_resolve_next_path(), TAG, "path resolution failed");

    s_log_file = fopen(s_current_path, "w");
    ESP_RETURN_ON_FALSE(s_log_file != NULL, ESP_FAIL, TAG, "failed to open %s", s_current_path);

    fprintf(s_log_file,
            "uptime_ms,record_type,event,detail,tc01_c,tc02_c,tc03_c,tc04_c,tc05_c,tc06_c,tc07_c,tc08_c,"
            "tc_valid_mask,ai01,ai02,ai03,ai04,ai_valid_mask,di_value,di_valid_mask\n");
    s_active = true;
    s_samples_since_flush = 0;

    ESP_RETURN_ON_ERROR(logger_service_log_event("logger_started", s_current_path), TAG, "initial log failed");
    ESP_LOGI(TAG, "Logging to %s", s_current_path);
    return ESP_OK;
}

esp_err_t logger_service_stop(void)
{
    if (!s_active) {
        return ESP_OK;
    }

    (void)logger_service_log_event("logger_stopped", "service_stop");
    (void)logger_service_flush();
    fclose(s_log_file);
    s_log_file = NULL;
    s_active = false;
    ESP_LOGI(TAG, "Logger stopped");
    return ESP_OK;
}

esp_err_t logger_service_flush(void)
{
    if (!s_active || s_log_file == NULL) {
        return ESP_OK;
    }

    if (fflush(s_log_file) != 0) {
        ESP_LOGE(TAG, "fflush failed for %s", s_current_path);
        return ESP_FAIL;
    }

    if (fsync(fileno(s_log_file)) != 0) {
        ESP_LOGE(TAG, "fsync failed for %s: errno=%d", s_current_path, errno);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t logger_service_log_event(const char *event, const char *detail)
{
    ESP_RETURN_ON_FALSE(s_active && s_log_file != NULL, ESP_ERR_INVALID_STATE, TAG, "logger inactive");
    ESP_RETURN_ON_FALSE(event != NULL, ESP_ERR_INVALID_ARG, TAG, "event is required");

    fprintf(s_log_file, "%" PRIu64 ",", logger_get_uptime_ms());
    logger_write_csv_field(s_log_file, "event");
    fputc(',', s_log_file);
    logger_write_csv_field(s_log_file, event);
    fputc(',', s_log_file);
    logger_write_csv_field(s_log_file, detail);
    logger_write_empty_fields(s_log_file, LOGGER_EMPTY_SAMPLE_FIELDS);
    fputc('\n', s_log_file);

    return logger_commit_row(true);
}

esp_err_t logger_service_log_sample(const kdl_sensor_sample_t *sample)
{
    ESP_RETURN_ON_FALSE(s_active && s_log_file != NULL, ESP_ERR_INVALID_STATE, TAG, "logger inactive");
    ESP_RETURN_ON_FALSE(sample != NULL, ESP_ERR_INVALID_ARG, TAG, "sample is required");

    fprintf(s_log_file, "%" PRIu64 ",", sample->uptime_ms);
    logger_write_csv_field(s_log_file, "sample");
    fputc(',', s_log_file);
    logger_write_csv_field(s_log_file, NULL);
    fputc(',', s_log_file);
    logger_write_csv_field(s_log_file, NULL);

    for (size_t index = 0; index < DATA_MODEL_THERMOCOUPLE_COUNT; ++index) {
        fprintf(s_log_file, ",%.2f", sample->thermocouples_c[index]);
    }
    fprintf(s_log_file, ",0x%04" PRIx16, sample->thermocouple_valid_mask);
    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        fprintf(s_log_file, ",%.3f", sample->analog_inputs[index]);
    }
    fprintf(s_log_file, ",0x%02" PRIx8, sample->analog_valid_mask);
    fprintf(s_log_file, ",0x%08" PRIx32, sample->digital_inputs);
    fprintf(s_log_file, ",0x%08" PRIx32, sample->digital_valid_mask);
    fputc('\n', s_log_file);

    s_samples_since_flush++;
    return logger_commit_row(false);
}

bool logger_service_is_active(void)
{
    return s_active;
}

const char *logger_service_get_current_path(void)
{
    return s_current_path;
}
