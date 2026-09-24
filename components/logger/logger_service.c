#include "logger_service.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "data_model_channels.h"
#include "storage_manager.h"
#include "timekeeping.h"

#define LOGGER_FILE_PREFIX "log_"
#define LOGGER_FILE_EXTENSION ".csv"
#define LOGGER_PATH_MAX_LEN 96
/* Time-based rather than sample-based: the acquisition period is configurable
 * (100 ms - 5 s), and a per-sample count would turn into an fsync every 500 ms
 * at the fast end -- needless flash wear and a periodic stall in the
 * acquisition loop, since fsync on FAT+wear_levelling can take tens of ms. */
#define LOGGER_FLUSH_PERIOD_MS 2000ULL
/* Free space kept available on the volume. Checked at session start and on
 * every periodic flush: below it the oldest log files are deleted, so a
 * device whose logs are never cleared keeps recording, as a ring buffer at
 * file granularity. At the fastest rate (100 ms, ~115 B per row) this is
 * roughly 4 minutes of data, far more than one flush period. */
#define LOGGER_MIN_FREE_BYTES (256U * 1024U)
/* Cap on the number of log files, bounding the directory scan done at
 * session start and when reclaiming space. FAT subdirectories have no fixed
 * entry limit; this is about scan time, not capacity. */
#define LOGGER_MAX_FILES 500U

/* The file is meant to be double-clicked open in an Italian-locale Excel:
 * ';' as field separator and ',' as decimal mark, which is what Excel expects
 * there. The UTF-8 BOM is what makes Excel decode the degree sign correctly. */
#define LOGGER_CSV_SEPARATOR ';'
#define LOGGER_DECIMAL_MARK ','
#define LOGGER_UTF8_BOM "\xEF\xBB\xBF"
#define LOGGER_EOL "\r\n"
#define LOGGER_TC_DECIMALS 2
#define LOGGER_AI_DECIMALS 3

static const char *TAG = "logger";

static FILE *s_log_file;
static bool s_initialized;
static bool s_active;
static char s_current_path[LOGGER_PATH_MAX_LEN];
static uint32_t s_current_index;
/** Sticky until the next successful start; read from the GUI task. */
static volatile bool s_fault;
static uint32_t s_sample_count;
static uint64_t s_last_flush_ms;
/** Uptime of the first sample of the session: the zero of the "Tempo" column. */
static uint64_t s_session_start_ms;

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
    if (mkdir(LOGGER_DIRECTORY_PATH, 0755) == 0) {
        return ESP_OK;
    }

    if (errno != EEXIST) {
        ESP_LOGE(TAG, "Failed to create %s: errno=%d", LOGGER_DIRECTORY_PATH, errno);
        return ESP_FAIL;
    }

    /* EEXIST only proves the path is taken, not that it's a directory -- a
     * stray file left behind by earlier testing would make mkdir() fail here
     * and every later opendir()/fopen() fail silently downstream. */
    struct stat path_stat;
    if (stat(LOGGER_DIRECTORY_PATH, &path_stat) != 0 || !S_ISDIR(path_stat.st_mode)) {
        ESP_LOGE(TAG, "%s exists and is not a directory", LOGGER_DIRECTORY_PATH);
        return ESP_FAIL;
    }

    return ESP_OK;
}

typedef struct {
    uint32_t count;
    uint32_t min_index;
    uint32_t max_index;
} logger_dir_scan_t;

/** Count the log files and find the lowest and highest index on the volume. */
static void logger_scan_directory(logger_dir_scan_t *scan)
{
    scan->count = 0;
    scan->min_index = UINT32_MAX;
    scan->max_index = 0;

    DIR *dir = opendir(LOGGER_DIRECTORY_PATH);
    if (dir == NULL) {
        return;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        uint32_t index = 0;
        if (logger_parse_index(entry->d_name, &index)) {
            if (index > scan->max_index) { scan->max_index = index; }
            if (index < scan->min_index) { scan->min_index = index; }
            scan->count++;
        }
    }
    closedir(dir);
}

static esp_err_t logger_format_path(char *out, size_t len, uint32_t index)
{
    const int written = snprintf(out, len, "%s/%s%04" PRIu32 "%s", LOGGER_DIRECTORY_PATH,
                                 LOGGER_FILE_PREFIX, index, LOGGER_FILE_EXTENSION);
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)len, ESP_ERR_INVALID_SIZE, TAG,
                        "log path too long");
    return ESP_OK;
}

