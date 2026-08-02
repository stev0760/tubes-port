#!/usr/bin/env python3
"""Count the wave-table dispatch arms in an unpacked TUBES image.

`1000:86b8`'s objective dispatch is a linear if-else chain, one arm per wave:

    3c NN        CMP AL, wave
    75 dd        JNZ  next arm
    55           PUSH BP           (the static link)
    e8 lo hi     CALL the objective routine
    e9 lo hi     JMP  the common tail      (11-byte arm)
      or
    eb dd        JMP  the common tail      (10-byte arm)

so they can be counted without disassembling anything: find the longest run
whose wave numbers ascend by one.

**The short JMP is the whole reason this needed a second attempt.** A first
version assumed a flat 11-byte stride and reported 62 arms for an image known
to have 75 - the arms nearest the tail are close enough to reach it with a
2-byte `EB`, so the run broke at the first one and the count looked plausible
enough to believe. `CLAUDE.md`: a negative - or a low - result is only as good
as the filter that produced it, and the tell was a number that did not match
the one thing already known.
"""
import sys


def arm_len(data, j):
    """Length of the arm at j, or 0 if there is not one there."""
    if j + 10 > len(data):
        return 0
    if data[j] != 0x3C or data[j + 2] != 0x75 or data[j + 4] != 0x55 \
       or data[j + 5] != 0xE8:
        return 0
    if data[j + 8] == 0xE9 and data[j + 3] == 7:
        return 11
    if data[j + 8] == 0xEB and data[j + 3] == 6:
        return 10
    # The LAST arm has no JMP at all: the common tail follows it directly, so
    # it is 8 bytes and its JNZ jumps 4 - straight past the call. Missing this
    # is what made a first count read 74 for a table of 75.
    if data[j + 3] == 4:
        return 8
    return 0


def arms(data):
    best = []
    i = 0
    while i < len(data) - 11:
        n = arm_len(data, i)
        if not n:
            i += 1
            continue
        run, j, want = [], i, data[i + 1]
        while True:
            n = arm_len(data, j)
            if not n or data[j + 1] != want:
                break
            run.append((j, want))
            j += n
            want += 1
        if len(run) > len(best):
            best = run
        i = j if len(run) > 1 else i + 1
    return best


for path in sys.argv[1:]:
    data = open(path, "rb").read()
    a = arms(data)
    name = path.rsplit("/", 1)[-1]
    if a:
        print(f"{name:22} {len(a):3d} arms, waves {a[0][1]}..{a[-1][1]}, "
              f"chain at file offset 0x{a[0][0]:x}")
    else:
        print(f"{name:22} no dispatch chain found")
