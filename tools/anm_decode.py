#!/usr/bin/env python3
"""
Decode Tubes .ANM animation resources.

There is exactly one, `SOFT.ANM`, and it belongs to the Software Creations
splash. Like a .CSP it is not data in any ordinary sense - it is generated
16-bit x86 code, and the player at `21d5:0000` FAR CALLS it:

    N       := ANM^[0]                      { word: frame count }
    table   := @ANM^[2]                     { N dwords: byte size per frame }
    frame   := table + N * 4                { the first blob }
    for i := 0 to N - 1 do begin
      size := table[i]                      { only the low word is read }
      WaitRetrace; WaitRetrace; WaitRetrace { 23e7:0016, three times }
      ES := $A000; DI := 0
      CALL FAR frame                        { DS:SI = frame, ES:DI = screen }
      frame := frame + size
    end

So a frame is a subroutine that paints itself into the mode 13h framebuffer -
`23df:0000` sets mode 13h before the splashes, which is why DI is a plain
linear `y * 320 + x` here and not a Mode X plane offset.

Two consequences worth stating, because they shape the port:

  * frames are DELTAS. Each one only touches the pixels that changed, so they
    must be replayed onto a persistent canvas in order.
  * SI enters pointing at the frame's own first byte. A frame starts by
    adding its own code length to SI, which lands SI on the literal pixel
    data stored after the code - the `rep movsw` runs then copy from it.

Grammar (the whole of it, verified against all 23 frames):

    33 c9                    xor cx,cx          { ch stays 0 throughout }
    b1 ii                    mov cl,imm8        { run length }
    b9 iiii                  mov cx,imm16
    b0 ii                    mov al,imm8        { the fill colour }
    b8 iiii                  mov ax,imm16       { the fill colour, twice }
    81 c6 iiii               add si,imm16
    81 ee iiii               sub si,imm16
    81 c7 iiii               add di,imm16
    81 ef iiii               sub di,imm16
    8b df                    mov bx,di          { save the row base }
    8b fb                    mov di,bx          { and restore it }
    f3 ab                    rep stosw          { cx words of ax }
    f3 aa                    rep stosb          { cx bytes of al }
    f3 a5                    rep movsw          { cx words from ds:si }
    f3 a4                    rep movsb
    ab                       stosw
    aa                       stosb
    a5                       movsw
    a4                       movsb
    cb                       retf

Usage:
  anm_decode.py INFO   <file.ANM>
  anm_decode.py OPS    <file.ANM>            # opcode histogram, all frames
  anm_decode.py RUNS   <file.ANM>            # per-frame runs + hash
  anm_decode.py RENDER <palette.PAL> <outdir> <file.ANM> [under.GFX]

The frames are deltas, so pass `SOFT.GFX` as the base: the splash draws the
still image first and the animation only paints what moves over it.
"""

import os
import struct
import sys

SCREEN_W = 320
SCREEN_H = 200
SCREEN_BYTES = SCREEN_W * SCREEN_H


class ParseError(Exception):
    pass


def frames(blob):
    """Split the container into its per-frame code blobs."""
    if len(blob) < 2:
        raise ParseError("too short for a frame count")
    n = struct.unpack_from("<H", blob, 0)[0]
    table_at = 2
    data_at = table_at + n * 4
    if data_at > len(blob):
        raise ParseError("frame table runs past the file")
    out = []
    off = data_at
    for i in range(n):
        size, hi = struct.unpack_from("<HH", blob, table_at + i * 4)
        if hi != 0:
            raise ParseError("frame %d has a high word of %d" % (i, hi))
        if off + size > len(blob):
            raise ParseError("frame %d runs past the file" % i)
        out.append((off, blob[off:off + size]))
        off += size
    if off != len(blob):
        raise ParseError("frames end at %d, file is %d" % (off, len(blob)))
    return out


