#include "display_driver.h"

#include <inttypes.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7796.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"

#include "board_config.h"

#define DISPLAY_SPI_PCLK_HZ (40 * 1000 * 1000)
#define DISPLAY_LVGL_BUFFER_LINES (DISPLAY_PANEL_VER_RES / 10)

#define DISPLAY_BACKLIGHT_LEDC_TIMER LEDC_TIMER_0
#define DISPLAY_BACKLIGHT_LEDC_MODE LEDC_LOW_SPEED_MODE
#define DISPLAY_BACKLIGHT_LEDC_CHANNEL LEDC_CHANNEL_0
#define DISPLAY_BACKLIGHT_LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define DISPLAY_BACKLIGHT_LEDC_FREQ_HZ 5000
#define DISPLAY_BACKLIGHT_MAX_DUTY ((1U << 10) - 1U)

/*
 * Confirmed against a sibling project (ISP-HMI) driving the same ST7796
 * panel on the same board: swap_xy + mirror_x + mirror_y all true gives
 * correct landscape orientation, applied via lvgl_port_display_cfg_t.rotation
 * (see display_init_lvgl) rather than direct esp_lcd_panel_* calls.
 */
#define DISPLAY_ROTATE_SWAP_XY true
#define DISPLAY_ROTATE_MIRROR_X true
#define DISPLAY_ROTATE_MIRROR_Y true
#define DISPLAY_INVERT_COLOR true
#define DISPLAY_LVGL_DIAG_ENABLED 1

static const char *TAG = "display_driver";

static esp_lcd_panel_io_handle_t s_io_handle;
static esp_lcd_panel_handle_t s_panel_handle;
static lv_display_t *s_lvgl_display;
static bool s_spi_bus_initialized;
static bool s_initialized;

#if DISPLAY_LVGL_DIAG_ENABLED
typedef struct {
    uint32_t invalidate_count;
    uint32_t refresh_count;
    uint32_t flush_count;
    uint32_t flush_pixels;
    uint32_t max_flush_pixels;
    lv_area_t last_flush_area;
    int64_t last_log_us;
} display_lvgl_diag_t;

static display_lvgl_diag_t s_lvgl_diag;

static uint32_t display_area_pixels(const lv_area_t *area)
{
    if (area == NULL || area->x2 < area->x1 || area->y2 < area->y1) {
        return 0;
    }

    return (uint32_t)(area->x2 - area->x1 + 1) * (uint32_t)(area->y2 - area->y1 + 1);
}

static void display_lvgl_diag_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_area_t *area = (lv_area_t *)lv_event_get_param(event);

    switch (code) {
    case LV_EVENT_INVALIDATE_AREA:
        s_lvgl_diag.invalidate_count++;
        break;
    case LV_EVENT_REFR_START:
        s_lvgl_diag.refresh_count++;
        break;
    case LV_EVENT_FLUSH_START: {
        uint32_t pixels = display_area_pixels(area);
        s_lvgl_diag.flush_count++;
        s_lvgl_diag.flush_pixels += pixels;
        if (pixels > s_lvgl_diag.max_flush_pixels) {
            s_lvgl_diag.max_flush_pixels = pixels;
        }
        if (area != NULL) {
            s_lvgl_diag.last_flush_area = *area;
        }
        break;
    }
    default:
        break;
    }

    int64_t now_us = esp_timer_get_time();
    if (s_lvgl_diag.last_log_us == 0) {
        s_lvgl_diag.last_log_us = now_us;
        return;
    }

    if (now_us - s_lvgl_diag.last_log_us >= 1000000) {
        ESP_LOGI(TAG,
                 "lvgl diag: refr=%" PRIu32 " inv=%" PRIu32 " flush=%" PRIu32
                 " px=%" PRIu32 " max_px=%" PRIu32 " last=(%" PRId32 ",%" PRId32 ")-(%"
                 PRId32 ",%" PRId32 ")",
                 s_lvgl_diag.refresh_count,
                 s_lvgl_diag.invalidate_count,
                 s_lvgl_diag.flush_count,
                 s_lvgl_diag.flush_pixels,
                 s_lvgl_diag.max_flush_pixels,
                 s_lvgl_diag.last_flush_area.x1,
                 s_lvgl_diag.last_flush_area.y1,
                 s_lvgl_diag.last_flush_area.x2,
                 s_lvgl_diag.last_flush_area.y2);

        s_lvgl_diag.invalidate_count = 0;
        s_lvgl_diag.refresh_count = 0;
        s_lvgl_diag.flush_count = 0;
        s_lvgl_diag.flush_pixels = 0;
        s_lvgl_diag.max_flush_pixels = 0;
        s_lvgl_diag.last_log_us = now_us;
    }
}
#endif

