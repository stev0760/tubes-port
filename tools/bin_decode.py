#!/usr/bin/env python3
"""
Decode Tubes .BIN files - DOS text-mode screen dumps.

There is exactly one in the archive, TUBESEND.BIN, and it was the last
undecoded resource in the game. It is not code, not a bitmap and not a table:
it is a raw dump of VGA text-mode video memory, the format `Move`d straight to
segment 0xB800 by a Pascal program that wants to print a full-screen banner and
exit.

    u8[2] per cell, row-major:  [character (CP437), attribute]

    attribute:  bit 7    blink
                bits 6-4 background colour
                bits 3-0 foreground colour

3,680 bytes is 1,840 cells, and the only sensible factorisation for an 80
column screen is **80 x 23**. Not 25: the bottom two rows are left alone so
that the shell prompt lands under the art instead of scrolling it. That is the
ordinary way a DOS program signs off, and the dump's own last row is the
drop-shadow of the boxes above it, which would look wrong anywhere but the
bottom of the picture.

Nothing loads it. "TUBESEND" appears exactly ONCE in the whole game directory -
in the archive's own directory entry - and neither TUBES.EXE, SETUP.EXE nor the
drivers reference it or any other `.BIN`. The unpacked image names 112
resources and none of them has that extension. So it is an orphan carried in a
shared archive; see docs/reversing-notes.md for what that says about which
edition this copy is.

Usage:
  bin_decode.py TEXT  TUBES.RES NAME     render as CP437 text
  bin_decode.py ANSI  TUBES.RES NAME     render in colour, for a terminal
  bin_decode.py INFO  TUBES.RES NAME     geometry and the attributes used
"""

import sys
from collections import Counter

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from res_extract import parse, lzss_decompress  # noqa: E402

WIDTH = 80

# The 16 CGA colours as xterm-256 indices, in DOS order: the low 8 are the
# dark half and bit 3 brightens.
CGA_TO_XTERM = [0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15]


def load(res_path, name):
    data, _version, _dir_off, entries = parse(res_path)
    for e in entries:
        if e.name.upper() == name.upper():
            return lzss_decompress(data[e.offset:e.offset + e.ssize], e.usize)
    raise SystemExit(f"{res_path}: no resource named {name}")


def cells(blob):
    """(character, attribute) pairs, and the row count they imply."""
    n = len(blob) // 2
    if len(blob) % 2 or n % WIDTH:
        raise SystemExit(f"{len(blob)} bytes is not a whole {WIDTH}-column screen")
    return [(blob[i * 2], blob[i * 2 + 1]) for i in range(n)], n // WIDTH


def render_text(blob):
    cp437 = bytes(range(256)).decode("cp437")
    cs, rows = cells(blob)
    for y in range(rows):
        print("".join(cp437[c] for c, _a in cs[y * WIDTH:(y + 1) * WIDTH]))


def render_ansi(blob):
    cp437 = bytes(range(256)).decode("cp437")
    cs, rows = cells(blob)
    for y in range(rows):
        out = []
        last = None
        for c, a in cs[y * WIDTH:(y + 1) * WIDTH]:
            if a != last:
                fg = CGA_TO_XTERM[a & 0x0F]
                bg = CGA_TO_XTERM[(a >> 4) & 0x07]
                out.append(f"\033[38;5;{fg}m\033[48;5;{bg}m")
                last = a
            out.append(cp437[c])
        out.append("\033[0m")
        print("".join(out))


def info(blob):
    cs, rows = cells(blob)
    print(f"{len(blob)} bytes, {len(cs)} cells, {WIDTH} x {rows}")
    attrs = Counter(a for _c, a in cs)
    print(f"{len(attrs)} distinct attributes:")
    for a, n in attrs.most_common():
        blink = " BLINK" if a & 0x80 else ""
        print(f"  0x{a:02x}  fg={a & 15:<2d} bg={(a >> 4) & 7}{blink}   x{n}")
    print(f"characters used: {len(Counter(c for c, _a in cs))} distinct")


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    mode, res_path, name = sys.argv[1].upper(), sys.argv[2], sys.argv[3]
    blob = load(res_path, name)
    if mode == "TEXT":
        render_text(blob)
    elif mode == "ANSI":
        render_ansi(blob)
    elif mode == "INFO":
        info(blob)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
