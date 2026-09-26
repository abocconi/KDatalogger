/*
 * Workaround for a wear levelling bug in ESP-IDF 6.1, applied at link time
 * (-Wl,--wrap=wl_read,--wrap=wl_write, see CMakeLists.txt) so every caller is
 * covered -- esp_tinyusb's MSC storage and FATFS alike -- without patching IDF.
 *
 * WL_Flash::read()/write() split a request into wl_page_size (4 KB) pieces
 * counted from the request's own start address, and map only each piece's
 * first byte to a physical address. A request that is not 4 KB aligned has
 * pieces straddling two logical pages, read or written as if the two were
 * physically contiguous; next to the WL dummy sector they are not, and the
 * second part lands on the wrong physical sector: reads return stale data
 * from 4 KB earlier, writes are silently lost. Reproduced on the linux host
 * target with MSC-like 8 KB transfers.
 *
 * Splitting at logical 4 KB boundaries keeps every piece inside one page, which
 * WL maps correctly. wl_erase_range() is not affected and is not wrapped.
 * Remove this file and the link options once IDF fixes WL_Flash.
 */

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "wear_levelling.h"

/* WL maps one page (the partition erase size, 4096 on SPI flash) at a time.
 * Any multiple of it works too: splitting at 4 KB is safe for every such page. */
#define WL_SPLIT_BOUNDARY 4096U

esp_err_t __real_wl_read(wl_handle_t handle, size_t src_addr, void *dest, size_t size);
esp_err_t __real_wl_write(wl_handle_t handle, size_t dest_addr, const void *src, size_t size);
esp_err_t __wrap_wl_read(wl_handle_t handle, size_t src_addr, void *dest, size_t size);
esp_err_t __wrap_wl_write(wl_handle_t handle, size_t dest_addr, const void *src, size_t size);

/** Bytes from addr up to the next 4 KB boundary, capped at remaining. */
static size_t wl_split_piece(size_t addr, size_t remaining)
{
    const size_t to_boundary = WL_SPLIT_BOUNDARY - (addr % WL_SPLIT_BOUNDARY);
    return (remaining < to_boundary) ? remaining : to_boundary;
}

esp_err_t __wrap_wl_read(wl_handle_t handle, size_t src_addr, void *dest, size_t size)
{
    /* Single piece (or size 0): let WL validate the arguments as usual. */
    if (wl_split_piece(src_addr, size) == size || dest == NULL) {
        return __real_wl_read(handle, src_addr, dest, size);
    }

    uint8_t *out = dest;
    while (size > 0) {
        const size_t piece = wl_split_piece(src_addr, size);
        const esp_err_t err = __real_wl_read(handle, src_addr, out, piece);
        if (err != ESP_OK) {
            return err;
        }
        src_addr += piece;
        out += piece;
        size -= piece;
    }
    return ESP_OK;
}

esp_err_t __wrap_wl_write(wl_handle_t handle, size_t dest_addr, const void *src, size_t size)
{
    if (wl_split_piece(dest_addr, size) == size || src == NULL) {
        return __real_wl_write(handle, dest_addr, src, size);
    }

    const uint8_t *in = src;
    while (size > 0) {
        const size_t piece = wl_split_piece(dest_addr, size);
        const esp_err_t err = __real_wl_write(handle, dest_addr, in, piece);
        if (err != ESP_OK) {
            return err;
        }
        dest_addr += piece;
        in += piece;
        size -= piece;
    }
    return ESP_OK;
}
