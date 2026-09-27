#include "app_controller.h"

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "timekeeping.h"

#include "acquisition_service.h"
#include "fw_update.h"
#include "gui_actions.h"
#include "gui_service.h"
#include "log_viewer.h"
#include "logger_service.h"
#include "sensor_config.h"
#include "settings_service.h"
#include "storage_manager.h"
#include "trend_service.h"
#include "usb_msc_service.h"

/* NOTE: software-side flicker (partial-buffer SPI tearing) has been fixed.
 * Re-enabled to verify thermocouple readings. If electrical interference
 * between SPI2 (MAX31855) and SPI3 (display) reappears it is a hardware
 * issue (decoupling/grounding/layout) unrelated to the software flicker fix. */
#define KDL_ACQUISITION_ENABLED 1

#define APP_CONTROLLER_LOOP_PERIOD_MS 500U
/** Uninterrupted normal-mode run a freshly updated image must survive. Long
 *  enough to cover the whole power-up sequence -- 2 s splash, then the date
 *  prompt that dismisses itself after 10 s -- and invisible to the operator,
 *  who gets no notice on success. */
#define APP_CONTROLLER_SELF_TEST_US   (15LL * 1000LL * 1000LL)
/** Slack on top of two acquisition periods before a sample counts as stale. */
#define APP_CONTROLLER_SAMPLE_SLACK_MS 1000U
/** Lets the GUI draw "restarting" before the reset cuts it off. */
#define APP_CONTROLLER_RESTART_DELAY_MS 1500U

static const char *TAG = "app_controller";

/* Mode transitions come from the GUI task (USB key, eject) and from the
 * controller loop (firmware update): s_mode_mutex serialises them, so the
 * volume can never be handed to the USB host while an update reads it or the
 * viewer page is being written. */
static volatile app_mode_t s_app_mode = APP_MODE_NORMAL;
static SemaphoreHandle_t s_mode_mutex;
static StaticSemaphore_t s_mode_mutex_buffer;
/** Set at boot and after USB mode, i.e. whenever the volume may hold a new file. */
static volatile bool s_fw_scan_requested;
/** Same triggers: the operator may have deleted or changed the viewer page. */
static volatile bool s_viewer_check_requested;
/** Start of the current normal-mode run, for the post-update self-test. */
static volatile int64_t s_normal_since_us;

static bool app_controller_is_usb_msc_mode(void)
{
    return s_app_mode == APP_MODE_USB_MSC;
}

static esp_err_t app_controller_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* Partition layout/format changed (or is worn out): the emulated
         * EEPROM contents can't be trusted, so wipe and reinit rather than
         * fail boot over it -- settings will just fall back to defaults. */
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t app_controller_init(void)
{
    s_mode_mutex = xSemaphoreCreateMutexStatic(&s_mode_mutex_buffer);
    ESP_RETURN_ON_FALSE(s_mode_mutex != NULL, ESP_ERR_NO_MEM, TAG, "mode mutex creation failed");

    ESP_RETURN_ON_ERROR(app_controller_init_nvs(), TAG, "nvs init failed");
    /* Not fatal: the logger must still come up. Without the install record
     * the outcome of the last update just goes unreported. */
    const esp_err_t fw_err = fw_update_init();
    if (fw_err != ESP_OK) {
        ESP_LOGW(TAG, "fw update init failed: %s", esp_err_to_name(fw_err));
    }
    ESP_RETURN_ON_ERROR(settings_service_init(), TAG, "settings init failed");
    ESP_RETURN_ON_ERROR(timekeeping_init(), TAG, "timekeeping init failed");
    ESP_RETURN_ON_ERROR(storage_manager_init(), TAG, "storage init failed");
    ESP_RETURN_ON_ERROR(sensor_config_init(), TAG, "sensor config init failed");
    /* Not fatal: an unreadable file leaves the defaults in effect, and the
     * settings page reports it. */
    const esp_err_t config_err = sensor_config_load();
    if (config_err != ESP_OK) {
        ESP_LOGW(TAG, "sensor configuration not loaded: %s", esp_err_to_name(config_err));
    }
    ESP_RETURN_ON_ERROR(usb_msc_service_init(), TAG, "usb msc init failed");
    ESP_RETURN_ON_ERROR(logger_service_init(), TAG, "logger init failed");
    ESP_RETURN_ON_ERROR(trend_service_init(), TAG, "trend init failed");
    ESP_RETURN_ON_ERROR(acquisition_service_init(), TAG, "acquisition init failed");
    ESP_RETURN_ON_ERROR(gui_service_init(), TAG, "gui init failed");
    gui_actions_set_usb_msc_hooks(app_controller_enter_usb_msc_mode,
                                  app_controller_exit_usb_msc_mode,
                                  app_controller_is_usb_msc_mode);
    s_app_mode = APP_MODE_NORMAL;
    return ESP_OK;
}

