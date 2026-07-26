#pragma once

#include <stdint.h>

#include "lvgl.h"

#define PAGE_MANAGER_BUTTON_COUNT 5

/**
 * @brief A GUI page: owns the content area, exposes labels for the fixed
 *        5-button sidebar, and reacts to button presses while active.
 */
typedef struct {
    const char *name;
    const char *button_labels[PAGE_MANAGER_BUTTON_COUNT];
    void (*on_show)(lv_obj_t *content);
    void (*on_hide)(void);
    void (*on_tick)(void);
    void (*on_button)(uint8_t button_index);
} gui_page_t;

/** @brief Build the sidebar + content layout on the given screen. Call once. */
void page_manager_init(lv_obj_t *screen);

/** @brief Hide the current page (if any), clear content, show the new page. */
void page_manager_switch_to(const gui_page_t *page);

/** @brief Forward a periodic refresh tick to the current page. */
void page_manager_tick(void);

/** @brief Forward a button press (0-4) to the current page. */
void page_manager_dispatch_button(uint8_t button_index);