static esp_err_t display_init_backlight(void)
{
    ledc_timer_config_t timer_config = {
        .speed_mode = DISPLAY_BACKLIGHT_LEDC_MODE,
        .timer_num = DISPLAY_BACKLIGHT_LEDC_TIMER,
        .duty_resolution = DISPLAY_BACKLIGHT_LEDC_DUTY_RES,
        .freq_hz = DISPLAY_BACKLIGHT_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "ledc_timer_config failed");

    ledc_channel_config_t channel_config = {
        .gpio_num = board_config_display_bl_io(),
        .speed_mode = DISPLAY_BACKLIGHT_LEDC_MODE,
        .channel = DISPLAY_BACKLIGHT_LEDC_CHANNEL,
        .timer_sel = DISPLAY_BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "ledc_channel_config failed");

    return ESP_OK;
}

static esp_err_t display_init_panel(void)
{
    spi_bus_config_t bus_config = {
        .mosi_io_num = board_config_display_mosi_io(),
        .miso_io_num = -1,
        .sclk_io_num = board_config_display_sclk_io(),
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_PANEL_HOR_RES * DISPLAY_PANEL_VER_RES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(board_config_display_host(), &bus_config, SPI_DMA_CH_AUTO),
                        TAG, "spi_bus_initialize failed");
    s_spi_bus_initialized = true;

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = board_config_display_cs_io(),
        .dc_gpio_num = board_config_display_dc_io(),
        .spi_mode = 0,
        .pclk_hz = DISPLAY_SPI_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)board_config_display_host(),
                                                 &io_config, &s_io_handle),
                        TAG, "esp_lcd_new_panel_io_spi failed");

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = board_config_display_rst_io(),
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7796(s_io_handle, &panel_config, &s_panel_handle),
                        TAG, "esp_lcd_new_panel_st7796 failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel_handle), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel_handle), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel_handle, DISPLAY_INVERT_COLOR), TAG, "panel invert failed");
    /* swap_xy/mirror are NOT set here: lvgl_port_add_disp() re-applies them from
     * lvgl_port_display_cfg_t.rotation on every init, so setting them here would
     * just get silently overwritten — .rotation below is the single source of truth. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel_handle, true), TAG, "panel disp_on_off failed");

    return ESP_OK;
}

static esp_err_t display_init_lvgl(void)
{
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 8192,
        .task_affinity = 0,
        .task_max_sleep_ms = 500,
        .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl_port_init failed");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_io_handle,
        .panel_handle = s_panel_handle,
        .buffer_size = DISPLAY_PANEL_HOR_RES * DISPLAY_LVGL_BUFFER_LINES,
        .double_buffer = true,
        .hres = DISPLAY_PANEL_HOR_RES,
        .vres = DISPLAY_PANEL_VER_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = DISPLAY_ROTATE_SWAP_XY,
            .mirror_x = DISPLAY_ROTATE_MIRROR_X,
            .mirror_y = DISPLAY_ROTATE_MIRROR_Y,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .swap_bytes = true,
        },
    };

    s_lvgl_display = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(s_lvgl_display != NULL, ESP_FAIL, TAG, "lvgl_port_add_disp failed");

#if DISPLAY_LVGL_DIAG_ENABLED
    lv_display_add_event_cb(s_lvgl_display, display_lvgl_diag_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_add_event_cb(s_lvgl_display, display_lvgl_diag_event_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(s_lvgl_display, display_lvgl_diag_event_cb, LV_EVENT_FLUSH_START, NULL);
#endif

    return ESP_OK;
}

esp_err_t display_driver_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(board_config_display_has_valid_pins(), ESP_ERR_INVALID_STATE, TAG, "display pins not configured");

    ESP_RETURN_ON_ERROR(display_init_panel(), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(display_init_lvgl(), TAG, "lvgl init failed");
    ESP_RETURN_ON_ERROR(display_init_backlight(), TAG, "backlight init failed");
    ESP_RETURN_ON_ERROR(display_driver_set_backlight(100), TAG, "backlight enable failed");

    s_initialized = true;
    ESP_LOGI(TAG, "Display ready (%dx%d landscape)", DISPLAY_PANEL_HOR_RES, DISPLAY_PANEL_VER_RES);
    return ESP_OK;
}

esp_err_t display_driver_set_backlight(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }

    uint32_t duty = (DISPLAY_BACKLIGHT_MAX_DUTY * percent) / 100U;
    ESP_RETURN_ON_ERROR(ledc_set_duty(DISPLAY_BACKLIGHT_LEDC_MODE, DISPLAY_BACKLIGHT_LEDC_CHANNEL, duty),
                        TAG, "ledc_set_duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(DISPLAY_BACKLIGHT_LEDC_MODE, DISPLAY_BACKLIGHT_LEDC_CHANNEL),
                        TAG, "ledc_update_duty failed");
    return ESP_OK;
}

bool display_driver_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void display_driver_unlock(void)
{
    lvgl_port_unlock();
}

lv_display_t *display_driver_get_lvgl_display(void)
{
    return s_lvgl_display;
}