esp_err_t app_controller_start(void)
{
    /* Recording is no longer forced on at boot: acquisition_service reads
     * AI1 (record-enable, GPIO13) every cycle and starts/stops the logger
     * to follow it. */
#if KDL_ACQUISITION_ENABLED
    ESP_RETURN_ON_ERROR(acquisition_service_start(), TAG, "acquisition start failed");
#else
    ESP_LOGW(TAG, "acquisition disabled (KDL_ACQUISITION_ENABLED=0): display/SPI2 interference under investigation");
#endif
    ESP_RETURN_ON_ERROR(gui_service_start(), TAG, "gui start failed");
    s_normal_since_us = esp_timer_get_time();
    s_fw_scan_requested = true;
    s_viewer_check_requested = true;
    ESP_LOGI(TAG, "KDatalogger services ready in normal mode");

    return ESP_OK;
}

app_mode_t app_controller_get_mode(void)
{
    return s_app_mode;
}

static esp_err_t app_controller_enter_usb_msc_mode_locked(void)
{
    if (s_app_mode == APP_MODE_USB_MSC) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(s_app_mode == APP_MODE_NORMAL, ESP_ERR_INVALID_STATE, TAG,
                        "usb msc refused in mode %d", (int)s_app_mode);

    if (acquisition_service_is_active()) {
        ESP_RETURN_ON_ERROR(acquisition_service_stop(), TAG, "acquisition stop failed");
    }

    if (logger_service_is_active()) {
        /* The file is closed even when the final flush fails, so the handoff
         * is still safe -- aborting here would only lock the operator out of
         * the logs that did make it to flash. A timeout is different: the
         * logger task may still have the file open. */
        const esp_err_t stop_err = logger_service_stop();
        if (stop_err == ESP_ERR_TIMEOUT) {
#if KDL_ACQUISITION_ENABLED
            const esp_err_t restart_err = acquisition_service_start();
            if (restart_err != ESP_OK) {
                ESP_LOGE(TAG, "acquisition restart failed: %s", esp_err_to_name(restart_err));
            }
#endif
            ESP_LOGE(TAG, "logger did not stop, USB handoff aborted");
            return stop_err;
        }
        if (stop_err != ESP_OK) {
            ESP_LOGW(TAG, "logger stop failed (%s), continuing USB handoff",
                     esp_err_to_name(stop_err));
        }
    }

    ESP_RETURN_ON_ERROR(storage_manager_prepare_for_usb_export(), TAG, "storage handoff failed");
    ESP_RETURN_ON_ERROR(usb_msc_service_start(), TAG, "usb start failed");
    s_app_mode = APP_MODE_USB_MSC;
    ESP_LOGI(TAG, "Entered USB MSC mode");
    return ESP_OK;
}

static esp_err_t app_controller_exit_usb_msc_mode_locked(void)
{
    if (s_app_mode != APP_MODE_USB_MSC) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(usb_msc_service_stop(), TAG, "usb stop failed");
    ESP_RETURN_ON_ERROR(storage_manager_resume_firmware_access(), TAG, "storage resume failed");
    /* The operator may have edited the configuration from the PC. Read
     * before acquisition restarts, so its first sample already uses it. */
    const esp_err_t config_err = sensor_config_load();
    if (config_err != ESP_OK) {
        ESP_LOGW(TAG, "sensor configuration not reloaded: %s", esp_err_to_name(config_err));
    }
    /* Recording resumes on its own, once acquisition_service samples AI1
     * again -- no explicit logger_service_start() here. */
#if KDL_ACQUISITION_ENABLED
    ESP_RETURN_ON_ERROR(acquisition_service_start(), TAG, "acquisition restart failed");
#endif
    s_normal_since_us = esp_timer_get_time();
    s_app_mode = APP_MODE_NORMAL;
    s_fw_scan_requested = true;
    s_viewer_check_requested = true;
    ESP_LOGI(TAG, "Returned to normal mode");
    return ESP_OK;
}

