#!/usr/bin/env python3
"""
Decode Tubes bitmap fonts (.816 and .88).

Standard VGA glyph tables: 256 characters, one byte per scanline, most
significant bit leftmost, no header. The extension gives the cell size, and
the file size follows from it:

    .816   8 x 16 cells   256 * 16 = 4096 bytes
    .88    8 x  8 cells   256 *  8 = 2048 bytes

Cell width is always 8 because a scanline is one byte. Fonts that are
narrower in practice (TINY6X8, THIN8X8) simply leave the right-hand columns
clear, so the advance width is a property of the renderer, not the file.

Usage:
  fnt_decode.py INFO   <font>...
  fnt_decode.py RENDER <outdir> <font>...    # PNG sheet of chars 32..127
  fnt_decode.py SHOW   <font> <char>         # ASCII art for one glyph
"""

import os
import sys

CELL_HEIGHT = {".816": 16, ".88": 8}


def cell_height(path, size):
    ext = os.path.splitext(path)[1].lower()
    h = CELL_HEIGHT.get(ext)
    if h is None:
        # Fall back to inferring it, so unusual extensions still work.
        if size % 256:
            raise SystemExit(f"{path}: {size} bytes is not a multiple of 256")
        h = size // 256
    if size != 256 * h:
        raise SystemExit(
            f"{path}: expected {256 * h} bytes for {h}-line cells, got {size}")
    return h


def glyph(blob, h, code):
    """Return the glyph as a list of rows of booleans."""
    base = code * h
    return [[(blob[base + y] >> (7 - x)) & 1 == 1 for x in range(8)]
            for y in range(h)]


def used_columns(blob, h):
    """Widest column actually touched across printable glyphs."""
    widest = 0
    for code in range(32, 128):
        for y in range(h):
            b = blob[code * h + y]
            for x in range(8):
                if (b >> (7 - x)) & 1:
                    widest = max(widest, x + 1)
    return widest


def main():
    if len(sys.argv) < 3:
        print(__doc__.strip())
        return 2
    cmd = sys.argv[1].upper()

    if cmd == "INFO":
        for path in sys.argv[2:]:
            blob = open(path, "rb").read()
            h = cell_height(path, len(blob))
            nonblank = sum(1 for c in range(256)
                           if any(blob[c * h:(c + 1) * h]))
            print(f"  {os.path.basename(path):<15} 8x{h:<3} "
                  f"{len(blob):>5} bytes  {nonblank:>3}/256 glyphs used  "
                  f"widest column {used_columns(blob, h)}")
        return 0

    if cmd == "SHOW":
        path, ch = sys.argv[2], sys.argv[3]
        blob = open(path, "rb").read()
        h = cell_height(path, len(blob))
        code = int(ch, 0) if ch.startswith("0") else ord(ch[0])
        for row in glyph(blob, h, code):
            print("  " + "".join("#" if p else "." for p in row))
        return 0

    if cmd == "RENDER":
        from PIL import Image
        outdir = sys.argv[2]
        os.makedirs(outdir, exist_ok=True)
        for path in sys.argv[3:]:
            blob = open(path, "rb").read()
            h = cell_height(path, len(blob))
            cols, rows = 16, 6           # chars 32..127
            img = Image.new("RGB", (cols * 8, rows * h), (16, 16, 20))
            px = img.load()
            for i in range(96):
                code = 32 + i
                ox, oy = (i % cols) * 8, (i // cols) * h
                for y, row in enumerate(glyph(blob, h, code)):
                    for x, on in enumerate(row):
                        if on:
                            px[ox + x, oy + y] = (235, 235, 240)
            name = os.path.splitext(os.path.basename(path))[0] + ".png"
            img.resize((img.width * 3, img.height * 3), Image.NEAREST).save(
                os.path.join(outdir, name))
        print(f"rendered {len(sys.argv) - 3} fonts to {outdir}")
        return 0

    print(f"unknown command {cmd}")
    return 2


if __name__ == "__main__":
    sys.exit(main())
