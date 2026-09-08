#include "timekeeping.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

#define TIMEKEEPING_NVS_NAMESPACE "kdl_time"
#define TIMEKEEPING_NVS_KEY_LAST  "last_set"

/* 2026-01-01 00:00:00. Used both as the setup-mask default on a device that
 * has never had its clock set and as the placeholder FAT timestamp, so an
 * unset clock produces an obviously-wrong-but-stable date rather than the
 * 1980 epoch FAT would otherwise fall back to. */
#define TIMEKEEPING_DEFAULT_EPOCH ((time_t)1767225600)

static const char *TAG = "timekeeping";

static bool s_valid;
static time_t s_setup_default = TIMEKEEPING_DEFAULT_EPOCH;

static void timekeeping_apply_epoch(time_t epoch)
{
    const struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

esp_err_t timekeeping_init(void)
{
    /* Pin the zone so mktime()/localtime_r() are exact inverses: the operator
     * enters a wall time and reads back the same wall time, with no DST or
     * offset applied in between. */
    setenv("TZ", "UTC0", 1);
    tzset();

    s_valid = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(TIMEKEEPING_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        /* First boot: the namespace does not exist yet. */
        timekeeping_apply_epoch(s_setup_default);
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open failed");

    int64_t stored = 0;
    err = nvs_get_i64(handle, TIMEKEEPING_NVS_KEY_LAST, &stored);
    nvs_close(handle);

    if (err == ESP_OK)
    {
        s_setup_default = (time_t)stored;
    }
    else if (err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "reading last set time failed: %s", esp_err_to_name(err));
    }

    /* Seed the system clock with the last known value so timestamps are at
     * least plausible before the operator confirms; s_valid stays false so
     * the UI still shows the clock as unset. */
    timekeeping_apply_epoch(s_setup_default);
    return ESP_OK;
}

bool timekeeping_is_valid(void)
{
    return s_valid;
}

esp_err_t timekeeping_set(const struct tm *local)
{
    ESP_RETURN_ON_FALSE(local != NULL, ESP_ERR_INVALID_ARG, TAG, "null time");

    struct tm normalized = *local;
    /* mktime() normalizes out-of-range fields in place, which is exactly what
     * an increment-based setup mask produces (e.g. minute 60 after a wrap). */
    const time_t epoch = mktime(&normalized);
    ESP_RETURN_ON_FALSE(epoch != (time_t)-1, ESP_ERR_INVALID_ARG, TAG, "unrepresentable time");

    timekeeping_apply_epoch(epoch);
    s_valid = true;
    s_setup_default = epoch;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(TIMEKEEPING_NVS_NAMESPACE, NVS_READWRITE, &handle);
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_open failed");

    err = nvs_set_i64(handle, TIMEKEEPING_NVS_KEY_LAST, (int64_t)epoch);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_RETURN_ON_ERROR(err, TAG, "persisting last set time failed");

    ESP_LOGI(TAG, "clock set to %04d-%02d-%02d %02d:%02d:%02d",
             normalized.tm_year + 1900, normalized.tm_mon + 1, normalized.tm_mday,
             normalized.tm_hour, normalized.tm_min, normalized.tm_sec);
    return ESP_OK;
}

void timekeeping_get(struct tm *out)
{
    if (out == NULL)
    {
        return;
    }

    const time_t now = time(NULL);
    localtime_r(&now, out);
}

void timekeeping_get_setup_default(struct tm *out)
{
    if (out == NULL)
    {
        return;
    }

    /* Once the clock is running, the mask should open on "now" rather than on
     * the stale value that was stored at the last set. */
    const time_t seed = s_valid ? time(NULL) : s_setup_default;
    localtime_r(&seed, out);
}

unsigned int timekeeping_fattime(void)
{
    struct tm now;
    const time_t seed = s_valid ? time(NULL) : TIMEKEEPING_DEFAULT_EPOCH;
    localtime_r(&seed, &now);

    /* Packed FAT timestamp: yyyyyyym mmmddddd hhhhhmmm mmmsssss, with the
     * year relative to 1980 and seconds in 2 s units. */
    return ((unsigned int)(now.tm_year + 1900 - 1980) << 25)
           | ((unsigned int)(now.tm_mon + 1) << 21)
           | ((unsigned int)now.tm_mday << 16)
           | ((unsigned int)now.tm_hour << 11)
           | ((unsigned int)now.tm_min << 5)
           | ((unsigned int)(now.tm_sec / 2));
}
