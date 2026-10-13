#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Adafruit Industries
# SPDX-License-Identifier: MIT
"""Make src/drivers/dvhstx/font_cp437.c from a 12x24 Terminus BDF.

    python3 tools/cp437_font.py ter-u24n.bdf > src/drivers/dvhstx/font_cp437.c

Each glyph becomes 24 rows of 14 bits, bit 13 the leftmost of the 14
columns of a text cell. The 12 pixel wide glyph is centered, and box drawing
and block characters are stretched to both edges so that neighboring cells
join.
"""
import sys

CELL_W, CELL_H = 14, 24

# Code page 437 shows these as pictures rather than control characters.
LOW = [
    0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
    0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
    0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
    0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
]


# Terminus has no pointers, so use the triangles most CP437 fonts draw.
SUBSTITUTE = {0x25BA: 0x25B6, 0x25C4: 0x25C0}


def cp437_to_unicode(c):
    if c < 0x20:
        return LOW[c]
    if c == 0x7F:
        return 0x2302
    return ord(bytes([c]).decode("cp437"))


def read_bdf(path):
    """Return {codepoint: (rows, width, height, xoff, yoff)} and the ascent."""
    glyphs = {}
    ascent = None
    with open(path, encoding="latin-1") as f:
        lines = iter(f.read().splitlines())
    for line in lines:
        if line.startswith("FONT_ASCENT"):
            ascent = int(line.split()[1])
        if not line.startswith("STARTCHAR"):
            continue
        enc = bbx = None
        for line in lines:
            if line.startswith("ENCODING"):
                enc = int(line.split()[1])
            elif line.startswith("BBX"):
                bbx = [int(v) for v in line.split()[1:]]
            elif line == "BITMAP":
                break
        rows = []
        for line in lines:
            if line == "ENDCHAR":
                break
            rows.append(int(line, 16))
        w, h, xoff, yoff = bbx
        nbits = ((w + 7) // 8) * 8
        # Left align each row in w bits.
        rows = [r >> (nbits - w) for r in rows]
        glyphs[enc] = (rows, w, h, xoff, yoff)
    return glyphs, ascent


def joins(u):
    # Box drawing and block elements.
    return 0x2500 <= u <= 0x259F


def shade(u):
    # Light, medium and dark shade repeat every 2 pixels.
    return 0x2591 <= u <= 0x2593


def cell(glyph, ascent, stretch, period):
    rows, w, h, xoff, yoff = glyph
    out = [0] * CELL_H
    top = ascent - (yoff + h)
    left = (CELL_W - 12) // 2 + xoff
    for i, r in enumerate(rows):
        y = top + i
        if not 0 <= y < CELL_H:
            continue
        bits = 0
        for x in range(w):
            if r & (1 << (w - 1 - x)):
                cx = left + x
                if 0 <= cx < CELL_W:
                    bits |= 1 << (CELL_W - 1 - cx)
        if stretch:
            # Copy the pixel one period in from each edge outwards.
            if r & (1 << (w - period)):
                bits |= ((1 << left) - 1) << (CELL_W - left)
            if r & (1 << (period - 1)):
                bits |= (1 << (CELL_W - left - w)) - 1
        out[y] = bits
    return out


def main():
    glyphs, ascent = read_bdf(sys.argv[1])
    missing = []
    table = []
    for c in range(256):
        u = cp437_to_unicode(c)
        if u in (0x0000, 0x0020, 0x00A0):
            table.append([0] * CELL_H)
            continue
        g = glyphs.get(SUBSTITUTE.get(u, u))
        if g is None:
            missing.append((c, u))
            table.append([0] * CELL_H)
            continue
        if g[1] != 12 or g[3] != 0 or ascent - (g[4] + g[2]) < 0 or ascent - g[4] > CELL_H:
            sys.exit(f"0x{c:02X} U+{u:04X} does not fit a 12x{CELL_H} box")
        table.append(cell(g, ascent, joins(u), 2 if shade(u) else 1))
    if missing:
        sys.exit("missing: " + ", ".join(f"0x{c:02X} U+{u:04X}" for c, u in missing))

    print("// SPDX-FileCopyrightText: 2014 Dimitar Toshkov Zhekov")
    print("// SPDX-License-Identifier: OFL-1.1")
    print("//")
    print("// Code page 437 font for text mode, made by tools/cp437_font.py from")
    print("// ter-u24n.bdf, Terminus Font 4.39.")
    print("// Licensed under the SIL Open Font License, Version 1.1:")
    print("// https://openfontlicense.org/open-font-license-official-text/")
    print("//")
    print("// 24 rows per glyph, bit 13 the leftmost of the 14 columns.")
    print()
    print('#include "font_cp437.h"')
    print()
    print("const uint16_t font_cp437[256][FONT_CP437_HEIGHT] = {")
    for c, rows in enumerate(table):
        u = cp437_to_unicode(c)
        vals = ", ".join(f"0x{r:04x}" for r in rows)
        print(f"    /* 0x{c:02X} U+{u:04X} */ {{{vals}}},")
    print("};")


if __name__ == "__main__":
    main()
