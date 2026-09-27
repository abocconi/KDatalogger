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
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "data_model_channels.h"
#include "storage_manager.h"
#include "timekeeping.h"

#define LOGGER_TASK_NAME "logger"
/* Same budget the file I/O had when it ran inside the acquisition task. */
#define LOGGER_TASK_STACK_SIZE 4096
/* Below acquisition (3): file I/O must never delay a sample. While this task
 * is stuck in a flush, samples wait in the queue. */
#define LOGGER_TASK_PRIORITY 2
/* Backlog absorbed while the task is stalled on flash: 5 s at the fastest
 * acquisition period (100 ms). An fsync on FAT + wear levelling with 512 B
 * sectors erases one 4 KB flash sector per FAT sector written, a few hundred
 * ms in total; a session start (directory scan, reclaim, header) can take more. */
#define LOGGER_QUEUE_LENGTH 50U
/* Slots samples may not take, so a start/stop request still gets through
 * when the backlog is full. */
#define LOGGER_QUEUE_CONTROL_SLOTS 2U
_Static_assert(LOGGER_QUEUE_LENGTH > LOGGER_QUEUE_CONTROL_SLOTS,
               "the log queue must leave room for samples");
/* A session whose file cannot be opened is retried at this pace, as long as
 * recording is requested, instead of on every sample. */
#define LOGGER_OPEN_RETRY_MS 1000ULL
/* Bound on logger_service_stop(): draining a full queue plus the final fsync
 * takes well under a second; this only catches a wedged flash. */
#define LOGGER_STOP_TIMEOUT_MS 10000U

#define LOGGER_FILE_PREFIX "log_"
#define LOGGER_FILE_EXTENSION ".csv"
#define LOGGER_PATH_MAX_LEN 96
/* Time-based rather than sample-based: the acquisition period is configurable
 * (100 ms - 5 s), and a per-sample count would turn into an fsync every 500 ms
 * at the fast end -- needless flash wear, since each fsync on FAT+wear_levelling
 * erases several flash sectors. */
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
#define LOGGER_PRESSURE_DECIMALS 2
#define LOGGER_RPM_DECIMALS 0

static const char *TAG = "logger";

typedef enum {
    LOGGER_MSG_START,
    LOGGER_MSG_SAMPLE,
    LOGGER_MSG_STOP,
    LOGGER_MSG_STOP_SYNC, /**< STOP that signals s_stop_done once the file is closed */
} logger_msg_type_t;

typedef struct {
    logger_msg_type_t type;
    /* Wall clock read when the sample was queued, not when it is written:
     * the row may be written seconds later after a stall. */
    bool wall_clock_valid;
    time_t wall_clock;
    kdl_sensor_sample_t sample;
} logger_msg_t;

/* Everything below up to s_session_start_ms is owned by the logger task. */
static FILE *s_log_file;
static bool s_initialized;
/** Whether the session file is open; read from other tasks. */
static volatile bool s_file_open;
static char s_current_path[LOGGER_PATH_MAX_LEN];
static uint32_t s_current_index;
/** Sticky until the next successful start; read from the GUI task. */
static volatile bool s_fault;
static volatile uint32_t s_sample_count;
static uint64_t s_last_flush_ms;
/** Uptime of the first sample of the session: the zero of the "Tempo" column. */
static uint64_t s_session_start_ms;

/* Caller side, flipped when a request is queued: the recording state as the
 * rest of the firmware sees it, ahead of the task catching up. */
static volatile bool s_requested;
/** Samples dropped on a full queue in this session; written by the producer only. */
static volatile uint32_t s_dropped_samples;

