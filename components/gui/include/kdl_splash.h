#pragma once

#include "lvgl.h"

/**
 * @file kdl_splash.h
 * @brief Boot splash artwork.
 *
 * The bitmap is a raw RGB565 blob linked into the application image by
 * EMBED_FILES (see the component CMakeLists) rather than a generated C array:
 * 307 200 bytes of source text would be miserable to keep in the repository
 * and slow to compile, and nothing is gained by it.
 *
 * Because the blob is memory-mapped flash in LVGL's native pixel format, it
 * costs no RAM at all: the renderer blits it straight into the partial draw
 * buffer a strip at a time, with no decode step and no full-frame buffer.
 * That is the whole reason not to store a PNG and decode it at boot.
 *
 * Regenerate after editing the artwork:
 *   python3 tools/png_to_rgb565.py components/gui/assets/splash_source.png \
 *           components/gui/assets/splash_480x320_rgb565.bin --width 480 --height 320
 */

#define KDL_SPLASH_WIDTH  480
#define KDL_SPLASH_HEIGHT 320

extern const lv_image_dsc_t kdl_splash_image;
