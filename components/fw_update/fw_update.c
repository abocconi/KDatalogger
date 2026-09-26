#include "fw_update.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "fw_update";

/** Update candidates examined per scan; more .bin files than this is an operator mess anyway. */
#define FW_UPDATE_MAX_FILES     8
/** Flash sector size: esp_ota_write() with sequential writes erases one sector at a time. */
#define FW_UPDATE_CHUNK_SIZE    4096
#define FW_UPDATE_BIN_EXT       ".bin"
#define FW_UPDATE_BAD_SUFFIX    ".bad"
#define FW_UPDATE_BAD_PATH_MAX  (FW_UPDATE_PATH_MAX + sizeof(FW_UPDATE_BAD_SUFFIX))
#define FW_UPDATE_SHA_LEN       32
/** esp_image_format.c: checksum byte, then padding to a 16-byte boundary. */
#define FW_UPDATE_IMAGE_ALIGN   16U

#define FW_UPDATE_NVS_NAMESPACE "fw_update"
#define FW_UPDATE_NVS_KEY_SHA   "pend_sha"
#define FW_UPDATE_NVS_KEY_VER   "pend_ver"

_Static_assert(sizeof(((esp_app_desc_t *)0)->app_elf_sha256) == FW_UPDATE_SHA_LEN,
               "app descriptor digest size changed");
_Static_assert(sizeof(((esp_app_desc_t *)0)->version) == FW_UPDATE_VERSION_MAX,
               "app descriptor version size changed");

typedef enum {
    FW_IMAGE_FOREIGN = 0, /**< Not an image of this project: leave the file alone */
    FW_IMAGE_DAMAGED,     /**< Ours, but truncated, malformed or too big for a slot */
    FW_IMAGE_RUNNING,     /**< Ours and identical to the running image */
    FW_IMAGE_UPDATE,      /**< Ours, structurally sound, different from the running one */
} fw_image_kind_t;

/** Image recorded by the last install, to tell success from rollback after the reset. */
typedef struct {
    bool present;
    uint8_t sha[FW_UPDATE_SHA_LEN];
    char version[FW_UPDATE_VERSION_MAX];
} fw_update_record_t;

static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static fw_update_status_t s_status;
static bool s_pending_verify;
static fw_update_record_t s_record;
/** This boot is the first run of an installed update: a copy of it left on
 *  the volume is the file the install could not delete, not a new request. */
static bool s_booted_from_update;
static uint8_t s_chunk[FW_UPDATE_CHUNK_SIZE];
static char s_scan_paths[FW_UPDATE_MAX_FILES][FW_UPDATE_PATH_MAX];

static void fw_update_set_phase(fw_update_phase_t phase, uint8_t progress_percent,
                                const char *version)
{
    taskENTER_CRITICAL(&s_status_lock);
    s_status.phase = phase;
    s_status.progress_percent = progress_percent;
    if (version != NULL) {
        strlcpy(s_status.version, version, sizeof(s_status.version));
    }
    taskEXIT_CRITICAL(&s_status_lock);
}

/**
 * "Already installed" is the only notice that yields to one already pending:
 * it is informational, and must not hide a rollback or a failure the
 * operator has not seen yet.
 */
static void fw_update_raise_notice(fw_update_notice_t notice, const char *version)
{
    taskENTER_CRITICAL(&s_status_lock);
    if (notice != FW_UPDATE_NOTICE_ALREADY_INSTALLED || s_status.notice == FW_UPDATE_NOTICE_NONE) {
        s_status.notice = notice;
        strlcpy(s_status.version, version != NULL ? version : "", sizeof(s_status.version));
    }
    taskEXIT_CRITICAL(&s_status_lock);
}

static esp_err_t fw_update_record_load(fw_update_record_t *record)
{
    memset(record, 0, sizeof(*record));

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(FW_UPDATE_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK; /* Namespace never written: no install recorded */
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs open failed");

    size_t sha_len = sizeof(record->sha);
    size_t ver_len = sizeof(record->version);
    err = nvs_get_blob(nvs, FW_UPDATE_NVS_KEY_SHA, record->sha, &sha_len);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, FW_UPDATE_NVS_KEY_VER, record->version, &ver_len);
    }
    nvs_close(nvs);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs read failed");
    ESP_RETURN_ON_FALSE(sha_len == sizeof(record->sha), ESP_ERR_INVALID_SIZE, TAG,
                        "stored digest has the wrong size");
    record->present = true;
    return ESP_OK;
}

