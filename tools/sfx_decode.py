#!/usr/bin/env python3
"""
Decode Tubes .SFX digital sound effects to WAV.

    [0]        0xf1 marker
    [1..0x1f]  Pascal ShortString name, zero-padded (1 length + 30 chars)
    [0x20..23] u32 sample rate - 8000 Hz in every shipped sound
    [0x24]     unknown, 0 in every shipped sound
    [0x25..26] u16 sample count
    [0x27..]   unsigned 8-bit PCM, silence at 0x80

Verified across all 24 resources: the count at 0x25 equals filesize - 0x27
exactly in every case, the marker is always 0xf1, and the rate is always
8000.

The byte at 0x24 is always zero, so its meaning is unconstrained by the
shipped data. It could be a format flag or the high byte of a wider length
field - nothing here can distinguish those, since every sound is under 64KB.

Usage:
  sfx_decode.py INFO <file.SFX>...
  sfx_decode.py WAV  <outdir> <file.SFX>...
"""

import os
import struct
import sys
import wave

MARKER = 0xF1
DATA_OFFSET = 0x27


class ParseError(Exception):
    pass


def parse(blob, path="<sfx>"):
    """Return (name, rate, pcm)."""
    if not blob or blob[0] != MARKER:
        raise ParseError(
            f"{path}: expected marker {MARKER:#04x}, got "
            f"{blob[0]:#04x}" if blob else f"{path}: empty")
    n = blob[1]
    name = blob[2:2 + n].decode("ascii", "replace")
    rate = struct.unpack_from("<I", blob, 0x20)[0]
    count = struct.unpack_from("<H", blob, 0x25)[0]
    pcm = blob[DATA_OFFSET:]
    if count != len(pcm):
        raise ParseError(
            f"{path}: header says {count} samples, {len(pcm)} bytes present")
    return name, rate, pcm


def main():
    if len(sys.argv) < 3:
        print(__doc__.strip())
        return 2
    cmd = sys.argv[1].upper()

    if cmd == "INFO":
        for path in sys.argv[2:]:
            name, rate, pcm = parse(open(path, "rb").read(), path)
            lo, hi = min(pcm), max(pcm)
            mean = sum(pcm) / len(pcm)
            silence = sum(1 for s in pcm if abs(s - 0x80) <= 1)
            print(f"  {os.path.basename(path):<15} {rate:>5} Hz  "
                  f"{len(pcm):>6} samples  {len(pcm) / rate:>5.2f}s  "
                  f"range {lo:>3}..{hi:<3} mean {mean:>6.1f}  "
                  f"silent {100.0 * silence / len(pcm):>4.1f}%   \"{name}\"")
        return 0

    if cmd == "WAV":
        outdir = sys.argv[2]
        os.makedirs(outdir, exist_ok=True)
        for path in sys.argv[3:]:
            _, rate, pcm = parse(open(path, "rb").read(), path)
            out = os.path.join(
                outdir,
                os.path.splitext(os.path.basename(path))[0] + ".wav")
            with wave.open(out, "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(1)      # WAV 8-bit is unsigned, same as source
                w.setframerate(rate)
                w.writeframes(pcm)
        print(f"wrote {len(sys.argv) - 3} WAVs to {outdir}")
        return 0

    print(f"unknown command {cmd}")
    return 2


if __name__ == "__main__":
    sys.exit(main())
