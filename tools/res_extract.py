#!/usr/bin/env python3
"""
Read an "Absolute Magic Resource File!" container (TUBES.RES, DRIVERS.RES).

Format recovered by decompiling the loader at 2407:0146.

  header (39 bytes)
    [0..31]   signature "Absolute Magic Resource File!\\r\\n\\x1a"
    [32]      format version (1)
    [33..34]  entry count            u16
    [35..38]  directory file offset  u32

  directory: `count` entries of 26 bytes each, located at the directory
  offset and running to end of file
    [0]       name length (Pascal ShortString)
    [1..12]   name, NUL-padded to 12 bytes
    [13]      type / flags
    [14..17]  uncompressed size      u32
    [18..21]  stored size            u32
    [22..25]  file offset            u32

Stored size is smaller than uncompressed size, so payloads are compressed.
The codec is not implemented here; --extract writes the raw stored bytes.

Usage:
  res_extract.py LIST   TUBES.RES
  res_extract.py VERIFY TUBES.RES
  res_extract.py EXTRACT TUBES.RES OUTDIR
"""

import os
import struct
import sys

SIG = b"Absolute Magic Resource File!\r\n\x1a"
HDR_LEN = 39
ENT_LEN = 26


# LZSS parameters, read straight out of the decompressor at 2475:115c/10dc.
# This is Okumura's classic LZSS: 4096-byte ring buffer pre-filled with
# spaces, 12-bit offset, 4-bit length biased by THRESHOLD+1.
LZSS_N = 4096          # ring buffer size          (offset masked with 0xfff)
LZSS_F = 18            # longest match             (0xf + 3)
LZSS_THRESHOLD = 2     # shortest encoded match is THRESHOLD + 1 = 3
LZSS_FILL = 0x20       # ring pre-fill byte        (fill of 0x1011 with 0x20)


def lzss_decompress(src, expected=None):
    """Decode one LZSS payload. Flag bit set = literal, clear = match."""
    ring = bytearray([LZSS_FILL]) * LZSS_N
    r = LZSS_N - LZSS_F          # 0xfee
    out = bytearray()
    pos = 0
    flags = 0

    while True:
        flags >>= 1
        if (flags & 0x100) == 0:
            if pos >= len(src):
                break
            flags = src[pos] | 0xFF00
            pos += 1

        if flags & 1:
            if pos >= len(src):
                break
            c = src[pos]
            pos += 1
            out.append(c)
            ring[r] = c
            r = (r + 1) & (LZSS_N - 1)
        else:
            if pos + 1 >= len(src):
                break
            lo = src[pos]
            hi = src[pos + 1]
            pos += 2
            offset = lo | ((hi & 0xF0) << 4)
            length = (hi & 0x0F) + LZSS_THRESHOLD + 1
            for k in range(length):
                c = ring[(offset + k) & (LZSS_N - 1)]
                out.append(c)
                ring[r] = c
                r = (r + 1) & (LZSS_N - 1)

        # The encoder can pad the final flag group; stop at the known length.
        if expected is not None and len(out) >= expected:
            break

    return bytes(out)


class Entry:
    __slots__ = ("name", "flags", "usize", "ssize", "offset")

    def __repr__(self):
        ratio = (100.0 * self.ssize / self.usize) if self.usize else 0.0
        return (f"{self.name:<13} flags={self.flags:#04x} "
                f"off={self.offset:<8} stored={self.ssize:<8} "
                f"raw={self.usize:<8} ({ratio:5.1f}%)")


def parse(path):
    data = open(path, "rb").read()
    if data[:len(SIG)] != SIG:
        raise SystemExit(f"{path}: bad signature")

    version = data[32]
    count = struct.unpack_from("<H", data, 33)[0]
    dir_off = struct.unpack_from("<I", data, 35)[0]
    if version != 1:
        raise SystemExit(f"{path}: unsupported version {version}")

    entries = []
    for i in range(count):
        base = dir_off + i * ENT_LEN
        raw = data[base:base + ENT_LEN]
        if len(raw) < ENT_LEN:
            raise SystemExit(f"{path}: directory truncated at entry {i}")
        e = Entry()
        n = raw[0]
        e.name = raw[1:1 + n].decode("ascii", "replace")
        e.flags = raw[13]
        e.usize, e.ssize, e.offset = struct.unpack_from("<III", raw, 14)
        entries.append(e)

    return data, version, dir_off, entries


def verify(path):
    data, version, dir_off, entries = parse(path)
    print(f"{path}: version {version}, {len(entries)} entries, "
          f"directory at {dir_off}, size {len(data)}")

    problems = 0
    if dir_off + len(entries) * ENT_LEN != len(data):
        print("  ! directory does not end exactly at EOF")
        problems += 1

    ordered = sorted(entries, key=lambda e: e.offset)
    cursor = HDR_LEN
    gaps = 0
    for e in ordered:
        if e.offset + e.ssize > dir_off:
            print(f"  ! {e.name}: payload overruns directory")
            problems += 1
        if e.offset != cursor:
            gaps += 1
        cursor = e.offset + e.ssize

    print(f"  first payload at {ordered[0].offset} "
          f"(header is {HDR_LEN} bytes)")
    print(f"  payload region ends at {cursor}, directory starts at {dir_off}")
    print(f"  non-contiguous boundaries: {gaps}")

    flags = {}
    for e in entries:
        flags[e.flags] = flags.get(e.flags, 0) + 1
    print(f"  flag byte values: {flags}")

    exts = {}
    for e in entries:
        ext = e.name.rsplit(".", 1)[-1].upper() if "." in e.name else "(none)"
        exts[ext] = exts.get(ext, 0) + 1
    print(f"  extensions: {dict(sorted(exts.items(), key=lambda kv: -kv[1]))}")

    total_stored = sum(e.ssize for e in entries)
    total_raw = sum(e.usize for e in entries)
    print(f"  stored {total_stored} -> raw {total_raw} "
          f"({100.0 * total_stored / total_raw:.1f}%)")
    print(f"  problems: {problems}")
    return problems


def main():
    if len(sys.argv) < 3:
        print(__doc__.strip())
        return 2
    cmd, path = sys.argv[1].upper(), sys.argv[2]

    if cmd == "VERIFY":
        return 1 if verify(path) else 0

    if cmd == "LIST":
        _, _, _, entries = parse(path)
        for e in sorted(entries, key=lambda x: x.offset):
            print(e)
        return 0

    if cmd == "EXTRACT":
        if len(sys.argv) < 4:
            print("EXTRACT needs an output directory")
            return 2
        outdir = sys.argv[3]
        data, _, _, entries = parse(path)
        os.makedirs(outdir, exist_ok=True)
        bad = 0
        for e in entries:
            blob = data[e.offset:e.offset + e.ssize]
            if e.flags == 1:
                blob = lzss_decompress(blob, e.usize)
            if len(blob) != e.usize:
                print(f"  ! {e.name}: expected {e.usize} bytes, got {len(blob)}")
                bad += 1
            with open(os.path.join(outdir, e.name), "wb") as f:
                f.write(blob)
        print(f"wrote {len(entries)} payloads to {outdir} ({bad} size mismatches)")
        return 1 if bad else 0

    print(f"unknown command {cmd}")
    return 2


if __name__ == "__main__":
    sys.exit(main())
