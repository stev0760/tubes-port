#!/usr/bin/env python3
"""Pull the Instructions slideshow out of `1b2e:2d63` mechanically.

4,510 bytes and 152 strings is too much to read by hand and exactly the shape a
machine is good at: every slide is a run of text calls between two comparisons
on the slide number, and every text call is a fixed push sequence.

    WriteAt      2000:36ab   pushes x, y, colour, mode, str
    WriteCentred 2000:37ea   pushes x0, x1, y, colour, mode, str

A string argument is `MOV DI,<off>` / `PUSH CS` / `PUSH DI`, so the offset is
CS-relative in segment 1b2e and resolves against a byte dump of the same
segment. Anything that does not match the pattern is reported rather than
skipped, so a slide cannot go missing quietly.

Usage: extract_slides.py disasm-2d63.txt strings.txt
"""
from __future__ import annotations

import re
import sys

WRITE_AT = "0x2000:36ab"
WRITE_CENTRED = "0x2000:37ea"
SET_FONT = "0x2000:3fab"

INSN = re.compile(r"^(1b2e|1000):([0-9a-f]{4})\s+(.*?)\s*$")


def load_disasm(path):
    out = []
    for line in open(path):
        m = INSN.match(line.rstrip("\n"))
        if m:
            out.append((int(m.group(2), 16), m.group(3)))
    return out


def load_strings(path):
    """A DumpBytes listing of segment 1b2e -> {offset: byte}."""
    data = {}
    row = re.compile(r"1b2e:([0-9a-f]{4})\s+((?:[0-9a-f]{2} )+)")
    for line in open(path):
        m = row.search(line)
        if not m:
            continue
        base = int(m.group(1), 16)
        for i, b in enumerate(m.group(2).split()):
            data[base + i] = int(b, 16)
    return data


def pascal(data, off):
    n = data.get(off)
    if n is None:
        return None
    return "".join(chr(data.get(off + 1 + i, 32)) for i in range(n))


def main():
    ins = load_disasm(sys.argv[1])
    strs = load_strings(sys.argv[2]) if len(sys.argv) > 2 else {}

    # Walk backwards from each text call collecting the PUSHes that feed it.
    calls = []
    for i, (addr, text) in enumerate(ins):
        if not text.startswith("CALLF"):
            continue
        target = text.split()[-1]
        if target not in (WRITE_AT, WRITE_CENTRED, SET_FONT):
            continue
        args, stroff = [], None
        j = i - 1
        while j >= 0 and len(args) < 10:
            a, t = ins[j]
            if t.startswith("PUSH 0x") or t.startswith("PUSH -"):
                v = t.split()[1]
                args.append(int(v, 16) if v.startswith("0x") else int(v, 16))
            elif t.startswith("MOV DI,0x"):
                stroff = int(t.split(",")[1], 16)
            elif t.startswith("CALLF") or t.startswith("CALL "):
                break
            j -= 1
        calls.append((addr, target, list(reversed(args)), stroff))

    kind = {WRITE_AT: "at", WRITE_CENTRED: "mid", SET_FONT: "font"}
    for addr, target, args, stroff in calls:
        s = pascal(strs, stroff) if stroff is not None else None
        print(f"{addr:04x} {kind[target]:4s} args={args} "
              f"str={stroff:#06x} {s!r}" if stroff is not None
              else f"{addr:04x} {kind[target]:4s} args={args}")

    print(f"\n{len(calls)} text calls", file=sys.stderr)
    offs = [o for _, _, _, o in calls if o is not None]
    if offs:
        print(f"string offsets {min(offs):#06x}..{max(offs):#06x}",
              file=sys.stderr)


if __name__ == "__main__":
    main()
