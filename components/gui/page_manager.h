#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define PAGE_MANAGER_BUTTON_COUNT 5

/**
 * @brief A GUI page: owns the content area, names the five sidebar slots and
 *        reacts to key presses while active.
 *
 * The page never draws the chrome (status bar, clock, key rail); it only
 * supplies the text that goes into it. Chrome refresh is owned by
 * page_manager so the status bar survives page switches untouched, which
 * keeps the invalidated area on a switch limited to the content box.
 */
typedef struct {
    const char *name;
    const char *title; /**< Status bar caption; page_manager_set_title() overrides at runtime */
    /** Take over the whole panel: the status bar and the key rail are hidden
     *  and the content box grows to the full 480x320. For pages that are not
     *  part of the normal navigation -- the boot splash today, a fatal-error
     *  screen later -- where the chrome would only be in the way. */
    bool fullscreen;
    const char *button_labels[PAGE_MANAGER_BUTTON_COUNT];
    void (*on_show)(lv_obj_t *content);
    void (*on_hide)(void);
    void (*on_tick)(void);
    void (*on_button)(uint8_t button_index);
} gui_page_t;

/** @brief Build the status bar, content box and key rail. Call once. */
void page_manager_init(lv_obj_t *screen);

/** @brief Hide the current page (if any), clear content, show the new page. */
void page_manager_switch_to(const gui_page_t *page);

/** @brief Page currently shown, NULL before the first switch. */
const gui_page_t *page_manager_get_current(void);

/** @brief Refresh the chrome, then forward a periodic tick to the current page. */
void page_manager_tick(void);

/** @brief Forward a key press (0 = topmost, 4 = bottom) to the current page. */
void page_manager_dispatch_button(uint8_t button_index);

/**
 * @brief Override the status bar caption until the next page switch.
 *
 * For captions that track page state rather than page identity, e.g.
 * "GRAPH - HOLD" versus "GRAPH - LIVE". Passing NULL restores the page's
 * static title.
 */
void page_manager_set_title(const char *title);

/**
 * @brief Override one key label until the next page switch.
 *
 * For labels that toggle with page state, e.g. Hold/Run. Passing NULL
 * restores the page's static label for that slot.
 */
void page_manager_set_button_label(uint8_t button_index, const char *label);
