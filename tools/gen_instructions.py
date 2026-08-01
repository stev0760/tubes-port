#!/usr/bin/env python3
"""Emit src/instructions.cpp from the disassembly of `1b2e:2d63`.

152 strings and 22 sprite draws across 21 slides is data, not logic, and typing
it by hand would be 150 chances to make a transcription error nobody would ever
notice. So it is extracted: every text call is a fixed push sequence and every
illustration indexes the game's own ball table at `DS:0x1da6 + 4 * type`.

Run from ~/Dev/tubes-tooling with disasm-2d63.txt and strings-2d63.txt present.
"""
import re
import sys

BALL_TABLE = 0x1DA6            # DS:0x1da6 + 4*type - see board.h
WRITE_AT, WRITE_MID, DRAW = "0x2000:36ab", "0x2000:37ea", "0x2000:3b15"
INSN = re.compile(r"^1b2e:([0-9a-f]{4})\s+(.*?)\s*$")


def load_disasm(path):
    out = []
    for line in open(path):
        m = INSN.match(line.rstrip("\n"))
        if m:
            out.append((int(m.group(1), 16), m.group(2)))
    return out


def load_strings(path):
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
    n = data.get(off, 0)
    return "".join(chr(data.get(off + 1 + i, 32)) for i in range(n))


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    ins = load_disasm("disasm-2d63.txt")
    strs = load_strings("strings-2d63.txt")
    seps = [a for a, t in ins if t == "CMP byte ptr [BP + -0x3],0x2"]

    events = []          # (addr, kind, payload)
    for i, (addr, text) in enumerate(ins):
        if not text.startswith("CALLF"):
            continue
        target = text.split()[-1]
        if target not in (WRITE_AT, WRITE_MID, DRAW):
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
                    # A local sprite. 1b2e:1f64 "TESTUBE1.CSP" loads into
                    # [BP-0x10] and 1b2e:1f71 "TESTUBES.CSP" into [BP-0x8], so
                    # the frame offset says which.
                    off = t.split("[BP + ")[1].split("]")[0]
                    slot = -1 if off == "-0x10" else -2
                    j -= 1
                    continue
                break
            j -= 1
        imm.reverse()
        if target == DRAW:
            events.append((addr, "draw", (imm, slot)))
        else:
            events.append((addr, "mid" if target == WRITE_MID else "at",
                           (imm, pascal(strs, stroff))))

    slides = {}
    for addr, kind, payload in events:
        n = sum(1 for s in seps if s < addr)
        slides.setdefault(n, []).append((kind, payload))

    out = []
    w = out.append
    w("// GENERATED from `1b2e:2d63` by tools/gen_instructions.py - do not edit")
    w("// by hand. 21 slides, 152 strings and 22 illustrations: data, not")
    w("// logic, and extracted rather than transcribed so that no line of the")
    w("// game's own documentation can be quietly mistyped.")
    w("//")
    w("// The illustrations index the ball table at `DS:0x1da6 + 4 * type`, so")
    w("// they are atom TYPES and the port already has every sprite.")
    w('#include "instructions.h"')
    w("")
    w("namespace tubes {")
    w("namespace {")
    for n in sorted(slides):
        w(f"const InstructionItem kSlide{n}[] = {{")
        for kind, (imm, payload) in slides[n]:
            if kind == "draw":
                x, y = imm[0], imm[1]
                if payload in (-1, -2):
                    typ = payload      # kTestTube1 / kTestTubeS
                elif payload is None:
                    typ = 0
                else:
                    typ = (payload - BALL_TABLE) // 4
                w(f"    {{InstructionItem::kAtom, {x}, {y}, {typ}, 0, "
                  f"nullptr}},")
            elif kind == "at":
                x, y, colour, mode = imm[0], imm[1], imm[2] & 0xFF, imm[3]
                w(f"    {{InstructionItem::kText, {x}, {y}, {colour}, {mode}, "
                  f"{cstr(payload)}}},")
            else:
                x0, x1, y, colour, mode = imm[0], imm[1], imm[2], \
                    imm[3] & 0xFF, imm[4]
                w(f"    {{InstructionItem::kCentred, {x0}, {y}, {colour}, "
                  f"{mode}, {cstr(payload)}}},   // to x {x1}")
        w("};")
    w("}  // namespace")
    w("")
    w("const InstructionSlide kInstructionSlides[kInstructionSlideCount] = {")
    for n in sorted(slides):
        w(f"    {{kSlide{n}, sizeof(kSlide{n}) / sizeof(kSlide{n}[0])}},")
    w("};")
    w("")
    w("}  // namespace tubes")
    print("\n".join(out))
    print(f"{len(slides)} slides, {len(events)} items", file=sys.stderr)


if __name__ == "__main__":
    main()
