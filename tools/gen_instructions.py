#!/usr/bin/env python3
"""Emit src/instructions.cpp from the disassembly of `1b2e:2d63`.

152 strings and 22 sprite draws across 21 slides is data, not logic, and typing
it by hand would be 150 chances to make a transcription error nobody would ever
notice. So it is extracted: every text call is a fixed push sequence and every
illustration indexes the game's own ball table at `DS:0x1da6 + 4 * type`.

Run from the tooling checkout (see CLAUDE.md) with disasm-2d63.txt and strings-2d63.txt present.
"""
import argparse
import re
import sys

BALL_TABLE = 0x1DA6            # DS:0x1da6 + 4*type - see board.h
# The four text-unit entry points, as Ghidra prints far targets - on a
# normalised 0x2000 base, so these are flat addresses wearing a segment.
#
# THEY MOVE BETWEEN EDITIONS. The shareware image's interface unit is larger,
# which pushes every segment after it up by 0x12 paragraphs - 0x120 bytes - so
# the same four routines sit at `registered + 0x120` there. A uniform shift
# with no exceptions, which is what `--shift` applies rather than four separate
# lookups. See docs/reversing-notes.md, "The segment layout shifted".
WRITE_AT, WRITE_MID, DRAW = 0x36AB, 0x37EA, 0x3B15
SET_FONT = 0x3FAB
# `LoadSprite(name: string; var dest)`. The slideshow calls it twice at its head
# to load the two sprites it owns, and the DRAW calls later push those same two
# locals. Following the load is how a draw's sprite gets its NAME rather than a
# guess from its frame offset - see `SPRITE_SLOT`.
LOAD_SPRITE = 0x21FB
# The negatives `instructions.h` uses for the slideshow's own sprites, by the
# file the binary loads into the local a draw pushes. Anything not listed here
# is an atom type out of the ball table.
SPRITE_SLOT = {"TESTUBE1.CSP": -1, "TESTUBES.CSP": -2}
# Any interface-unit segment: `1b2e` registered, `1ac3` shareware.
INSN = re.compile(r"^[0-9a-f]{4}:([0-9a-f]{4})\s+(.*?)\s*$")


def load_disasm(path):
    out = []
    for line in open(path):
        m = INSN.match(line.rstrip("\n"))
        if m:
            out.append((int(m.group(1), 16), m.group(2)))
    return out


def load_strings(path):
    data = {}
    row = re.compile(r"[0-9a-f]{4}:([0-9a-f]{4})\s+((?:[0-9a-f]{2} )+)")
    for line in open(path):
        m = row.search(line)
        if not m:
            continue
        base = int(m.group(1), 16)
        for i, b in enumerate(m.group(2).split()):
            data[base + i] = int(b, 16)
    return data