static QueueHandle_t s_queue;
static StaticQueue_t s_queue_buffer;
static uint8_t s_queue_storage[LOGGER_QUEUE_LENGTH * sizeof(logger_msg_t)];
static StaticTask_t s_task_buffer;
static StackType_t s_task_stack[LOGGER_TASK_STACK_SIZE];
/** Given by the task after a LOGGER_MSG_STOP_SYNC, with s_stop_result set. */
static SemaphoreHandle_t s_stop_done;
static StaticSemaphore_t s_stop_done_buffer;
/** Serializes logger_service_stop() callers, which share s_stop_done. */
static SemaphoreHandle_t s_stop_mutex;
static StaticSemaphore_t s_stop_mutex_buffer;
static esp_err_t s_stop_result;

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
    if (scan.count == 0U || (s_file_open && scan.min_index == s_current_index)) {
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
    fprintf(file, ";%s [%s]", data_model_rpm_channel.name, data_model_rpm_channel.unit);
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
static void logger_write_wall_clock(FILE *file, bool valid, time_t wall_clock)
{
    if (!valid) {
        fputc(LOGGER_CSV_SEPARATOR, file);
        return;
    }

    struct tm now;
    localtime_r(&wall_clock, &now);
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

static esp_err_t logger_flush(void)
{
    if (!s_file_open || s_log_file == NULL) {
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
        if (logger_flush() != ESP_OK) {
            s_fault = true;
            return ESP_FAIL;
        }
        s_last_flush_ms = now_ms;
        /* After the flush, so the free count includes what was just written. */
        logger_reclaim_space();
    }

    return ESP_OK;
}

static esp_err_t logger_start_session(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "logger not initialized");
    ESP_RETURN_ON_FALSE(storage_manager_get_owner() == STORAGE_OWNER_FIRMWARE,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "storage is not owned by firmware");

    if (s_file_open) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(logger_ensure_directory(), TAG, "directory init failed");
    ESP_RETURN_ON_ERROR(logger_resolve_next_path(), TAG, "path resolution failed");

    s_log_file = fopen(s_current_path, "w");
    ESP_RETURN_ON_FALSE(s_log_file != NULL, ESP_FAIL, TAG, "failed to open %s", s_current_path);

    s_sample_count = 0;
    s_session_start_ms = 0;

    logger_write_header(s_log_file);
    s_file_open = true;
    s_last_flush_ms = logger_get_uptime_ms();

    /* Flush the header right away: a session that ends before its first
     * periodic flush still leaves a well-formed file behind. */
    if (logger_commit_row(true) != ESP_OK) {
        fclose(s_log_file);
        s_log_file = NULL;
        s_file_open = false;
        ESP_LOGE(TAG, "failed to write header to %s", s_current_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Logging to %s", s_current_path);
    return ESP_OK;
}

static esp_err_t logger_open_session(void)
{
    if (s_file_open) {
        return ESP_OK;
    }

    const esp_err_t err = logger_start_session();
    s_fault = (err != ESP_OK);
    return err;
}

static esp_err_t logger_close_session(void)
{
    if (!s_file_open) {
        return ESP_OK;
    }

    const esp_err_t flush_err = logger_flush();
    const int close_result = fclose(s_log_file);
    s_log_file = NULL;
    s_file_open = false;

    ESP_RETURN_ON_ERROR(flush_err, TAG, "final flush failed for %s", s_current_path);
    ESP_RETURN_ON_FALSE(close_result == 0, ESP_FAIL, TAG, "fclose failed for %s: errno=%d",
                        s_current_path, errno);
    ESP_LOGI(TAG, "Logger stopped (%" PRIu32 " samples, %" PRIu32 " dropped)",
             s_sample_count, s_dropped_samples);
    return ESP_OK;
}

static esp_err_t logger_write_row(const logger_msg_t *msg)
{
    const kdl_sensor_sample_t *sample = &msg->sample;

    if (s_sample_count == 0U) {
        s_session_start_ms = sample->uptime_ms;
    }

    logger_write_wall_clock(s_log_file, msg->wall_clock_valid, msg->wall_clock);
    logger_write_elapsed(s_log_file, sample->uptime_ms);

    for (size_t index = 0; index < DATA_MODEL_THERMOCOUPLE_COUNT; ++index) {
        const bool valid = (sample->thermocouple_valid_mask & (1U << index)) != 0U;
        logger_write_value(s_log_file, valid, sample->thermocouples_c[index], LOGGER_TC_DECIMALS);
    }
    /* A disabled input or a sensor fault leaves the cell empty, like an
     * unplugged thermocouple. */
    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        const bool valid = (sample->pressure_valid_mask & (1U << index)) != 0U;
        logger_write_value(s_log_file, valid, sample->pressures_bar[index],
                           LOGGER_PRESSURE_DECIMALS);
    }
    logger_write_value(s_log_file, sample->engine_rpm_valid, sample->engine_rpm,
                       LOGGER_RPM_DECIMALS);
    fputs(LOGGER_EOL, s_log_file);

    s_sample_count++;
    return logger_commit_row(false);
}

/**
 * Owns the log file. Messages are handled in queue order, so a stop always
 * lands after the samples taken before it, and a start before the samples
 * taken after it. Failures are logged and latched in s_fault where they
 * happen; there is no caller left to return them to.
 */
static void logger_task(void *arg)
{
    (void)arg;

    /* The session as the queue has delivered it so far; s_requested runs
     * ahead of it by whatever is still queued. */
    bool session_wanted = false;
    uint64_t next_open_retry_ms = 0;

    for (;;) {
        logger_msg_t msg;
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (msg.type) {
        case LOGGER_MSG_START:
            session_wanted = true;
            (void)logger_open_session();
            next_open_retry_ms = logger_get_uptime_ms() + LOGGER_OPEN_RETRY_MS;
            break;

        case LOGGER_MSG_SAMPLE:
            if (!s_file_open && session_wanted && logger_get_uptime_ms() >= next_open_retry_ms) {
                (void)logger_open_session();
                next_open_retry_ms = logger_get_uptime_ms() + LOGGER_OPEN_RETRY_MS;
            }
            /* Without an open file the sample is lost: either no session is
             * wanted, or opening failed and s_fault already says so. */
            if (s_file_open) {
                (void)logger_write_row(&msg);
            }
            break;

        case LOGGER_MSG_STOP:
            session_wanted = false;
            (void)logger_close_session();
            break;

        case LOGGER_MSG_STOP_SYNC:
            session_wanted = false;
            s_stop_result = logger_close_session();
            (void)xSemaphoreGive(s_stop_done);
            break;

        default:
            ESP_LOGE(TAG, "unknown message %d", (int)msg.type);
            break;
        }
    }
}

esp_err_t logger_service_init(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }

    s_log_file = NULL;
    s_file_open = false;
    s_requested = false;
    s_current_path[0] = '\0';
    s_last_flush_ms = 0;

    s_queue = xQueueCreateStatic(LOGGER_QUEUE_LENGTH, sizeof(logger_msg_t), s_queue_storage,
                                 &s_queue_buffer);
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_FAIL, TAG, "failed to create queue");
    s_stop_done = xSemaphoreCreateBinaryStatic(&s_stop_done_buffer);
    ESP_RETURN_ON_FALSE(s_stop_done != NULL, ESP_FAIL, TAG, "failed to create stop signal");
    s_stop_mutex = xSemaphoreCreateMutexStatic(&s_stop_mutex_buffer);
    ESP_RETURN_ON_FALSE(s_stop_mutex != NULL, ESP_FAIL, TAG, "failed to create stop mutex");

    s_initialized = true;
    const TaskHandle_t task = xTaskCreateStatic(logger_task, LOGGER_TASK_NAME,
                                                LOGGER_TASK_STACK_SIZE, NULL,
                                                LOGGER_TASK_PRIORITY, s_task_stack,
                                                &s_task_buffer);
    ESP_RETURN_ON_FALSE(task != NULL, ESP_FAIL, TAG, "failed to create logger task");
    return ESP_OK;
}

