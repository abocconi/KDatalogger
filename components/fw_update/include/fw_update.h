#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @file fw_update.h
 * @brief Firmware update from a .bin file left on the data volume.
 *
 * The operator copies the application image (build/KDatalogger.bin, under
 * any name ending in .bin) to the root of the USB volume. A file is taken
 * for an update only if its content says so -- ESP image header for this
 * chip, app descriptor with this project's name -- so anything else copied
 * there, including the "._*" companions macOS writes on FAT, is left alone.
 *
 * The image goes to the OTA slot that is not running and boots pending
 * verification: the caller confirms it with fw_update_confirm_running()
 * once the system is up, otherwise the bootloader returns to the previous
 * image on the next reset. The install is recorded in NVS across that reset
 * so a rollback can be reported as a notice (fw_update_get_status()); a
 * successful install raises none -- the version shown on screen tells.
 *
 * Not thread-safe except for fw_update_get_status() and
 * fw_update_ack_notice(): the rest must be called from a single task.
 */

#define FW_UPDATE_PATH_MAX    128
#define FW_UPDATE_VERSION_MAX 32

/** A validated update image on the volume. */
typedef struct {
    char path[FW_UPDATE_PATH_MAX];
    char version[FW_UPDATE_VERSION_MAX];
    size_t image_size; /**< Bytes of the image proper, as walked from its header */
} fw_update_candidate_t;

typedef enum {
    FW_UPDATE_PHASE_IDLE = 0,
    FW_UPDATE_PHASE_WRITING,    /**< Image being written, progress valid */
    FW_UPDATE_PHASE_RESTARTING, /**< Installed; the caller is about to reset */
} fw_update_phase_t;

/** Outcome waiting to be shown to the operator; cleared by fw_update_ack_notice(). */
typedef enum {
    FW_UPDATE_NOTICE_NONE = 0,
    FW_UPDATE_NOTICE_ROLLED_BACK,       /**< `version` did not come up; previous image restored */
    FW_UPDATE_NOTICE_INVALID_FILE,      /**< Update file damaged; renamed to *.bad */
    FW_UPDATE_NOTICE_MULTIPLE_FILES,    /**< More than one update file; none installed */
    FW_UPDATE_NOTICE_ALREADY_INSTALLED, /**< File holds the running image; removed */
    FW_UPDATE_NOTICE_WRITE_FAILED,      /**< Flash or file I/O error; firmware unchanged */
} fw_update_notice_t;

typedef struct {
    fw_update_phase_t phase;
    uint8_t progress_percent;
    fw_update_notice_t notice;
    /** Version the phase or notice refers to; empty when there is none. */
    char version[FW_UPDATE_VERSION_MAX];
} fw_update_status_t;

/**
 * @brief Read the running image state and the outcome of the last install.
 *
 * Requires nvs_flash_init(). Raises FW_UPDATE_NOTICE_ROLLED_BACK when the
 * image recorded by the last install is not the one running.
 */
esp_err_t fw_update_init(void);

/**
 * @brief Whether the running image still waits for fw_update_confirm_running().
 *
 * No update can be installed in this state (esp_ota_begin() refuses it).
 */
bool fw_update_is_pending_verify(void);

/**
 * @brief Mark the running image valid and cancel the rollback.
 *
 * No-op when the image is not pending verification.
 */
esp_err_t fw_update_confirm_running(void);

/**
 * @brief Give up on the running image: mark it invalid and reboot into the
 *        previous one. Returns only on failure.
 */
esp_err_t fw_update_reject_running(void);

/**
 * @brief Look for an update image in the root of @p dir.
 *
 * Tidies the volume while at it: a file holding the running image is deleted
 * (FW_UPDATE_NOTICE_ALREADY_INSTALLED), a damaged update file is renamed to
 * "<name>.bad" (FW_UPDATE_NOTICE_INVALID_FILE). Files that are not an image
 * of this project are ignored.
 *
 * @return ESP_OK with @p out filled when exactly one update image is found;
 *         ESP_ERR_NOT_FOUND when there is none; ESP_ERR_INVALID_STATE when
 *         there are several (FW_UPDATE_NOTICE_MULTIPLE_FILES); another error
 *         if @p dir cannot be read.
 */
esp_err_t fw_update_scan(const char *dir, fw_update_candidate_t *out);

/**
 * @brief Write @p candidate to the inactive OTA slot and make it the boot image.
 *
 * Blocks for several seconds, yielding between chunks. On success the file
 * has been deleted and the phase is FW_UPDATE_PHASE_RESTARTING: the caller
 * resets with esp_restart(). On failure the running firmware is untouched,
 * the phase is back to idle and a notice says why.
 */
esp_err_t fw_update_install(const fw_update_candidate_t *candidate);

/** @brief Snapshot of the update state for the GUI. Safe from any task. */
void fw_update_get_status(fw_update_status_t *out);

/** @brief Drop the pending notice once the operator has seen it. Safe from any task. */
void fw_update_ack_notice(void);