def pascal(data, off):
    n = data.get(off, 0)
    return "".join(chr(data.get(off + 1 + i, 32)) for i in range(n))


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("disasm")
    ap.add_argument("strings")
    ap.add_argument("name", help="prefix for the per-page arrays, e.g. Credit")
    ap.add_argument("sep_insn", nargs="?",
                    default="CMP byte ptr [BP + -0x3],0x2",
                    help="the instruction that separates one page from the next")
    ap.add_argument("--shift", type=lambda v: int(v, 0), default=0,
                    help="added to the four text-routine addresses; 0x120 for "
                         "the shareware image, whose segments sit that much higher")
    ap.add_argument("--array", default="kInstructionSlides",
                    help="name of the emitted page table")
    ap.add_argument("--count", default="kInstructionSlideCount",
                    help="name of the constant giving its length")
    ap.add_argument("--header", default="", help="path to a file whose contents "
                    "become the generated file's leading comment")
    ap.add_argument("--include", default="instructions.h")
    args = ap.parse_args()

    ins = load_disasm(args.disasm)
    strs = load_strings(args.strings)
    name = args.name
    sep_insn = args.sep_insn
    write_at = f"0x2000:{WRITE_AT + args.shift:04x}"
    write_mid = f"0x2000:{WRITE_MID + args.shift:04x}"
    draw = f"0x2000:{DRAW + args.shift:04x}"
    set_font = f"0x2000:{SET_FONT + args.shift:04x}"
    load_sprite = f"0x2000:{LOAD_SPRITE + args.shift:04x}"
    seps = [a for a, t in ins if t == sep_insn]

    # Which sprite each frame local holds, read off the loads at the head of the
    # routine rather than assumed from the offset. The two editions use the same
    # two offsets, but nothing in the binary promises that and an earlier version
    # of this script hardcoded the pair - backwards, as it turned out, so the
    # slideshow drew the tube under its own interior shading. Deriving it costs
    # one pass and cannot be wrong in the same way.
    #
    #     MOV DI,<name>  PUSH CS  PUSH DI          { the filename    }
    #     LEA DI,[BP + -0xX]  PUSH SS  PUSH DI     { var dest        }
    #     CALLF <load_sprite>
    locals_ = {}
    for i, (addr, text) in enumerate(ins):
        if not text.startswith("CALLF") or text.split()[-1] != load_sprite:
            continue
        dest, stroff = None, None
        for j in range(i - 1, max(i - 12, -1), -1):
            t = ins[j][1]
            if t.startswith("LEA DI,[BP + ") and dest is None:
                dest = int(t.split("[BP + ")[1].split("]")[0], 16)
            elif t.startswith("MOV DI,0x"):
                stroff = int(t.split(",")[1], 16)
                break
        if dest is not None and stroff is not None:
            locals_[dest] = pascal(strs, stroff)

    events = []          # (addr, kind, payload)
    font = 0             # 0 = TINY6X8 (advance 6), 1 = the heading font
    for i, (addr, text) in enumerate(ins):
        if not text.startswith("CALLF"):
            continue
        target = text.split()[-1]
        if target == set_font:
            # `2000:3fab(advance, height, peak, ...)`. Advance 6 is TINY6X8 and
            # 8 is the heading font; the credits alternate between them.
            imm = []
            j = i - 1
            while j >= 0 and len(imm) < 4:
                a, t = ins[j]
                if t.startswith("PUSH 0x"):
                    imm.append(int(t.split()[1], 16))
                elif t.startswith("PUSH word"):
                    pass
                else:
                    break
                j -= 1
            # imm is in reverse push order, so [0] is the peak:
            # 4 for TINY6X8 and 8 for the heading font.
            font = 1 if (imm and imm[0] == 8) else 0
            continue
        if target not in (write_at, write_mid, draw):
            continue
        imm, slot, stroff = [], None, None
        j = i - 1
        while j >= 0:
            a, t = ins[j]
            if t.startswith("PUSH 0x") or t.startswith("PUSH -"):
                imm.append(int(t.split()[1], 16))
            elif t.startswith("PUSH word ptr [0x"):
                slot = int(t.split("[")[1].split("]")[0], 16)
            elif t.startswith("MOV DI,0x"):
                stroff = int(t.split(",")[1], 16)
            elif t.startswith(("CALLF", "CALL ", "PUSH word ptr [BP")):
                if t.startswith("PUSH word ptr [BP"):
                    # A local sprite, pushed as a far pointer: the segment half
                    # first, then the offset half at the variable's own base.
                    # Scanning backwards the base is what arrives FIRST, so keep
                    # it and let the segment push fall through - the reverse is
                    # what made every draw resolve to the same sprite.
                    if slot is None:
                        off = int(t.split("[BP + ")[1].split("]")[0], 16)
                        what = locals_.get(off)
                        if what not in SPRITE_SLOT:
                            sys.exit(f"{addr:04x}: draw pushes [BP{off:+#x}], "
                                     f"which holds {what!r} - add it to "
                                     f"SPRITE_SLOT and to instructions.h")
                        slot = SPRITE_SLOT[what]
                    j -= 1
                    continue
                break
            j -= 1
        imm.reverse()
        if target == draw:
            events.append((addr, "draw", (imm, slot), font))
        else:
            events.append((addr, "mid" if target == write_mid else "at",
                           (imm, pascal(strs, stroff)), font))

    slides = {}
    for addr, kind, payload, font in events:
        # The two navigation lines are drawn once, before the first page, but
        # they are on screen for all of them - the original never clears them.
        # They live in the header as `kInstructionNav` instead.
        if kind == "mid" and payload[0][2] >= 180:
            continue
        n = sum(1 for s in seps if s < addr)
        slides.setdefault(n, []).append((kind, payload, font))

    out = []
    w = out.append
    if args.header:
        w(open(args.header).read().rstrip("\n"))
    else:
        w("// GENERATED by tools/gen_instructions.py - do not edit by hand.")
        w("// Text and layout are data in the binary, extracted rather than")
        w("// transcribed so no line of the game's own words can be mistyped.")
    w(f'#include "{args.include}"')
    w("")
    w("namespace tubes {")
    w("namespace {")
    for n in sorted(slides):
        w(f"const InstructionItem k{name}{n}[] = {{")
        for kind, (imm, payload), font in slides[n]:
            if kind == "draw":
                x, y = imm[0], imm[1]
                if payload in (-1, -2):
                    typ = payload      # kTestTube1 / kTestTubeS
                elif payload is None:
                    typ = 0
                else:
                    typ = (payload - BALL_TABLE) // 4
                w(f"    {{InstructionItem::kAtom, {x}, {y}, {typ}, 0, 0, "
                  f"nullptr}},")
            elif kind == "at":
                x, y, colour, mode = imm[0], imm[1], imm[2] & 0xFF, imm[3]
                w(f"    {{InstructionItem::kText, {x}, {y}, {colour}, {mode}, "
                  f"{font}, {cstr(payload)}}},")
            else:
                x0, x1, y, colour, mode = imm[0], imm[1], imm[2], \
                    imm[3] & 0xFF, imm[4]
                w(f"    {{InstructionItem::kCentred, {x0}, {y}, {colour}, "
                  f"{mode}, {font}, {cstr(payload)}}},   // to x {x1}")
        w("};")
    w("}  // namespace")
    w("")
    w(f"const InstructionSlide {args.array}[{args.count}] = {{")
    for n in sorted(slides):
        w(f"    {{k{name}{n}, sizeof(k{name}{n}) / sizeof(k{name}{n}[0])}},")
    w("};")
    w("")
    w("}  // namespace tubes")
    print("\n".join(out))
    print(f"{len(slides)} slides, {len(events)} items", file=sys.stderr)


if __name__ == "__main__":
    main()