static esp_err_t fw_update_record_store(const esp_app_desc_t *desc)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(FW_UPDATE_NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG,
                        "nvs open failed");

    esp_err_t err = nvs_set_blob(nvs, FW_UPDATE_NVS_KEY_SHA, desc->app_elf_sha256,
                                 sizeof(desc->app_elf_sha256));
    if (err == ESP_OK) {
        char version[FW_UPDATE_VERSION_MAX];
        strlcpy(version, desc->version, sizeof(version));
        err = nvs_set_str(nvs, FW_UPDATE_NVS_KEY_VER, version);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t fw_update_record_clear(void)
{
    s_record.present = false;

    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(FW_UPDATE_NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG,
                        "nvs open failed");

    esp_err_t err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static bool fw_update_record_is_running(void)
{
    const esp_app_desc_t *running = esp_app_get_description();
    return s_record.present
           && memcmp(s_record.sha, running->app_elf_sha256, FW_UPDATE_SHA_LEN) == 0;
}

esp_err_t fw_update_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_RETURN_ON_FALSE(running != NULL, ESP_ERR_NOT_FOUND, TAG, "running partition unknown");

    /* ESP_ERR_NOT_FOUND here just means otadata is blank, as after a serial
     * flash: the image was never installed through OTA, so it is valid. */
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t state_err = esp_ota_get_state_partition(running, &state);
    s_pending_verify = (state_err == ESP_OK) && (state == ESP_OTA_IMG_PENDING_VERIFY);

    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "Running %s %s from %s%s", desc->project_name, desc->version, running->label,
             s_pending_verify ? " (pending verification)" : "");

    ESP_RETURN_ON_ERROR(fw_update_record_load(&s_record), TAG, "install record unreadable");
    if (!s_record.present) {
        return ESP_OK;
    }

    if (!fw_update_record_is_running()) {
        ESP_LOGW(TAG, "Update to %s did not come up, back on %s", s_record.version, desc->version);
        fw_update_raise_notice(FW_UPDATE_NOTICE_ROLLED_BACK, s_record.version);
        return fw_update_record_clear();
    }

    s_booted_from_update = true;
    if (!s_pending_verify) {
        /* Rollback disabled when it was installed, or already confirmed. */
        return fw_update_record_clear();
    }

    /* Installed and booting for the first time: the record stays until
     * fw_update_confirm_running(), so a rollback before it is still reported. */
    return ESP_OK;
}

bool fw_update_is_pending_verify(void)
{
    return s_pending_verify;
}

esp_err_t fw_update_confirm_running(void)
{
    if (!s_pending_verify) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_ota_mark_app_valid_cancel_rollback(), TAG, "mark valid failed");
    s_pending_verify = false;
    ESP_LOGI(TAG, "Running image confirmed, rollback cancelled");

    if (fw_update_record_is_running()) {
        ESP_RETURN_ON_ERROR(fw_update_record_clear(), TAG, "install record not cleared");
    }
    return ESP_OK;
}

esp_err_t fw_update_reject_running(void)
{
    ESP_LOGE(TAG, "Rejecting the running image, rolling back");
    return esp_ota_mark_app_invalid_rollback_and_reboot();
}

static bool fw_update_has_bin_ext(const char *name)
{
    const size_t len = strlen(name);
    const size_t ext_len = strlen(FW_UPDATE_BIN_EXT);
    return len > ext_len && strcasecmp(name + len - ext_len, FW_UPDATE_BIN_EXT) == 0;
}

static bool fw_update_read_at(FILE *file, long offset, void *out, size_t len)
{
    return fseek(file, offset, SEEK_SET) == 0 && fread(out, 1, len, file) == len;
}

/**
 * Walk the segment table the way esp_image_format.c does, to get the image
 * length without reading the payload: segments, checksum byte padded to 16,
 * then the SHA-256 when appended. The digest itself is checked by
 * esp_ota_end() -- this only catches a truncated or malformed file before
 * any flash is erased for it.
 */
