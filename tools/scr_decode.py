#!/usr/bin/env python3
"""
Decode Tubes .SCR files - recorded attract-mode demos.

Despite the extension, .SCR is not a script or a screen: it is a recording of
player input, replayed by feeding it into the normal game loop. The demo
pointer set up by 1000:5f4b is consumed by 1000:3a67, the largest function in
the binary (the main loop).

    u16   frame count (bytes following this field)
    u32   RNG seed          [inferred - see below]
    u8[]  one input bitmask per frame

Bit assignments (inferred from behaviour, not yet read out of the input
handler):

    0x01  up          20 frames, never held
    0x02  down      1357 frames, runs up to 30 - held to drop faster
    0x04  left       280 frames, short taps
    0x08  right      258 frames, short taps
    0x10  button A   134 frames, never held
    0x20  button B   unused in the shipped demo

Supporting evidence: 0x04 and 0x08 have near-identical run statistics, as a
left/right pair should. Only 0x02 is held for long stretches. And across
11,974 frames the physically impossible combinations never occur - 0x03
(up+down) and 0x0c (left+right) are entirely absent - while the four
combinations that do appear (0x05, 0x06, 0x0a, 0x18) are all legal
diagonals or direction+button.

The 4 header bytes are read as an RNG seed because a replay must reproduce
the same atom sequence to stay in sync, and Turbo Pascal's System.RandSeed is
a 32-bit LongInt. This has not been confirmed against the playback code.

Usage:
  scr_decode.py INFO <file.SCR>
  scr_decode.py DUMP <file.SCR> [maxframes]
"""

import struct
import sys
from collections import Counter

BUTTONS = [
    (0x01, "up"),
    (0x02, "down"),
    (0x04, "left"),
    (0x08, "right"),
    (0x10, "A"),
    (0x20, "B"),
]

IMPOSSIBLE = [(0x03, "up+down"), (0x0C, "left+right")]


def parse(path):
    blob = open(path, "rb").read()
    count = struct.unpack_from("<H", blob, 0)[0]
    if count != len(blob) - 2:
        raise SystemExit(
            f"{path}: header says {count} bytes follow, file has {len(blob) - 2}")
    seed = struct.unpack_from("<I", blob, 2)[0]
    return seed, blob[6:]


def describe(mask):
    if mask == 0:
        return "-"
    return "+".join(name for bit, name in BUTTONS if mask & bit) or f"?{mask:#04x}"


def main():
    if len(sys.argv) < 3:
        print(__doc__.strip())
        return 2
    cmd, path = sys.argv[1].upper(), sys.argv[2]
    seed, frames = parse(path)

    if cmd == "INFO":
        print(f"frames    : {len(frames)}")
        print(f"seed      : {seed:#010x} ({seed})")
        idle = frames.count(0)
        print(f"idle      : {idle} ({100.0 * idle / len(frames):.1f}%)")

        print("\nper-button:")
        for bit, name in BUTTONS:
            idx = [i for i, v in enumerate(frames) if v & bit]
            if not idx:
                print(f"  {name:<6} unused")
                continue
            runs, cur = [], 1
            for a, b in zip(idx, idx[1:]):
                if b == a + 1:
                    cur += 1
                else:
                    runs.append(cur)
                    cur = 1
            runs.append(cur)
            print(f"  {name:<6} {len(idx):>5} frames  {len(runs):>4} runs  "
                  f"mean {sum(runs) / len(runs):.2f}  max {max(runs)}")

        print("\nsanity - impossible combinations:")
        for mask, name in IMPOSSIBLE:
            n = sum(1 for v in frames if v & mask == mask)
            print(f"  {name:<12} {n}  {'OK' if n == 0 else 'UNEXPECTED'}")

        unknown = {v for v in frames if v & ~0x3F}
        if unknown:
            print(f"\nframes with unknown bits set: {sorted(unknown)}")
        return 0

    if cmd == "DUMP":
        limit = int(sys.argv[3]) if len(sys.argv) > 3 else len(frames)
        prev, start = None, 0
        for i, v in enumerate(frames[:limit]):
            if v != prev:
                if prev is not None:
                    print(f"  {start:>5}..{i - 1:<5} {describe(prev)}")
                prev, start = v, i
        print(f"  {start:>5}..{min(limit, len(frames)) - 1:<5} {describe(prev)}")
        return 0

    print(f"unknown command {cmd}")
    return 2


if __name__ == "__main__":
    sys.exit(main())