/**
 * Delete the oldest log file, never the one being written.
 * @return ESP_ERR_NOT_FOUND when there is nothing left to delete.
 */
static esp_err_t logger_delete_oldest(void)
{
    logger_dir_scan_t scan;
    logger_scan_directory(&scan);
    if (scan.count == 0U || (s_active && scan.min_index == s_current_index)) {
        return ESP_ERR_NOT_FOUND;
    }

    char oldest[LOGGER_PATH_MAX_LEN];
    ESP_RETURN_ON_ERROR(logger_format_path(oldest, sizeof(oldest), scan.min_index), TAG,
                        "oldest path");
    ESP_RETURN_ON_FALSE(remove(oldest) == 0, ESP_FAIL, TAG, "failed to delete %s: errno=%d",
                        oldest, errno);
    ESP_LOGI(TAG, "deleted oldest log %s (%" PRIu32 " files)", oldest, scan.count);
    return ESP_OK;
}

/** Delete old logs until LOGGER_MIN_FREE_BYTES are free, or none is left. */
static void logger_reclaim_space(void)
{
    for (;;) {
        uint64_t total_bytes = 0;
        uint64_t free_bytes = 0;
        if (storage_manager_get_usage(&total_bytes, &free_bytes) != ESP_OK) {
            ESP_LOGW(TAG, "free space unknown, nothing reclaimed");
            return;
        }
        if (free_bytes >= LOGGER_MIN_FREE_BYTES) {
            return;
        }

        const esp_err_t err = logger_delete_oldest();
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "volume nearly full (%" PRIu64 " B free), no old log left to delete",
                     free_bytes);
            return;
        }
        if (err != ESP_OK) {
            return;
        }
    }
}

static esp_err_t logger_resolve_next_path(void)
{
    logger_dir_scan_t scan;
    logger_scan_directory(&scan);

    /* Count cap first, then free space: both drop the oldest files. */
    for (uint32_t count = scan.count; count >= LOGGER_MAX_FILES; --count) {
        if (logger_delete_oldest() != ESP_OK) {
            break;
        }
    }
    logger_reclaim_space();

    s_current_index = scan.max_index + 1U;
    return logger_format_path(s_current_path, sizeof(s_current_path), s_current_index);
}

static void logger_write_header(FILE *file)
{
    fputs(LOGGER_UTF8_BOM "Data;Ora;Tempo [s]", file);
    /* Same names as on screen, so the Excel legend matches the display.
     * None contains the separator, hence no quoting. */
    for (size_t index = 0; index < DATA_MODEL_THERMOCOUPLE_COUNT; ++index) {
        const kdl_channel_info_t *channel = &data_model_thermocouple_channels[index];
        fprintf(file, ";%s [%s]", channel->name, channel->unit);
    }
    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        const kdl_channel_info_t *channel = &data_model_analog_channels[index];
        fprintf(file, ";%s [%s]", channel->name, channel->unit);
    }
    fputs(LOGGER_EOL, file);
}

/**
 * Write a measurement cell preceded by its separator. An invalid or
 * non-finite value leaves the cell empty, so Excel breaks the plotted line
 * instead of drawing a spike to whatever number the sample happened to hold.
 */
static void logger_write_value(FILE *file, bool valid, float value, int decimals)
{
    fputc(LOGGER_CSV_SEPARATOR, file);
    if (!valid || !isfinite(value)) {
        return;
    }

    /* newlib's printf ignores the locale, so the decimal mark is patched in. */
    char text[24];
    const int len = snprintf(text, sizeof(text), "%.*f", decimals, (double)value);
    if (len <= 0 || len >= (int)sizeof(text)) {
        return;
    }
    char *dot = strchr(text, '.');
    if (dot != NULL) {
        *dot = LOGGER_DECIMAL_MARK;
    }
    fputs(text, file);
}

/** Date and time cells; both empty while the operator has not set the clock. */
static void logger_write_wall_clock(FILE *file)
{
    if (!timekeeping_is_valid()) {
        fputc(LOGGER_CSV_SEPARATOR, file);
        return;
    }

    struct tm now;
    timekeeping_get(&now);
    fprintf(file, "%02d/%02d/%04d%c%02d:%02d:%02d",
            now.tm_mday, now.tm_mon + 1, now.tm_year + 1900, LOGGER_CSV_SEPARATOR,
            now.tm_hour, now.tm_min, now.tm_sec);
}

