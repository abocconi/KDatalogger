#pragma once

#include <stdbool.h>

#include "page_manager.h"

extern const gui_page_t page_splash;
extern const gui_page_t page_main;
extern const gui_page_t page_graph;
extern const gui_page_t page_settings;
extern const gui_page_t page_datetime;
extern const gui_page_t page_usb;

/**
 * @brief Arm the date/time mask before switching to it.
 *
 * @param return_page Page to go back to once the mask is dismissed.
 * @param boot_mode   true for the power-up prompt: the cancel key becomes
 *                    "Skip" and the mask dismisses itself after a timeout, so
 *                    an unattended restart never leaves the logger waiting at
 *                    a dialog.
 */
void page_datetime_configure(const gui_page_t *return_page, bool boot_mode);

/**
 * @brief Snapshot the volume statistics, hand the storage to the USB host and
 *        show the USB page.
 *
 * The statistics have to be read before the handover: once the host owns the
 * volume the filesystem is unmounted and there is nothing left to query.
 */
void page_usb_enter(void);