def apply_runs(canvas, runs):
    """Paint a frame's runs onto a 320x200 canvas, clipping at its edges."""
    for off, data in runs:
        for i, v in enumerate(data):
            d = off + i
            if 0 <= d < SCREEN_BYTES:
                canvas[d] = v


def frame_hash(runs):
    """FNV-1a over (offset, bytes) - the oracle the C++ decoder is diffed
    against, since the two must agree run for run and not merely picture for
    picture."""
    h = 0x811c9dc5
    for off, data in runs:
        for b in (off & 0xff, (off >> 8) & 0xff, len(data) & 0xff,
                  (len(data) >> 8) & 0xff):
            h = ((h ^ b) * 0x01000193) & 0xffffffff
        for b in data:
            h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


def run_frame(code, base, ops=None):
    """Interpret one frame's blob and return its runs as [(offset, bytes)].
    SI starts at the blob itself, because the literal pixels it copies from
    live inside it, after the RETF."""
    ip = 0
    si = 0                       # relative to the blob's own first byte
    di = 0                       # linear offset into the framebuffer
    ax = 0
    cx = 0
    bx = 0

    def bump(name):
        if ops is not None:
            ops[name] = ops.get(name, 0) + 1

    def imm16():
        nonlocal ip
        v = struct.unpack_from("<H", code, ip)[0]
        ip += 2
        return v

    runs = []
    open_at = [-1]
    open_run = bytearray()

    def store_byte(v):
        nonlocal di
        if open_at[0] < 0 or di != open_at[0] + len(open_run):
            if open_at[0] >= 0 and open_run:
                runs.append((open_at[0], bytes(open_run)))
            open_run.clear()
            open_at[0] = di
        open_run.append(v)
        di = (di + 1) & 0xffff

    def close():
        if open_at[0] >= 0 and open_run:
            runs.append((open_at[0], bytes(open_run)))
        return runs

    def load_byte():
        nonlocal si
        v = code[si] if 0 <= si < len(code) else 0
        si = (si + 1) & 0xffff
        return v

    while ip < len(code):
        op = code[ip]
        ip += 1

        if op == 0xcb:                                  # retf
            bump("retf")
            # The bytes after the retf are not code: they are the literal
            # pixels the frame's `movs` runs copy from, which is what the
            # opening `add si,<code length>` skips SI over.
            return close()
        if op == 0x33 and code[ip] == 0xc9:             # xor cx,cx
            ip += 1
            cx = 0
            bump("xor cx,cx")
        elif op == 0xb1:                                # mov cl,imm8
            cx = (cx & 0xff00) | code[ip]
            ip += 1
            bump("mov cl,imm8")
        elif op == 0xb9:                                # mov cx,imm16
            cx = imm16()
            bump("mov cx,imm16")
        elif op == 0xb0:                                # mov al,imm8
            ax = (ax & 0xff00) | code[ip]
            ip += 1
            bump("mov al,imm8")
        elif op == 0xb8:                                # mov ax,imm16
            ax = imm16()
            bump("mov ax,imm16")
        elif op == 0x8b and code[ip] == 0xdf:           # mov bx,di
            ip += 1
            bx = di
            bump("mov bx,di")
        elif op == 0x8b and code[ip] == 0xfb:           # mov di,bx
            ip += 1
            di = bx
            bump("mov di,bx")
        elif op == 0x81:
            modrm = code[ip]
            ip += 1
            v = imm16()
            if modrm == 0xc6:
                si = (si + v) & 0xffff
                bump("add si,imm16")
            elif modrm == 0xee:
                si = (si - v) & 0xffff
                bump("sub si,imm16")
            elif modrm == 0xc7:
                di = (di + v) & 0xffff
                bump("add di,imm16")
            elif modrm == 0xef:
                di = (di - v) & 0xffff
                bump("sub di,imm16")
            elif modrm == 0xc3:
                bx = (bx + v) & 0xffff
                bump("add bx,imm16")
            else:
                raise ParseError("81 /%02x at %d" % (modrm, ip - 3))
        elif op == 0xf3:                                # rep <string op>
            sop = code[ip]
            ip += 1
            n = cx
            if sop == 0xab:
                bump("rep stosw")
                for _ in range(n):
                    store_byte(ax & 0xff)
                    store_byte(ax >> 8)
            elif sop == 0xaa:
                bump("rep stosb")
                for _ in range(n):
                    store_byte(ax & 0xff)
            elif sop == 0xa5:
                bump("rep movsw")
                for _ in range(n):
                    store_byte(load_byte())
                    store_byte(load_byte())
            elif sop == 0xa4:
                bump("rep movsb")
                for _ in range(n):
                    store_byte(load_byte())
            else:
                raise ParseError("f3 %02x at %d" % (sop, ip - 2))
            cx = 0
        elif op == 0xab:
            bump("stosw")
            store_byte(ax & 0xff)
            store_byte(ax >> 8)
        elif op == 0xaa:
            bump("stosb")
            store_byte(ax & 0xff)
        elif op == 0xa5:
            bump("movsw")
            store_byte(load_byte())
            store_byte(load_byte())
        elif op == 0xa4:
            bump("movsb")
            store_byte(load_byte())
        else:
            raise ParseError("unknown opcode %02x at %d in the blob at %d"
                             % (op, ip - 1, base))
    raise ParseError("ran off the end of the blob at %d without a retf" % base)


