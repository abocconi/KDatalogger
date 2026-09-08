#!/usr/bin/env python3
"""Convert a PNG into the raw RGB565 blob an LVGL lv_image_dsc_t points at.

Pure standard library on purpose: this runs in CI and on a fresh checkout with
no pip install, and a build asset that needs an environment set up first is an
asset that eventually stops being regenerated.

Byte order is LVGL's native little-endian RGB565 -- deliberately NOT swapped.
esp_lvgl_port is configured with swap_bytes = true, which inverts the whole
draw buffer at flush time, so an image that arrives pre-swapped comes out with
its colours inverted. See display_driver.c.

Usage:
    python3 tools/png_to_rgb565.py in.png out.bin --width 480 --height 320
"""

import argparse
import struct
import sys
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path):
    """Return (width, height, channels, pixel bytes) for an 8-bit RGB/RGBA PNG."""
    with open(path, "rb") as handle:
        data = handle.read()

    if data[:8] != PNG_SIGNATURE:
        raise SystemExit("%s is not a PNG" % path)

    width = height = depth = color_type = interlace = None
    idat = bytearray()
    offset = 8
    while offset < len(data):
        (length,) = struct.unpack(">I", data[offset:offset + 4])
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        offset += 12 + length  # length + type + payload + crc

        if kind == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", payload)
        elif kind == b"IDAT":
            idat += payload
        elif kind == b"IEND":
            break

    if depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise SystemExit(
            "unsupported PNG: need 8-bit RGB or RGBA, non-interlaced "
            "(got depth=%s color_type=%s interlace=%s)" % (depth, color_type, interlace))

    channels = 3 if color_type == 2 else 4
    stride = width * channels
    raw = zlib.decompress(bytes(idat))

    out = bytearray(stride * height)
    previous = bytearray(stride)
    pos = 0
    for row in range(height):
        filter_type = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride

        if filter_type == 1:      # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:    # Up
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif filter_type == 3:    # Average
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:    # Paeth
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                upper_left = previous[i - channels] if i >= channels else 0
                line[i] = (line[i] + _paeth(left, previous[i], upper_left)) & 0xFF
        elif filter_type != 0:
            raise SystemExit("unknown PNG filter type %d on row %d" % (filter_type, row))

        out[row * stride:(row + 1) * stride] = line
        previous = line

    return width, height, channels, bytes(out)


def box_resize(src, src_w, src_h, channels, dst_w, dst_h):
    """Area-average downscale.

    A box filter rather than a nearest-neighbour pick: at the 3.2x reduction
    this asset needs, dropping pixels would alias the checkered flag into
    moire and turn the smoke into blocks.
    """
    out = bytearray(dst_w * dst_h * 3)
    stride = src_w * channels

    # Source column spans are the same for every row, so resolve them once.
    spans = []
    for x in range(dst_w):
        x0 = (x * src_w) // dst_w
        x1 = max(x0 + 1, ((x + 1) * src_w) // dst_w)
        spans.append((x0, x1))

    for y in range(dst_h):
        y0 = (y * src_h) // dst_h
        y1 = max(y0 + 1, ((y + 1) * src_h) // dst_h)
        base = y * dst_w * 3

        for x, (x0, x1) in enumerate(spans):
            r = g = b = 0
            for sy in range(y0, y1):
                row = sy * stride
                for sx in range(x0, x1):
                    p = row + sx * channels
                    r += src[p]
                    g += src[p + 1]
                    b += src[p + 2]

            count = (y1 - y0) * (x1 - x0)
            out[base + x * 3] = r // count
            out[base + x * 3 + 1] = g // count
            out[base + x * 3 + 2] = b // count

    return bytes(out)


def to_rgb565(rgb, width, height):
    out = bytearray(width * height * 2)
    for index in range(width * height):
        r = rgb[index * 3]
        g = rgb[index * 3 + 1]
        b = rgb[index * 3 + 2]
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        struct.pack_into("<H", out, index * 2, value)
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source")
    parser.add_argument("destination")
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    args = parser.parse_args()

    src_w, src_h, channels, pixels = read_png(args.source)
    print("source: %dx%d, %d channels" % (src_w, src_h, channels))

    src_ratio = src_w / src_h
    dst_ratio = args.width / args.height
    if abs(src_ratio - dst_ratio) > 0.01:
        print("warning: aspect ratio %.3f does not match target %.3f, the image "
              "will be distorted" % (src_ratio, dst_ratio), file=sys.stderr)

    resized = box_resize(pixels, src_w, src_h, channels, args.width, args.height)
    blob = to_rgb565(resized, args.width, args.height)

    with open(args.destination, "wb") as handle:
        handle.write(blob)

    print("wrote %s: %dx%d RGB565, %d bytes" % (args.destination, args.width,
                                                args.height, len(blob)))


if __name__ == "__main__":
    main()
