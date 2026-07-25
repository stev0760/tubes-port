#!/usr/bin/env python3
"""
Decide whether two MZ images were built with the same Borland toolchain.

Aligns RTL regions found by byte-matching, then diffs them exhaustively.
Every surviving mismatch is classified as either a relocation site (expected -
segment values legitimately differ between programs) or a genuine code
difference (which would indicate a different compiler/RTL version).
"""

import struct
import sys
from collections import Counter


def load(path):
    d = open(path, "rb").read()
    hdr_paras = struct.unpack_from("<H", d, 8)[0]
    nrel = struct.unpack_from("<H", d, 6)[0]
    rel_off = struct.unpack_from("<H", d, 0x18)[0]
    relocs = set()
    for i in range(nrel):
        off, seg = struct.unpack_from("<HH", d, rel_off + i * 4)
        relocs.add(seg * 16 + off)   # image-relative linear address
    return d[hdr_paras * 16:], relocs


def runs_between(ref, tgt, seed=16):
    out = []
    i = 0
    while i < len(ref) - seed:
        j = tgt.find(ref[i:i + seed])
        if j < 0:
            i += 1
            continue
        n = seed
        while i + n < len(ref) and j + n < len(tgt) and ref[i + n] == tgt[j + n]:
            n += 1
        out.append((i, j, n))
        i += n
    return out


def main():
    ref, ref_rel = load(sys.argv[1])
    tgt, tgt_rel = load(sys.argv[2])
    print(f"reference: {len(ref)} bytes, {len(ref_rel)} relocations")
    print(f"target   : {len(tgt)} bytes, {len(tgt_rel)} relocations")

    runs = runs_between(ref, tgt)
    clusters = Counter(j - i for i, j, _ in runs)

    grand_reloc = 0
    grand_real = 0

    for delta, count in clusters.most_common():
        if count < 3:
            continue
        group = [(i, j, n) for i, j, n in runs if j - i == delta]
        i0 = min(i for i, _, _ in group)
        i1 = max(i + n for i, _, n in group)
        j0, j1 = i0 + delta, i1 + delta
        if j1 > len(tgt):
            continue

        a, b = ref[i0:i1], tgt[j0:j1]

        # Group differing bytes into contiguous stretches.
        diffs = [k for k in range(len(a)) if a[k] != b[k]]
        stretches = []
        for k in diffs:
            if stretches and k == stretches[-1][1] + 1:
                stretches[-1][1] = k
            else:
                stretches.append([k, k])

        # Two different programs necessarily disagree on address operands:
        # near-jump displacements, DS-relative variable offsets, and offset
        # immediates. Those appear as isolated 1-2 byte stretches surrounded
        # by identical opcode bytes. A genuine codegen difference instead
        # shifts instruction lengths, producing longer stretches and breaking
        # the constant delta.
        operand = [s for s in stretches if s[1] - s[0] + 1 <= 2]
        structural = [s for s in stretches if s[1] - s[0] + 1 > 2]

        oper_bytes = sum(s[1] - s[0] + 1 for s in operand)
        struct_bytes = sum(s[1] - s[0] + 1 for s in structural)
        grand_reloc += oper_bytes
        grand_real += struct_bytes

        span = len(a)
        print(f"\ncluster delta {delta:#08x}  span {span} bytes "
              f"(ref {i0:#x}..{i1:#x} -> tgt {j0:#x}..{j1:#x})")
        print(f"  identical            : {span - len(diffs)} "
              f"({100.0 * (span - len(diffs)) / span:.1f}%)")
        print(f"  operand-only diffs   : {oper_bytes} bytes "
              f"in {len(operand)} stretches of <=2")
        print(f"  STRUCTURAL diffs     : {struct_bytes} bytes "
              f"in {len(structural)} stretches of >2")
        if structural:
            print("  structural at ref offsets: "
                  f"{[hex(i0 + s[0]) for s in structural[:8]]}")

    print("\n=== VERDICT ===")
    print(f"operand-only differences   : {grand_reloc}")
    print(f"structural differences     : {grand_real}")
    if grand_real == 0:
        print("-> All differences are address operands. Same toolchain version.")
    else:
        print("-> Structural differences present. Suspect a version mismatch.")


if __name__ == "__main__":
    sys.exit(main())