static bool fw_update_walk_image(FILE *file, const esp_image_header_t *header, size_t file_size,
                                 size_t *image_size)
{
    if (header->segment_count == 0 || header->segment_count > ESP_IMAGE_MAX_SEGMENTS) {
        return false;
    }

    size_t len = sizeof(esp_image_header_t);
    for (uint8_t index = 0; index < header->segment_count; ++index) {
        esp_image_segment_header_t segment;
        if (!fw_update_read_at(file, (long)len, &segment, sizeof(segment))) {
            return false;
        }
        len += sizeof(segment);
        if (segment.data_len > file_size - len) {
            return false;
        }
        len += segment.data_len;
    }

    len = (len + 1U + FW_UPDATE_IMAGE_ALIGN - 1U) & ~(size_t)(FW_UPDATE_IMAGE_ALIGN - 1U);
    if (header->hash_appended == 1U) {
        len += FW_UPDATE_SHA_LEN;
    }
    if (len > file_size) {
        return false;
    }

    *image_size = len;
    return true;
}

static fw_image_kind_t fw_update_inspect(const char *path, size_t slot_size, char *version,
                                         size_t *image_size)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "%s: cannot open (errno %d), skipped", path, errno);
        return FW_IMAGE_FOREIGN;
    }

    struct stat info;
    esp_image_header_t header;
    esp_app_desc_t desc;
    fw_image_kind_t kind = FW_IMAGE_FOREIGN;

    /* The app descriptor opens the first segment, right after its header. */
    const long desc_offset = (long)(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));

    if (fstat(fileno(file), &info) != 0
        || !fw_update_read_at(file, 0, &header, sizeof(header))
        || header.magic != ESP_IMAGE_HEADER_MAGIC
        || header.chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID
        || !fw_update_read_at(file, desc_offset, &desc, sizeof(desc))
        || desc.magic_word != ESP_APP_DESC_MAGIC_WORD
        || strncmp(desc.project_name, esp_app_get_description()->project_name,
                   sizeof(desc.project_name)) != 0) {
        fclose(file);
        return FW_IMAGE_FOREIGN;
    }

    strlcpy(version, desc.version, FW_UPDATE_VERSION_MAX);

    if (memcmp(desc.app_elf_sha256, esp_app_get_description()->app_elf_sha256,
               FW_UPDATE_SHA_LEN) == 0) {
        kind = FW_IMAGE_RUNNING;
    } else if (!fw_update_walk_image(file, &header, (size_t)info.st_size, image_size)) {
        ESP_LOGW(TAG, "%s: truncated or malformed image", path);
        kind = FW_IMAGE_DAMAGED;
    } else if (*image_size > slot_size) {
        ESP_LOGW(TAG, "%s: image of %u bytes exceeds the %u-byte slot", path,
                 (unsigned)*image_size, (unsigned)slot_size);
        kind = FW_IMAGE_DAMAGED;
    } else {
        kind = FW_IMAGE_UPDATE;
    }

    fclose(file);
    return kind;
}

/**
 * Rename a damaged update file out of the way, so it is not retried at every
 * boot, but kept for the operator to see. A leftover *.bad of the same name
 * is replaced: FAT rename() fails when the target exists.
 */
static void fw_update_quarantine(const char *path)
{
    char bad_path[FW_UPDATE_BAD_PATH_MAX];
    snprintf(bad_path, sizeof(bad_path), "%s" FW_UPDATE_BAD_SUFFIX, path);

    if (unlink(bad_path) != 0 && errno != ENOENT) {
        ESP_LOGW(TAG, "%s: cannot remove (errno %d)", bad_path, errno);
    }
    if (rename(path, bad_path) != 0) {
        ESP_LOGE(TAG, "%s: cannot rename to %s (errno %d), will be retried", path,
                 FW_UPDATE_BAD_SUFFIX, errno);
    }
}

static size_t fw_update_list_bin_files(DIR *dir, const char *dir_path)
{
    size_t count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        /* A leading dot covers the "._name.bin" AppleDouble files macOS
         * writes next to every file copied to a FAT volume. */
        if (entry->d_type != DT_REG || entry->d_name[0] == '.'
            || !fw_update_has_bin_ext(entry->d_name)) {
            continue;
        }

        if (count == FW_UPDATE_MAX_FILES) {
            ESP_LOGW(TAG, "More than %d .bin files, the rest is ignored", FW_UPDATE_MAX_FILES);
            break;
        }

        const int written = snprintf(s_scan_paths[count], FW_UPDATE_PATH_MAX, "%s/%s", dir_path,
                                     entry->d_name);
        if (written < 0 || written >= FW_UPDATE_PATH_MAX) {
            ESP_LOGW(TAG, "%s: name too long, skipped", entry->d_name);
            continue;
        }
        ++count;
    }
    return count;
}

