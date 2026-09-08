#include "kdl_splash.h"

#include <stdint.h>

#include "display_driver.h"

/* Symbols produced by EMBED_FILES for splash_480x320_rgb565.bin. */
extern const uint8_t kdl_splash_data_start[] asm("_binary_splash_480x320_rgb565_bin_start");
extern const uint8_t kdl_splash_data_end[] asm("_binary_splash_480x320_rgb565_bin_end");

#define KDL_SPLASH_STRIDE (KDL_SPLASH_WIDTH * 2)
#define KDL_SPLASH_SIZE   (KDL_SPLASH_STRIDE * KDL_SPLASH_HEIGHT)

_Static_assert(KDL_SPLASH_WIDTH == DISPLAY_PANEL_HOR_RES
                   && KDL_SPLASH_HEIGHT == DISPLAY_PANEL_VER_RES,
               "splash artwork must match the panel exactly: it is drawn 1:1, "
               "and LVGL would have to scale it otherwise");

/*
 * Pixel order is LVGL's native little-endian RGB565, NOT byte-swapped. The
 * panel needs the opposite order, but esp_lvgl_port is configured with
 * swap_bytes = true and inverts the whole draw buffer at flush time, after
 * the blit -- so pre-swapping the asset would come out inverted.
 */
const lv_image_dsc_t kdl_splash_image = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = KDL_SPLASH_WIDTH,
        .h = KDL_SPLASH_HEIGHT,
        .stride = KDL_SPLASH_STRIDE,
    },
    .data = kdl_splash_data_start,
    .data_size = KDL_SPLASH_SIZE,
};
