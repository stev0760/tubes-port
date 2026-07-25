#!/usr/bin/env python3
"""
Locate Borland Pascal RTL code inside a target executable.

Compile a reference program with the same TP7 toolchain, then run this to find
which regions of the target are stock runtime library rather than game code.
Anything it identifies can be left alone during decompilation.

Usage:  rtl_match.py REFERENCE.EXE TARGET.EXE [--min-run N]

Matches shorter than ~32 bytes are frequently coincidental; runs that share a
common delta with many other runs are the trustworthy signal, since a whole
linked unit lands at one fixed offset.
"""

import struct
import sys
from collections import Counter


def load_image(path):
    """Return the load image (the file minus its MZ header)."""
    data = open(path, "rb").read()
    if data[:2] not in (b"MZ", b"ZM"):
        raise SystemExit(f"{path}: not an MZ executable")
    return data[struct.unpack_from("<H", data, 8)[0] * 16:]


def find_runs(ref, target, seed=16):
    """Greedily find maximal byte-identical runs of `ref` inside `target`."""
    runs = []
    i = 0
    while i < len(ref) - seed:
        j = target.find(ref[i:i + seed])
        if j < 0:
            i += 1
            continue
        n = seed
        while i + n < len(ref) and j + n < len(target) and ref[i + n] == target[j + n]:
            n += 1
        runs.append((i, j, n))
        i += n
    return runs


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        print(__doc__.strip())
        return 2
    min_run = 16
    if "--min-run" in sys.argv:
        min_run = int(sys.argv[sys.argv.index("--min-run") + 1])

    ref = load_image(args[0])
    target = load_image(args[1])
    runs = [r for r in find_runs(ref, target) if r[2] >= min_run]

    matched = sum(n for _, _, n in runs)
    print(f"reference image : {len(ref)} bytes")
    print(f"target image    : {len(target)} bytes")
    print(f"matched         : {matched} bytes in {len(runs)} runs "
          f"({100 * matched / len(ref):.1f}% of reference)")

    # A linked unit relocates as a block, so every run inside it shares one
    # delta. Clusters are real; singleton deltas are usually noise.
    print("\nclusters by delta (reference -> target):")
    counts = Counter(j - i for i, j, _ in runs)
    for delta, k in counts.most_common(10):
        total = sum(n for i, j, n in runs if j - i == delta)
        tag = "" if k > 1 else "   (singleton - treat as suspect)"
        print(f"  {delta:#010x}  {k:3d} runs  {total:6d} bytes{tag}")

    solid = [d for d, k in counts.items() if k > 1]
    if solid:
        print("\nhigh-confidence RTL extents:")
        for delta in sorted(solid):
            group = [(i, j, n) for i, j, n in runs if j - i == delta]
            lo = min(j for _, j, _ in group)
            hi = max(j + n for _, j, n in group)
            print(f"  target {lo:#08x} .. {hi:#08x}  ({hi - lo} bytes)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
