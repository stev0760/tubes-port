#!/usr/bin/env python3
"""
Decode Tubes .GFX raster images.

    u16   width
    u16   height
    u8[]  width * height pixels, 8-bit palette indices

The pixel data is **planar**, in the same Mode X layout the compiled sprites
draw into - not chunky. All of plane 0 comes first as a contiguous
(width / 4) x height block, then plane 1, 2 and 3:

    plane  = x % 4
    index  = plane * (width // 4) * height + y * (width // 4) + x // 4

Decoding it as chunky yields the image tiled 4x horizontally and squashed 4x
vertically, which is the tell-tale symptom if this ever regresses.

Three resources (SOFT, CLOUD, AMWRITE) carry an extra leading 0xE5 byte
before that header, and those same three are fetched by a different routine
in the game (21ea:045f rather than 21ea:03c4 / 21ea:035b). The prefix marks
**chunky** storage: their pixels are already linear and must NOT be
de-planarized. Everything else is planar.

Palettes are per-scene, not global. SOFT.GFX needs SOFT.PAL and CLOUD.GFX
needs INTRO.PAL; the in-game art uses TUBES.PAL.

Usage:
  gfx_decode.py INFO   <file.GFX>...
  gfx_decode.py RENDER <palette.PAL> <outdir> <file.GFX>...
"""

import os
import struct
import sys

VARIANT_PREFIX = 0xE5


class ParseError(Exception):
    pass


def parse(blob):
    """Return (width, height, pixels, chunky). Header offset is inferred."""
    for off in (0, 1):
        if len(blob) < off + 4:
            continue
        w, h = struct.unpack_from("<HH", blob, off)
        if w and h and off + 4 + w * h == len(blob):
            chunky = off == 1 and blob[0] == VARIANT_PREFIX
            return w, h, blob[off + 4:], chunky
    raise ParseError(
        f"no header at offset 0 or 1 fits a {len(blob)}-byte file")


def pixels(blob):
    """Decode to chunky pixels regardless of which storage variant is used."""
    w, h, data, chunky = parse(blob)
    return w, h, data if chunky else deplanarize(w, h, data)


def deplanarize(w, h, planar):
    """Mode X plane-major -> chunky, row by row."""
    if w % 4:
        raise ParseError(f"width {w} is not a multiple of 4")
    pw = w // 4
    plane_size = pw * h
    out = bytearray(w * h)
    for plane in range(4):
        base = plane * plane_size
        for y in range(h):
            row = base + y * pw
            dst = y * w + plane
            for px in range(pw):
                out[dst + px * 4] = planar[row + px]
    return bytes(out)


def load_palette(path):
    raw = open(path, "rb").read()
    if len(raw) != 768:
        raise SystemExit(f"{path}: expected 768 bytes, got {len(raw)}")
    # VGA DAC values are 6-bit.
    return [tuple(min(255, (c * 255) // 63) for c in raw[i * 3:i * 3 + 3])
            for i in range(256)]


def main():
    if len(sys.argv) < 3:
        print(__doc__.strip())
        return 2
    cmd = sys.argv[1].upper()

    if cmd == "INFO":
        ok = 0
        bad = []
        for path in sys.argv[2:]:
            blob = open(path, "rb").read()
            try:
                w, h, px = pixels(blob)
            except ParseError as e:
                bad.append((os.path.basename(path), str(e)))
                continue
            variant = " (0xE5 chunky)" if blob[0] == VARIANT_PREFIX else ""
            print(f"  {os.path.basename(path):<14} {w:>4} x {h:<4} "
                  f"{len(px):>7} px  colours={len(set(px)):>3}{variant}")
            ok += 1
        print(f"\nparsed {ok}, failed {len(bad)}")
        for n, e in bad:
            print(f"  FAIL {n}: {e}")
        return 1 if bad else 0

    if cmd == "RENDER":
        from PIL import Image
        pal = load_palette(sys.argv[2])
        outdir = sys.argv[3]
        os.makedirs(outdir, exist_ok=True)
        n = 0
        for path in sys.argv[4:]:
            w, h, px = pixels(open(path, "rb").read())
            img = Image.new("P", (w, h))
            img.putdata(px)
            flat = []
            for rgb in pal:
                flat.extend(rgb)
            img.putpalette(flat)
            name = os.path.splitext(os.path.basename(path))[0] + ".png"
            img.convert("RGB").save(os.path.join(outdir, name))
            n += 1
        print(f"rendered {n} images to {outdir}")
        return 0

    print(f"unknown command {cmd}")
    return 2


if __name__ == "__main__":
    sys.exit(main())