esp_err_t fw_update_scan(const char *dir_path, fw_update_candidate_t *out)
{
    ESP_RETURN_ON_FALSE(dir_path != NULL && out != NULL, ESP_ERR_INVALID_ARG, TAG, "null argument");

    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
    ESP_RETURN_ON_FALSE(slot != NULL, ESP_ERR_NOT_FOUND, TAG, "no OTA slot to update");

    DIR *dir = opendir(dir_path);
    ESP_RETURN_ON_FALSE(dir != NULL, ESP_FAIL, TAG, "cannot open %s (errno %d)", dir_path, errno);
    /* Files are renamed or deleted below: list first, act after closedir(). */
    const size_t file_count = fw_update_list_bin_files(dir, dir_path);
    closedir(dir);

    size_t update_count = 0;
    for (size_t index = 0; index < file_count; ++index) {
        const char *path = s_scan_paths[index];
        char version[FW_UPDATE_VERSION_MAX] = "";
        size_t image_size = 0;

        switch (fw_update_inspect(path, slot->size, version, &image_size)) {
        case FW_IMAGE_RUNNING:
            ESP_LOGI(TAG, "%s: %s is already running, file removed", path, version);
            if (unlink(path) != 0) {
                ESP_LOGW(TAG, "%s: cannot remove (errno %d)", path, errno);
            }
            /* Right after an update this is the leftover of that install
             * (power lost before its delete): nothing to tell the operator. */
            if (!s_booted_from_update) {
                fw_update_raise_notice(FW_UPDATE_NOTICE_ALREADY_INSTALLED, version);
            }
            break;
        case FW_IMAGE_DAMAGED:
            fw_update_quarantine(path);
            fw_update_raise_notice(FW_UPDATE_NOTICE_INVALID_FILE, version);
            break;
        case FW_IMAGE_UPDATE:
            if (update_count == 0) {
                strlcpy(out->path, path, sizeof(out->path));
                strlcpy(out->version, version, sizeof(out->version));
                out->image_size = image_size;
            }
            ++update_count;
            break;
        case FW_IMAGE_FOREIGN:
        default:
            ESP_LOGI(TAG, "%s: not a %s image, ignored", path,
                     esp_app_get_description()->project_name);
            break;
        }
    }

    if (update_count > 1) {
        ESP_LOGW(TAG, "%u update files found, none installed", (unsigned)update_count);
        fw_update_raise_notice(FW_UPDATE_NOTICE_MULTIPLE_FILES, NULL);
        return ESP_ERR_INVALID_STATE;
    }
    if (update_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Update found: %s, %s, %u bytes", out->path, out->version,
             (unsigned)out->image_size);
    return ESP_OK;
}

/** Copy the image into the slot. The handle is always closed on return. */
static esp_err_t fw_update_write_image(const fw_update_candidate_t *candidate,
                                       const esp_partition_t *slot)
{
    FILE *file = fopen(candidate->path, "rb");
    ESP_RETURN_ON_FALSE(file != NULL, ESP_FAIL, TAG, "cannot open %s (errno %d)", candidate->path,
                        errno);

    /* Sequential writes erase sector by sector as the data arrives: erasing
     * the whole 1.5 MB slot up front blocks for seconds in one call. */
    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        fclose(file);
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t written = 0;
    while (written < candidate->image_size) {
        size_t chunk = candidate->image_size - written;
        if (chunk > sizeof(s_chunk)) {
            chunk = sizeof(s_chunk);
        }

        if (fread(s_chunk, 1, chunk, file) != chunk) {
            ESP_LOGE(TAG, "read failed at %u (errno %d)", (unsigned)written, errno);
            err = ESP_FAIL;
            break;
        }
        err = esp_ota_write(handle, s_chunk, chunk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed at %u: %s", (unsigned)written,
                     esp_err_to_name(err));
            break;
        }

        written += chunk;
        fw_update_set_phase(FW_UPDATE_PHASE_WRITING,
                            (uint8_t)((written * 100U) / candidate->image_size), NULL);
        /* This task outranks the idle tasks: without a yield per chunk they
         * starve for the whole copy and the task watchdog fires. */
        vTaskDelay(1);
    }
    fclose(file);

    if (err != ESP_OK) {
        (void)esp_ota_abort(handle);
        return err;
    }

    /* Verifies the whole image, appended SHA-256 included, and releases the
     * handle whatever the outcome. */
    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image verification failed: %s", esp_err_to_name(err));
    }
    return err;
}