def load_palette(path):
    raw = open(path, "rb").read()
    if len(raw) != 768:
        raise ParseError("%s is %d bytes, not 768" % (path, len(raw)))
    return [(raw[i * 3] << 2, raw[i * 3 + 1] << 2, raw[i * 3 + 2] << 2)
            for i in range(256)]


def write_png(path, canvas, pal):
    import zlib
    rows = bytearray()
    for y in range(SCREEN_H):
        rows.append(0)
        for x in range(SCREEN_W):
            r, g, b = pal[canvas[y * SCREEN_W + x]]
            rows += bytes((r, g, b))

    def chunk(tag, data):
        c = tag + data
        return (struct.pack(">I", len(data)) + c +
                struct.pack(">I", zlib.crc32(c) & 0xffffffff))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", SCREEN_W, SCREEN_H,
                                      8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1].upper()
    if cmd == "INFO":
        blob = open(argv[2], "rb").read()
        fs = frames(blob)
        print("%s: %d frames, %d bytes" % (argv[2], len(fs), len(blob)))
        for i, (off, code) in enumerate(fs):
            print("  frame %2d  at 0x%05x  %5d bytes" % (i, off, len(code)))
        return 0
    if cmd == "OPS":
        blob = open(argv[2], "rb").read()
        ops = {}
        for off, code in frames(blob):
            run_frame(code, off, ops)
        for k in sorted(ops, key=lambda k: -ops[k]):
            print("%8d  %s" % (ops[k], k))
        return 0
    if cmd == "RUNS":
        blob = open(argv[2], "rb").read()
        for i, (off, code) in enumerate(frames(blob)):
            runs = run_frame(code, off)
            n = sum(len(d) for _, d in runs)
            print("frame %2d  %4d runs  %6d bytes  %08x"
                  % (i, len(runs), n, frame_hash(runs)))
        return 0
    if cmd == "RENDER":
        pal = load_palette(argv[2])
        outdir = argv[3]
        blob = open(argv[4], "rb").read()
        os.makedirs(outdir, exist_ok=True)
        canvas = bytearray(SCREEN_BYTES)
        if len(argv) > 5:
            under = open(argv[5], "rb").read()
            canvas[:] = under[-SCREEN_BYTES:]
        write_png(os.path.join(outdir, "frame--.png"), canvas, pal)
        for i, (off, code) in enumerate(frames(blob)):
            apply_runs(canvas, run_frame(code, off))
            write_png(os.path.join(outdir, "frame%02d.png" % i), canvas, pal)
        print("wrote %d frames to %s" % (len(frames(blob)), outdir))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
