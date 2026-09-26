#include "gui_service.h"

#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"
#include "lvgl.h"

#include "digital_inputs.h"
#include "display_driver.h"
#include "page_manager.h"
#include "pages.h"
#include "settings_service.h"

#define GUI_BUTTON_POLL_PERIOD_MS 30
#define GUI_BUTTON_DEBOUNCE_SAMPLES 2
#define GUI_TICK_PERIOD_MS 500
#define GUI_BUTTON_MASK ((1UL << PAGE_MANAGER_BUTTON_COUNT) - 1UL)

/*
 * digital_inputs_init() configures the button GPIOs with no internal
 * pull-up/pull-down (GPIO_PULLUP_DISABLE/GPIO_PULLDOWN_DISABLE). Whether the
 * buttons are active-low with an external pull-up, or active-high, has not
 * been confirmed against the schematic. Assuming active-low until validated
 * on hardware -- see the plan's open item on button wiring.
 */
#define GUI_BUTTON_ACTIVE_LOW true

static const char *TAG = "gui_service";

static lv_timer_t *s_button_timer;
static lv_timer_t *s_tick_timer;
static uint32_t s_button_stable_state;
static uint32_t s_button_candidate_state;
static uint8_t s_button_candidate_count;
static bool s_initialized;
static bool s_started;

static uint32_t gui_normalize_buttons(uint32_t raw)
{
    return GUI_BUTTON_ACTIVE_LOW ? (~raw) : raw;
}

static void gui_button_poll_cb(lv_timer_t *timer)
{
    (void)timer;

    uint32_t raw = 0;
    uint32_t valid_mask = 0;
    if (digital_inputs_read(&raw, &valid_mask) != ESP_OK) {
        return;
    }

    uint32_t pressed_mask = gui_normalize_buttons(raw) & valid_mask & GUI_BUTTON_MASK;

    if (pressed_mask == s_button_candidate_state) {
        if (s_button_candidate_count < UINT8_MAX) {
            s_button_candidate_count++;
        }
    } else {
        s_button_candidate_state = pressed_mask;
        s_button_candidate_count = 1;
    }

    if (s_button_candidate_count < GUI_BUTTON_DEBOUNCE_SAMPLES) {
        return;
    }

    uint32_t newly_pressed = pressed_mask & ~s_button_stable_state;
    s_button_stable_state = pressed_mask;

    for (uint8_t index = 0; index < PAGE_MANAGER_BUTTON_COUNT; ++index) {
        if ((newly_pressed & (1UL << index)) != 0U) {
            page_manager_dispatch_button(index);
        }
    }
}

static void gui_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    page_fw_update_poll();
    page_manager_tick();
}

esp_err_t gui_service_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(display_driver_init(), TAG, "display init failed");
    /* display_driver_init() brings the backlight up at full brightness; apply
     * the operator's stored level once the panel is alive so the setting
     * survives a power cycle instead of only lasting the session. */
    ESP_RETURN_ON_ERROR(display_driver_set_backlight(settings_service_get_brightness_percent()),
                        TAG, "backlight init failed");

    ESP_RETURN_ON_FALSE(display_driver_lock(0), ESP_FAIL, TAG, "failed to lock lvgl");
    page_manager_init(lv_screen_active());
    page_manager_switch_to(&page_splash);
    display_driver_unlock();

    s_initialized = true;
    ESP_LOGI(TAG, "GUI ready");
    return ESP_OK;
}

esp_err_t gui_service_start(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "gui not initialized");

    if (s_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(display_driver_lock(0), ESP_FAIL, TAG, "failed to lock lvgl");
    s_button_timer = lv_timer_create(gui_button_poll_cb, GUI_BUTTON_POLL_PERIOD_MS, NULL);
    s_tick_timer = lv_timer_create(gui_tick_cb, GUI_TICK_PERIOD_MS, NULL);
    display_driver_unlock();

    ESP_RETURN_ON_FALSE(s_button_timer != NULL && s_tick_timer != NULL, ESP_ERR_NO_MEM, TAG, "timer creation failed");

    s_started = true;
    ESP_LOGI(TAG, "GUI started");
    return ESP_OK;
}

esp_err_t gui_service_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(display_driver_lock(0), ESP_FAIL, TAG, "failed to lock lvgl");
    lv_timer_del(s_button_timer);
    lv_timer_del(s_tick_timer);
    s_button_timer = NULL;
    s_tick_timer = NULL;
    display_driver_unlock();

    s_started = false;
    return ESP_OK;
}
