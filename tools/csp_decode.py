#!/usr/bin/env python3
"""
Decode Tubes .CSP "compiled sprite" resources.

A .CSP is not a bitmap - it is generated 16-bit x86 code that draws the
sprite with unrolled stores and returns. Transparency is implicit: pixels
that are not drawn simply have no instruction.

Instruction grammar (the whole format):

    c6 44 dd ii              mov byte [si+disp8],  imm8
    c7 44 dd ii ii           mov word [si+disp8],  imm16
    c6 84 dddd ii            mov byte [si+disp16], imm8
    c7 84 dddd ii ii         mov word [si+disp16], imm16
    d0 c0                    rol al,1
    83 d6 00                 adc si,0
    ee                       out dx,al
    cb                       retf

Those three are the Mode X plane switch, but they are independent
instructions, not a fixed block: rol/adc may repeat without an intervening
out in order to skip planes that contain no pixels. The carry flag must
therefore be tracked across instructions.

`al` holds the VGA sequencer map mask and starts at 0x11: rotating it left
gives
0x11 -> 0x22 -> 0x44 -> 0x88 -> 0x11, so the carry out is set exactly once
per four planes and `adc si,0` advances one byte per four pixels. The
sequencer only looks at the low nibble, so the plane is bit_index(al & 0x0f).

Screen mapping, with a Mode X plane stride of 80 bytes (320 / 4):

    x = (byte_offset % 80) * 4 + plane
    y =  byte_offset // 80

Usage:
  csp_decode.py INFO   <file.CSP>...
  csp_decode.py RENDER <palette.PAL> <outdir> <file.CSP>...
"""

import os
import struct
import sys

PLANE_STRIDE = 80          # bytes per row per plane (320 / 4)
INITIAL_MASK = 0x11        # al on entry; low nibble is the real plane mask
ROL_AL_1 = bytes([0xd0, 0xc0])
ADC_SI_0 = bytes([0x83, 0xd6, 0x00])


class ParseError(Exception):
    pass


def decode(blob):
    """Interpret a compiled sprite. Returns {(x, y): color} plus stats."""
    pixels = {}
    i = 0
    si = 0
    mask = INITIAL_MASK
    cf = 0
    n_writes = 0
    n_switch = 0

    while i < len(blob):
        op = blob[i]

        if op == 0xcb:                     # retf
            if i + 1 != len(blob):
                raise ParseError(
                    f"retf at {i:#x} but file is {len(blob):#x} bytes")
            break

        if blob[i:i + 2] == ROL_AL_1:
            cf = (mask >> 7) & 1
            mask = ((mask << 1) | cf) & 0xff
            n_switch += 1
            i += 2
            continue

        if blob[i:i + 3] == ADC_SI_0:
            si += cf
            cf = 0
            i += 3
            continue

        if op == 0xee:                     # out dx,al - no decoding effect
            i += 1
            continue

        if op in (0xc6, 0xc7) and i + 1 < len(blob):
            modrm = blob[i + 1]
            if modrm == 0x44:
                disp = struct.unpack_from("<b", blob, i + 2)[0]
                dlen = 1
            elif modrm == 0x84:
                disp = struct.unpack_from("<h", blob, i + 2)[0]
                dlen = 2
            else:
                raise ParseError(f"unexpected modrm {modrm:#04x} at {i:#x}")

            j = i + 2 + dlen
            data = blob[j:j + (1 if op == 0xc6 else 2)]
            i = j + len(data)

            plane = (mask & 0x0f).bit_length() - 1
            for k, value in enumerate(data):
                off = si + disp + k
                x = (off % PLANE_STRIDE) * 4 + plane
                y = off // PLANE_STRIDE
                pixels[(x, y)] = value
                n_writes += 1
            continue

        raise ParseError(f"unexpected opcode {op:#04x} at {i:#x}")

    return pixels, {"writes": n_writes, "plane_switches": n_switch}


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
        ok = bad = 0
        for path in sys.argv[2:]:
            try:
                px, st = decode(open(path, "rb").read())
            except ParseError as e:
                print(f"  FAIL {os.path.basename(path):<14} {e}")
                bad += 1
                continue
            xs = [p[0] for p in px]
            ys = [p[1] for p in px]
            print(f"  {os.path.basename(path):<14} "
                  f"{st['writes']:>6} px  {st['plane_switches']:>4} switches  "
                  f"x {min(xs):>3}..{max(xs):<3} y {min(ys):>3}..{max(ys):<3} "
                  f"({max(xs)-min(xs)+1}x{max(ys)-min(ys)+1})")
            ok += 1
        print(f"\nparsed {ok}, failed {bad}")
        return 1 if bad else 0

    if cmd == "RENDER":
        from PIL import Image
        pal = load_palette(sys.argv[2])
        outdir = sys.argv[3]
        os.makedirs(outdir, exist_ok=True)
        for path in sys.argv[4:]:
            px, _ = decode(open(path, "rb").read())
            if not px:
                continue
            xs = [p[0] for p in px]
            ys = [p[1] for p in px]
            x0, y0 = min(xs), min(ys)
            w, h = max(xs) - x0 + 1, max(ys) - y0 + 1
            img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            out = img.load()
            for (x, y), c in px.items():
                out[x - x0, y - y0] = pal[c] + (255,)
            name = os.path.splitext(os.path.basename(path))[0] + ".png"
            img.save(os.path.join(outdir, name))
        print(f"rendered {len(sys.argv) - 4} sprites to {outdir}")
        return 0

    print(f"unknown command {cmd}")
    return 2


if __name__ == "__main__":
    sys.exit(main())