/** Point the bootloader at @p slot, keeping the install record in step with it. */
static esp_err_t fw_update_activate(const esp_partition_t *slot)
{
    esp_app_desc_t desc;
    ESP_RETURN_ON_ERROR(esp_ota_get_partition_description(slot, &desc), TAG,
                        "new image has no descriptor");

    /* Written before the switch: a record without a switch reads as "did not
     * come up" after the next reset, which is still the truth -- the other
     * order could leave a new image running with nobody reporting it. */
    const esp_err_t record_err = fw_update_record_store(&desc);
    if (record_err != ESP_OK) {
        ESP_LOGW(TAG, "install record not saved (%s): outcome will not be reported",
                 esp_err_to_name(record_err));
    }

    const esp_err_t err = esp_ota_set_boot_partition(slot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        if (record_err == ESP_OK && fw_update_record_clear() != ESP_OK) {
            ESP_LOGW(TAG, "install record not cleared");
        }
    }
    return err;
}

esp_err_t fw_update_install(const fw_update_candidate_t *candidate)
{
    ESP_RETURN_ON_FALSE(candidate != NULL && candidate->image_size > 0, ESP_ERR_INVALID_ARG, TAG,
                        "invalid candidate");

    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
    ESP_RETURN_ON_FALSE(slot != NULL, ESP_ERR_NOT_FOUND, TAG, "no OTA slot to update");
    ESP_RETURN_ON_FALSE(candidate->image_size <= slot->size, ESP_ERR_INVALID_SIZE, TAG,
                        "image larger than slot %s", slot->label);

    ESP_LOGI(TAG, "Installing %s into %s", candidate->version, slot->label);
    fw_update_set_phase(FW_UPDATE_PHASE_WRITING, 0, candidate->version);

    esp_err_t err = fw_update_write_image(candidate, slot);
    if (err == ESP_OK) {
        err = fw_update_activate(slot);
    }

    if (err != ESP_OK) {
        fw_update_set_phase(FW_UPDATE_PHASE_IDLE, 0, NULL);
        /* A verification failure is the file's fault (copy cut short, flushed
         * late by the computer): set it aside. Anything else is a flash or
         * file I/O error, so the file stays and is retried next time. */
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            fw_update_quarantine(candidate->path);
            fw_update_raise_notice(FW_UPDATE_NOTICE_INVALID_FILE, candidate->version);
        } else {
            fw_update_raise_notice(FW_UPDATE_NOTICE_WRITE_FAILED, candidate->version);
        }
        return err;
    }

    /* A failed delete is harmless: after the reset the file holds the
     * running image, and the next scan removes it. */
    if (unlink(candidate->path) != 0) {
        ESP_LOGW(TAG, "%s: cannot remove (errno %d)", candidate->path, errno);
    }

    fw_update_set_phase(FW_UPDATE_PHASE_RESTARTING, 100, NULL);
    ESP_LOGI(TAG, "Installed %s, restart required (stack high water %u bytes)",
             candidate->version, (unsigned)uxTaskGetStackHighWaterMark(NULL));
    return ESP_OK;
}

void fw_update_get_status(fw_update_status_t *out)
{
    if (out == NULL) {
        return;
    }

    taskENTER_CRITICAL(&s_status_lock);
    *out = s_status;
    taskEXIT_CRITICAL(&s_status_lock);
}

void fw_update_ack_notice(void)
{
    taskENTER_CRITICAL(&s_status_lock);
    s_status.notice = FW_UPDATE_NOTICE_NONE;
    if (s_status.phase == FW_UPDATE_PHASE_IDLE) {
        s_status.version[0] = '\0';
    }
    taskEXIT_CRITICAL(&s_status_lock);
}
