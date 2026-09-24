#!/usr/bin/env python3
"""Generate the LVGL fonts of the KDatalogger GUI from the Barlow TTFs.

Two families, both OFL (tools/fonts/OFL.txt):

  kdl_font_text_<px>  Barlow SemiBold: ASCII, the Italian accented vowels, the
                      few typographic signs the UI prints and the LV_SYMBOL_*
                      glyphs the pages use, merged in from LVGL's FontAwesome.
  kdl_font_num_<px>   Barlow Condensed SemiBold: digits and the handful of
                      characters a reading can contain, nothing else.

Barlow's digits are proportional ("1" is ~60 % of "0"), so a right-aligned
reading would shift sideways every time a digit changes. The numeric faces
are post-processed here into tabular figures: every digit gets the widest
digit's advance and is centred in it. Kerning is left out of those faces for
the same reason.

A character outside these ranges renders as nothing on the panel. To add one,
extend the range below, re-run, and commit the regenerated .c files.

Needs Node.js (npx fetches lv_font_conv on first use) and the managed LVGL
component (idf.py reconfigure) for the FontAwesome file.

Usage, from the repository root:
    python3 tools/gen_fonts.py
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LV_FONT_CONV = ["npx", "--yes", "lv_font_conv@1.5.3"]
TEXT_TTF = os.path.join("tools", "fonts", "Barlow-SemiBold.ttf")
NUM_TTF = os.path.join("tools", "fonts", "BarlowCondensed-SemiBold.ttf")
SYMBOL_FONT = os.path.join("managed_components", "lvgl__lvgl", "scripts", "built_in_font",
                           "FontAwesome5-Solid+Brands+Regular.woff")
OUT_DIR = os.path.join("components", "gui", "assets", "fonts")

TEXT_SIZES = (10, 12, 14, 20)
NUM_SIZES = (16, 22, 26, 32)

# ASCII; degree, middle dot; the Italian accented vowels (upper and lower);
# Greek capital delta, em dash, bullet.
TEXT_RANGES = ("0x20-0x7E,0xB0,0xB7,"
               "0xC0,0xC8,0xC9,0xCC,0xD2,0xD9,0xE0,0xE8,0xE9,0xEC,0xF2,0xF9,"
               "0x394,0x2014,0x2022")

# LV_SYMBOL_PLAY, LEFT, RIGHT, WARNING, UP, DOWN, USB (lv_symbol_def.h).
SYMBOL_RANGES = "0xF04B,0xF053,0xF054,0xF071,0xF077,0xF078,0xF287"

# Space, "-" (also the open-circuit "---"), ".", digits, ":", the letters of
# the fault codes SCG / SCV / ERR, em dash.
NUM_RANGES = "0x20,0x2D,0x2E,0x30-0x3A,0x43,0x45,0x47,0x52,0x53,0x56,0x2014"

COMMON_OPTS = ["--no-compress", "--no-prefilter", "--bpp", "4", "--format", "lvgl",
               "--lv-include", "lvgl.h"]

GLYPH_COMMENT = re.compile(r'/\* U\+([0-9A-F]{4,6}) ')
GLYPH_DSC_ROW = re.compile(r'\{\.bitmap_index = \d+, \.adv_w = (\d+), .*?\.ofs_x = (-?\d+),')


def run(cmd):
    print(" ".join(cmd))
    result = subprocess.run(cmd, cwd=ROOT)
    if result.returncode != 0:
        sys.exit("lv_font_conv failed with exit code %d" % result.returncode)


def make_digits_tabular(path):
    """Give 0-9 the widest digit's advance, centring each glyph in its cell.

    glyph_dsc[] lists glyphs in the order of the bitmap comments, after the
    reserved id 0; adv_w is in 1/16 px and ofs_x in whole pixels.
    """
    with open(path, encoding="utf-8") as handle:
        text = handle.read()

    codepoints = [int(cp, 16) for cp in GLYPH_COMMENT.findall(text)]
    start = text.index("glyph_dsc[] = {")
    end = text.index("};", start)
    # The first row is the reserved glyph id 0, which has no bitmap comment.
    rows = list(GLYPH_DSC_ROW.finditer(text, start, end))[1:]
    if len(rows) != len(codepoints):
        sys.exit("%s: %d glyph rows but %d glyph comments" % (path, len(rows), len(codepoints)))

    digits = [(row, cp) for row, cp in zip(rows, codepoints) if 0x30 <= cp <= 0x39]
    if len(digits) != 10:
        sys.exit("%s: expected 10 digits, found %d" % (path, len(digits)))
    cell = max(int(row.group(1)) for row, _ in digits)

    # Rewrite back to front so earlier match offsets stay valid.
    for row, _ in reversed(digits):
        adv_w = int(row.group(1))
        ofs_x = int(row.group(2)) + round((cell - adv_w) / 2 / 16)
        new = row.group(0).replace(".adv_w = %s," % row.group(1), ".adv_w = %d," % cell)
        new = re.sub(r"\.ofs_x = -?\d+,", ".ofs_x = %d," % ofs_x, new)
        text = text[:row.start()] + new + text[row.end():]

    marker = " ******************************************************************************/"
    note = (" * Digits made tabular (adv_w = %d) by tools/gen_fonts.py\n" % cell) + marker
    text = text.replace(marker, note, 1)

    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def main():
    for required in (TEXT_TTF, NUM_TTF, SYMBOL_FONT):
        if not os.path.isfile(os.path.join(ROOT, required)):
            sys.exit("missing %s" % required)

    for size in TEXT_SIZES:
        name = "kdl_font_text_%d" % size
        run(LV_FONT_CONV + COMMON_OPTS + [
            "--size", str(size),
            "--font", TEXT_TTF, "-r", TEXT_RANGES,
            "--font", SYMBOL_FONT, "-r", SYMBOL_RANGES,
            "-o", os.path.join(OUT_DIR, name + ".c"), "--lv-font-name", name])

    for size in NUM_SIZES:
        name = "kdl_font_num_%d" % size
        out = os.path.join(OUT_DIR, name + ".c")
        run(LV_FONT_CONV + COMMON_OPTS + [
            "--size", str(size), "--no-kerning",
            "--font", NUM_TTF, "-r", NUM_RANGES,
            "-o", out, "--lv-font-name", name])
        make_digits_tabular(os.path.join(ROOT, out))


if __name__ == "__main__":
    main()