esp_err_t app_controller_enter_usb_msc_mode(void)
{
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_mode_mutex, portMAX_DELAY) == pdTRUE, ESP_FAIL, TAG,
                        "mode mutex take failed");
    const esp_err_t err = app_controller_enter_usb_msc_mode_locked();
    xSemaphoreGive(s_mode_mutex);
    return err;
}

esp_err_t app_controller_exit_usb_msc_mode(void)
{
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_mode_mutex, portMAX_DELAY) == pdTRUE, ESP_FAIL, TAG,
                        "mode mutex take failed");
    const esp_err_t err = app_controller_exit_usb_msc_mode_locked();
    xSemaphoreGive(s_mode_mutex);
    return err;
}

#if KDL_ACQUISITION_ENABLED
/**
 * The acquisition task publishes a sample every cycle whatever the sensors
 * report, so a fresh timestamp proves the task runs, not that probes are
 * plugged in -- a bench without thermocouples must not trigger a rollback.
 *
 * @return ESP_OK if live, ESP_ERR_TIMEOUT to ask again later, ESP_FAIL if stalled.
 */
static esp_err_t app_controller_check_acquisition(void)
{
    if (!acquisition_service_is_active()) {
        return ESP_FAIL;
    }

    kdl_sensor_sample_t sample;
    const esp_err_t err = acquisition_service_get_latest_sample(&sample);
    if (err == ESP_ERR_TIMEOUT) {
        return ESP_ERR_TIMEOUT;
    }
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    const uint64_t now_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
    const uint64_t max_age_ms = 2ULL * settings_service_get_acquisition_period_ms()
                                + APP_CONTROLLER_SAMPLE_SLACK_MS;
    return (sample.uptime_ms != 0 && now_ms - sample.uptime_ms <= max_age_ms) ? ESP_OK : ESP_FAIL;
}
#endif

/**
 * Confirm a freshly updated image after a stretch of normal operation, or
 * reject it. The window restarts on every return from USB mode: acquisition
 * is stopped there and its last sample goes stale. A hang or crash before
 * the verdict resets the chip, and the bootloader rolls back on its own.
 *
 * Runs under the mode mutex: a switch to USB mode halfway through would make
 * the volume unreadable and pass for a failed test.
 */
static void app_controller_self_test_locked(void)
{
    if (s_app_mode != APP_MODE_NORMAL
        || esp_timer_get_time() - s_normal_since_us < APP_CONTROLLER_SELF_TEST_US) {
        return;
    }

    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    const esp_err_t storage_err = storage_manager_get_usage(&total_bytes, &free_bytes);
    esp_err_t acquisition_err = ESP_OK;
#if KDL_ACQUISITION_ENABLED
    acquisition_err = app_controller_check_acquisition();
    if (acquisition_err == ESP_ERR_TIMEOUT) {
        return;
    }
#endif

    if (storage_err == ESP_OK && acquisition_err == ESP_OK) {
        const esp_err_t err = fw_update_confirm_running();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "confirming the new image failed: %s", esp_err_to_name(err));
        }
        return;
    }

    ESP_LOGE(TAG, "self-test failed (storage %s, acquisition %s)",
             esp_err_to_name(storage_err), esp_err_to_name(acquisition_err));
    const esp_err_t err = fw_update_reject_running();
    ESP_LOGE(TAG, "rollback failed: %s", esp_err_to_name(err));
}

static void app_controller_self_test(void)
{
    if (!fw_update_is_pending_verify()) {
        return;
    }
    if (xSemaphoreTake(s_mode_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "mode mutex take failed");
        return;
    }
    app_controller_self_test_locked();
    xSemaphoreGive(s_mode_mutex);
}