/** Seconds since the session start, formatted in integer math: exact however long the run. */
static void logger_write_elapsed(FILE *file, uint64_t uptime_ms)
{
    const uint64_t elapsed_ms = (uptime_ms > s_session_start_ms) ? uptime_ms - s_session_start_ms : 0U;
    fprintf(file, "%c%" PRIu64 "%c%02u", LOGGER_CSV_SEPARATOR, elapsed_ms / 1000U,
            LOGGER_DECIMAL_MARK, (unsigned)((elapsed_ms % 1000U) / 10U));
}

static esp_err_t logger_commit_row(bool force_flush)
{
    if (ferror(s_log_file) != 0) {
        clearerr(s_log_file);
        s_fault = true;
        ESP_LOGE(TAG, "write failed for %s", s_current_path);
        return ESP_FAIL;
    }

    uint64_t now_ms = logger_get_uptime_ms();
    if (force_flush || (now_ms - s_last_flush_ms) >= LOGGER_FLUSH_PERIOD_MS) {
        if (logger_service_flush() != ESP_OK) {
            s_fault = true;
            return ESP_FAIL;
        }
        s_last_flush_ms = now_ms;
        /* After the flush, so the free count includes what was just written. */
        logger_reclaim_space();
    }

    return ESP_OK;
}

esp_err_t logger_service_init(void)
{
    s_log_file = NULL;
    s_active = false;
    s_initialized = true;
    s_current_path[0] = '\0';
    s_last_flush_ms = 0;
    return ESP_OK;
}

static esp_err_t logger_start_session(void)
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

    s_sample_count = 0;
    s_session_start_ms = 0;

    logger_write_header(s_log_file);
    s_active = true;
    s_last_flush_ms = logger_get_uptime_ms();

    /* Flush the header right away: a session that ends before its first
     * periodic flush still leaves a well-formed file behind. */
    if (logger_commit_row(true) != ESP_OK) {
        fclose(s_log_file);
        s_log_file = NULL;
        s_active = false;
        ESP_LOGE(TAG, "failed to write header to %s", s_current_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Logging to %s", s_current_path);
    return ESP_OK;
}

esp_err_t logger_service_start(void)
{
    if (s_active) {
        return ESP_OK;
    }

    const esp_err_t err = logger_start_session();
    s_fault = (err != ESP_OK);
    return err;
}

esp_err_t logger_service_stop(void)
{
    if (!s_active) {
        return ESP_OK;
    }

    const esp_err_t flush_err = logger_service_flush();
    const int close_result = fclose(s_log_file);
    s_log_file = NULL;
    s_active = false;

    ESP_RETURN_ON_ERROR(flush_err, TAG, "final flush failed for %s", s_current_path);
    ESP_RETURN_ON_FALSE(close_result == 0, ESP_FAIL, TAG, "fclose failed for %s: errno=%d",
                        s_current_path, errno);
    ESP_LOGI(TAG, "Logger stopped (%" PRIu32 " samples)", s_sample_count);
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

esp_err_t logger_service_log_sample(const kdl_sensor_sample_t *sample)
{
    ESP_RETURN_ON_FALSE(s_active && s_log_file != NULL, ESP_ERR_INVALID_STATE, TAG, "logger inactive");
    ESP_RETURN_ON_FALSE(sample != NULL, ESP_ERR_INVALID_ARG, TAG, "sample is required");

    if (s_sample_count == 0U) {
        s_session_start_ms = sample->uptime_ms;
    }

    logger_write_wall_clock(s_log_file);
    logger_write_elapsed(s_log_file, sample->uptime_ms);

    for (size_t index = 0; index < DATA_MODEL_THERMOCOUPLE_COUNT; ++index) {
        const bool valid = (sample->thermocouple_valid_mask & (1U << index)) != 0U;
        logger_write_value(s_log_file, valid, sample->thermocouples_c[index], LOGGER_TC_DECIMALS);
    }
    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        const bool valid = (sample->analog_valid_mask & (1U << index)) != 0U;
        logger_write_value(s_log_file, valid, sample->analog_inputs[index], LOGGER_AI_DECIMALS);
    }
    fputs(LOGGER_EOL, s_log_file);

    s_sample_count++;
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

uint32_t logger_service_get_sample_count(void)
{
    return s_sample_count;
}

bool logger_service_has_fault(void)
{
    return s_fault;
}
