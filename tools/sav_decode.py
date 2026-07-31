#!/usr/bin/env python3
"""Decode `TUBES.SAV`, the saved-game file.

Not a guess fitted to two samples: every field below is a store in
`1000:2dd0`'s save arm, which fills a scratch record at `DGROUP:0x1ce8` from
the session frame one field at a time and then `Move`s it into the chosen slot.
The file itself is `1b2e:00ac`:

    Assign(f, 'TUBES.SAV');  Rewrite(f, 1)
    BlockWrite(f, DGROUP:0x1928, $1e0)      { bank 0, Endurance }
    BlockWrite(f, DGROUP:0x1b08, $1e0)      { bank 1, Wave }
    Close(f)

and the reader `1b2e:000a` FillChars both banks with zero before reading, so a
missing file leaves every slot with a zero length byte - which is exactly the
"empty" test the menu makes.

Usage: sav_decode.py TUBES.SAV
"""
from __future__ import annotations

import struct
import sys

BANK_BYTES = 0x1E0
SLOT_BYTES = 0x50
SLOTS_SHOWN = 5          # a bank holds six records; the sixth is never a save
BANKS = ("Endurance", "Wave")

# (name, offset, width) - the order the save arm writes them, which is not the
# order they sit in the record.
FIELDS = [
    ("score",             0x1F, 4),   # -0x153/-0x151, one u32 in two stores
    ("continues left",    0x23, 1),   # -0x14f
    ("total chains",      0x24, 2),   # -0x14e
    ("wave",              0x26, 1),   # -0x170
    ("drops remaining",   0x27, 1),   # -0x17e
    ("chains this wave",  0x28, 1),   # -0x17c
    ("velocity",          0x29, 2),   # -0x180
    ("interval",          0x2B, 1),   # -0x181
    ("chain target",      0x2C, 1),   # -0x183
    ("atom target",       0x2D, 1),   # -0x182
    ("colour target",     0x2E, 1),   # -0x184
    ("crystals",          0x2F, 1),   # -0x185
    ("marked",            0x30, 1),   # -0x186
    ("pre-fill",          0x31, 1),   # -0x17b
]

# `1b2e:00ac` writes Random(254)+1 into DS:0x1ae3 and DS:0x1cc3 on every save.
# Those are bank + 0x1bb = record 5 (0-based), field +0x2b - the sixth record's
# interval byte. The reader loads it into a local and never looks at it again,
# so it is a nonce with no consumer, NOT a checksum.
NONCE_OFF = 0x1BB


def field(rec: bytes, off: int, width: int) -> int:
    if width == 1:
        return rec[off]
    if width == 2:
        return struct.unpack_from("<H", rec, off)[0]
    return struct.unpack_from("<I", rec, off)[0]


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    raw = open(sys.argv[1], "rb").read()
    if len(raw) != BANK_BYTES * 2:
        print(f"expected {BANK_BYTES * 2} bytes, got {len(raw)}")
        return 1

    for b, label in enumerate(BANKS):
        base = b * BANK_BYTES
        print(f"=== bank {b}: {label} ===")
        print(f"    nonce at +0x{NONCE_OFF:03x}: "
              f"{raw[base + NONCE_OFF]:#04x}")
        for s in range(SLOTS_SHOWN):
            rec = raw[base + s * SLOT_BYTES: base + (s + 1) * SLOT_BYTES]
            n = rec[0]
            if n == 0:
                print(f"  slot {s + 1}: ( Available )")
                continue
            desc = rec[1:1 + n].decode("cp437", "replace")
            vals = "  ".join(f"{nm} {field(rec, o, w)}" for nm, o, w in FIELDS)
            print(f"  slot {s + 1}: {desc!r}")
            print(f"           {vals}")
            tail = rec[0x32:].rstrip(b"\0")
            if tail:
                print(f"           tail past +0x31: {tail.hex()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