/** Stop everything that touches the volume or the flash, for the install. */
static esp_err_t app_controller_quiesce_for_update(void)
{
    if (acquisition_service_is_active()) {
        ESP_RETURN_ON_ERROR(acquisition_service_stop(), TAG, "acquisition stop failed");
    }
    if (logger_service_is_active()) {
        const esp_err_t err = logger_service_stop();
        /* Timed out: the file may still be open, so the flash is not ours to rewrite. */
        ESP_RETURN_ON_FALSE(err != ESP_ERR_TIMEOUT, err, TAG, "logger did not stop");
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "logger stop failed (%s), continuing update", esp_err_to_name(err));
        }
    }
    return ESP_OK;
}

static void app_controller_leave_update_mode(void)
{
#if KDL_ACQUISITION_ENABLED
    if (!acquisition_service_is_active()) {
        const esp_err_t err = acquisition_service_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "acquisition restart failed: %s", esp_err_to_name(err));
        }
    }
#endif
    if (xSemaphoreTake(s_mode_mutex, portMAX_DELAY) == pdTRUE) {
        s_normal_since_us = esp_timer_get_time();
        s_app_mode = APP_MODE_NORMAL;
        xSemaphoreGive(s_mode_mutex);
    }
}

/**
 * Look for an update file and install it. Never cuts a recording short: a
 * request made while the logger runs waits until the session ends. The scan
 * holds the mode mutex so the volume cannot be handed to the USB host under
 * it; the install itself runs in APP_MODE_FW_UPDATE, which keeps USB mode out.
 */
static void app_controller_service_fw_update(void)
{
    if (!s_fw_scan_requested || fw_update_is_pending_verify() || logger_service_is_active()) {
        return;
    }
    if (xSemaphoreTake(s_mode_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "mode mutex take failed");
        return;
    }
    if (s_app_mode != APP_MODE_NORMAL) {
        xSemaphoreGive(s_mode_mutex);
        return;
    }

    s_fw_scan_requested = false;
    fw_update_candidate_t candidate;
    const esp_err_t scan_err = fw_update_scan(STORAGE_MOUNT_PATH, &candidate);
    if (scan_err != ESP_OK) {
        xSemaphoreGive(s_mode_mutex);
        if (scan_err != ESP_ERR_NOT_FOUND && scan_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "update scan failed: %s", esp_err_to_name(scan_err));
        }
        return;
    }
    s_app_mode = APP_MODE_FW_UPDATE;
    xSemaphoreGive(s_mode_mutex);

    esp_err_t err = app_controller_quiesce_for_update();
    if (err == ESP_OK) {
        err = fw_update_install(&candidate);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "update to %s not installed: %s", candidate.version, esp_err_to_name(err));
        app_controller_leave_update_mode();
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(APP_CONTROLLER_RESTART_DELAY_MS));
    esp_restart();
}

/**
 * Put the viewer page back on the volume if it is missing or stale. Like the
 * update scan, it waits for the end of a recording -- a rewrite erases flash
 * for long enough to back up the logger queue -- and holds the mode mutex, so
 * the volume cannot go to the USB host mid-write. A USB request made during a
 * rewrite (first boot after an update, or after the page was deleted) waits
 * for it to finish.
 */
static void app_controller_service_log_viewer(void)
{
    if (!s_viewer_check_requested || logger_service_is_active()) {
        return;
    }
    if (xSemaphoreTake(s_mode_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "mode mutex take failed");
        return;
    }
    if (s_app_mode == APP_MODE_NORMAL) {
        /* Not retried on failure until the next boot or USB session: a
         * volume that rejects the write would otherwise be hammered. */
        s_viewer_check_requested = false;
        const esp_err_t err = log_viewer_install();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "log viewer page not installed: %s", esp_err_to_name(err));
        }
    }
    xSemaphoreGive(s_mode_mutex);
}

void app_controller_run(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(APP_CONTROLLER_LOOP_PERIOD_MS));
        app_controller_self_test();
        app_controller_service_fw_update();
        app_controller_service_log_viewer();
    }
}