esp_err_t logger_service_request_start(void)
{
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "logger not initialized");

    const logger_msg_t msg = { .type = LOGGER_MSG_START };
    if (xQueueSendToBack(s_queue, &msg, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_dropped_samples = 0;
    s_requested = true;
    return ESP_OK;
}

esp_err_t logger_service_request_stop(void)
{
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "logger not initialized");

    const logger_msg_t msg = { .type = LOGGER_MSG_STOP };
    if (xQueueSendToBack(s_queue, &msg, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_requested = false;
    return ESP_OK;
}

esp_err_t logger_service_submit_sample(const kdl_sensor_sample_t *sample)
{
    ESP_RETURN_ON_FALSE(sample != NULL, ESP_ERR_INVALID_ARG, TAG, "sample is required");
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "logger not initialized");

    /* Single producer: free space can only grow between this check and the
     * send, so the send below cannot eat into the control slots. */
    if (uxQueueSpacesAvailable(s_queue) <= LOGGER_QUEUE_CONTROL_SLOTS) {
        if (s_dropped_samples == 0U) {
            ESP_LOGW(TAG, "log queue full, dropping samples");
        }
        s_dropped_samples++;
        return ESP_ERR_TIMEOUT;
    }

    const logger_msg_t msg = {
        .type = LOGGER_MSG_SAMPLE,
        .wall_clock_valid = timekeeping_is_valid(),
        .wall_clock = time(NULL),
        .sample = *sample,
    };
    ESP_RETURN_ON_FALSE(xQueueSendToBack(s_queue, &msg, 0) == pdTRUE, ESP_ERR_TIMEOUT, TAG,
                        "log queue send failed");
    return ESP_OK;
}

esp_err_t logger_service_stop(void)
{
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "logger not initialized");

    const TickType_t timeout = pdMS_TO_TICKS(LOGGER_STOP_TIMEOUT_MS);
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_stop_mutex, timeout) == pdTRUE, ESP_ERR_TIMEOUT, TAG,
                        "another stop is still pending");

    s_requested = false;
    /* A completion left behind by an earlier stop that timed out. If that
     * stop is still queued instead, it closes the file first and this call
     * returns on its signal, which is equally valid: nothing is queued
     * between the two, as the producer is already stopped. */
    (void)xSemaphoreTake(s_stop_done, 0);

    esp_err_t err = ESP_ERR_TIMEOUT;
    const logger_msg_t msg = { .type = LOGGER_MSG_STOP_SYNC };
    if (xQueueSendToBack(s_queue, &msg, timeout) != pdTRUE) {
        ESP_LOGE(TAG, "stop request not queued in %u ms", LOGGER_STOP_TIMEOUT_MS);
    } else if (xSemaphoreTake(s_stop_done, timeout) != pdTRUE) {
        ESP_LOGE(TAG, "log file not closed in %u ms", LOGGER_STOP_TIMEOUT_MS);
    } else {
        err = s_stop_result;
    }

    (void)xSemaphoreGive(s_stop_mutex);
    return err;
}

bool logger_service_is_active(void)
{
    return s_requested || s_file_open;
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
    return s_fault || s_dropped_samples != 0U;
}
