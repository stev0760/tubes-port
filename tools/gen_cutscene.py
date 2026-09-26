#!/usr/bin/env python3
"""Emit src/cutscene.cpp from the disassembly of `1b2e:1651`.

The opening cutscene is five pages of the game's own story - Dr. Lanny B.
Brilliant, the eight elements, the accident - drawn as 21 text calls and 8
atom draws, over two animation tracks whose frame lists are 26 and 17 slots
of pointers built with duplicates.

None of that is logic and all of it is a fixed instruction pattern, so it is
EXTRACTED rather than typed. `tools/gen_instructions.py` does the same job for
the Instructions and the Credits; this is the third screen through the same
method, which is what `CLAUDE.md` asks for.

What it reads, and how:

  * `MOV DI,<off>; PUSH CS; PUSH DI; ... CALLF 0x2000:36ab` is a text call,
    with the four immediates before it as x, y, colour, mode;
  * `... CALLF 0x2000:3b15` is an atom draw indexing `DS:0x1da6 + 4 * type`;
  * `MOV DI,<name>; PUSH CS; PUSH DI; LEA DI,[BP+d]` ahead of a call loads a
    resource into the frame slot at `d`, and a four-instruction
    `MOV AX/DX ... MOV/MOV` copies one slot to another - which is how 10 files
    fill 26 slots. The .GFX and .SFX loaders are different entry points, so
    the test is the name, not the target;
  * `CALL 0x1000:c226` is the animation, and its 23 pushed words are the two
    tracks (see cutscene.h for the parameter map).

Run from the tooling checkout (see CLAUDE.md) with disasm-1651.txt and strings-1651.txt
present. See docs/reversing-notes.md, "The opening cutscene".
"""
import re
import sys

BALL_TABLE = 0x1DA6
WRITE_AT = "0x2000:36ab"        # 2321:049b, Write(x, y, colour, mode, s)
DRAW = "0x2000:3b15"            # 2321:0905, the masked sprite draw
ANIMATE = "0x1000:c226"         # 1b2e:0f46, the two-track player
BLANK = "0x1000:c468"           # 1b2e:1188, blank a rect on BOTH pages
FILLBAR = "0x2000:3cd0"         # 2321:0ac0, clear a band on the active page

INSN = re.compile(r"^1b2e:([0-9a-f]{4})\s+(.*?)\s*$")

# `2321:0ac0`, read off its listing by the same push-walking method (see the
# reversing notes): dx, dy, dw, dh, useW, useH, colour. `useW` 0 means the
# width is a literal 1 rather than the panel's.
PANEL = [
    (0, 0, 0, 0, 0, 0, 1, 1, 7),      # the body
    (0, 0, 0, 0, 0, 0, 1, 0, 15),     # top edge
    (0, 0, 0, 0, 0, 0, 0, 1, 15),     # left edge
    (-3, 2, 0, -4, 1, 0, 0, 1, 15),   # x + w - 3
    (3, -3, -5, 0, 0, 1, 1, 0, 15),   # y + h - 3
    (2, 2, -4, 0, 0, 0, 1, 0, 8),
    (-1, 1, 0, -1, 1, 0, 0, 1, 8),    # x + w - 1
    (2, 2, 0, -4, 0, 0, 0, 1, 8),
    (1, -1, -1, 0, 0, 1, 1, 0, 8),    # y + h - 1
]


def load_disasm(path):
    out = []
    for line in open(path):
        m = INSN.match(line.rstrip("\n").replace(" (GhidraScript)  ", ""))
        if m:
            out.append((int(m.group(1), 16), m.group(2)))
    return out


def load_bytes(path):
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


def disp(text):
    """The signed BP displacement in an operand, or None."""
    m = re.search(r"\[BP \+ (-?0x[0-9a-f]+)\]", text)
    if not m:
        return None
    v = int(m.group(1), 16)
    return v - 0x10000 if v > 0x7FFF else v


def imm(text):
    m = re.fullmatch(r"(-?0x[0-9a-f]+)", text)
    if not m:
        return None
    v = int(m.group(1), 16)
    return v - 0x10000 if v > 0x7FFF else v


def slot_map(ins, data):
    """Reconstruct the two frame lists. 10 WRITE files and 16 EXPLOD files
    fill 26 and 17 slots, because the caller copies pointers - which is how a
    six-frame pose can be held for seven ticks without a delay parameter."""
    slots, i = {}, 0
    while i < len(ins):
        _, t = ins[i]
        m = re.fullmatch(r"MOV DI,(0x[0-9a-f]+)", t)
        if (m and i + 3 < len(ins) and ins[i + 1][1] == "PUSH CS"
                and ins[i + 2][1] == "PUSH DI"):
            name = pascal(data, int(m.group(1), 16))
            d = disp(ins[i + 3][1])
            if d is not None and re.match(r"[A-Z0-9.]+$", name):
                # Confirm a LOADER follows and not a text call. There is
                # more than one - .GFX and .SFX come through different
                # entry points - so the test is "a call, and the name is a
                # resource", not a fixed target.
                for k in range(i + 4, min(i + 14, len(ins))):
                    if ins[k][1].startswith("CALL"):
                        if "." in name:
                            slots[d] = name
                        break
                i += 4
                continue
        m = re.fullmatch(r"MOV AX,word ptr \[BP \+ (-?0x[0-9a-f]+)\]", t)
        if m and i + 3 < len(ins):
            src = disp(t)
            if (disp(ins[i + 1][1]) == src + 2
                    and re.match(r"MOV word ptr \[BP \+ .*\],AX", ins[i + 2][1])
                    and re.match(r"MOV word ptr \[BP \+ .*\],DX", ins[i + 3][1])):
                dst = disp(ins[i + 2][1])
                if src in slots:
                    slots[dst] = slots[src]
                i += 4
                continue
        i += 1
    return slots


def frame_list(slots, base, high):
    return [slots.get(base + 4 * k, "?") for k in range(high + 1)]


def main():
    ins = load_disasm(sys.argv[1])
    data = load_bytes(sys.argv[2])
    slots = slot_map(ins, data)

    pages, cur, args, pending = [], [], [], []
    for _, t in ins:
        p = re.fullmatch(r"PUSH (.*)", t)
        if p:
            args.append(("push", p.group(1)))
            continue
        m = re.fullmatch(r"MOV DI,(0x[0-9a-f]+)", t)
        if m:
            args.append(("di", int(m.group(1), 16)))
            continue
        c = re.match(r"CALLF? (\[0x[0-9a-f]+\]|0x[0-9a-f]+:[0-9a-f]+)", t)
        if not c:
            continue
        tgt, a = c.group(1), args
        args = []
        # The pushed words, IN ORDER. A far pointer arrives as two register
        # pushes, so keep the slot and record None - positions matter.
        words = [imm(v) for k, v in a if k == "push"]
        raws = [v for k, v in a if k == "push"]
        nums = [n for n in words if n is not None]
        if tgt == WRITE_AT and len(nums) >= 4:
            off = next(v for k, v in a if k == "di")
            cur.append(("text", nums[0], nums[1], nums[2] & 0xFF, nums[3],
                        pascal(data, off)))
        elif tgt == DRAW and len(nums) >= 2:
            ptr = None
            for _k, x in a:
                mm = re.fullmatch(r"word ptr \[(0x[0-9a-f]+)\]", str(x))
                if mm:
                    ptr = int(mm.group(1), 16)
            cur.append(("atom", nums[0], nums[1], (ptr - BALL_TABLE) // 4
                        if ptr else 0, 0, None))
        elif tgt == FILLBAR and len(nums) >= 4:
            cur.append(("bar", nums[0], nums[1], nums[2], nums[3], None))
        elif tgt == ANIMATE:
            # 23 words, plus the `PUSH CS` that makes the near call a far one.
            r = raws[-24:-1]
            # A sound arrives as segment then OFFSET, so the second of the
            # pair is the pointer slot the loader filled.
            snd = [slots.get(disp(r[10]) if disp(r[10]) is not None else 1),
                   slots.get(disp(r[21]) if disp(r[21]) is not None else 1)]
            pages.append((cur, words[-24:-1], snd, pending.pop() if pending
                          else None))
            cur = []
        elif tgt == "[0x230e]" and pages:
            # A standalone PlaySound between two pages - the "What the..."
            d = disp(raws[-1]) if raws else None
            pending.append(slots.get(d))

    out = []
    w = out.append
    w("// GENERATED by tools/gen_cutscene.py from the disassembly of")
    w("// `1b2e:1651`. Do not edit - regenerate.")
    w("//")
    w("// The story is the game's own text, extracted rather than transcribed")
    w("// for the same reason the Instructions are: a line of it mistyped")
    w("// would never be noticed.")
    w('#include "cutscene.h"')
    w("")
    w("namespace tubes {")
    w("namespace {")
    # A sound played between two pages belongs to the page it follows.
    for i in range(len(pages) - 1):
        if pages[i + 1][3]:
            pages[i] = pages[i][:3] + (pages[i + 1][3],)
            pages[i + 1] = pages[i + 1][:3] + (None,)
    for n, (items, _, _, _) in enumerate(pages, 1):
        w(f"const CutsceneItem kPage{n}[] = {{")
        for kind, x, y, c, mode, text in items:
            if kind == "text":
                w(f"    {{CutsceneItem::kText, {x}, {y}, {c}, {mode}, "
                  f"{cstr(text)}}},")
            elif kind == "atom":
                w(f"    {{CutsceneItem::kAtom, {x}, {y}, {c}, 0, nullptr}},")
            else:
                w(f"    {{CutsceneItem::kBar, {x}, {y}, {c}, {mode}, "
                  f"nullptr}},   // w x h")
        w("};")
    w("}  // namespace")
    w("")
    w("const CutscenePage kCutscenePages[kCutscenePageCount] = {")
    for n, (items, nums, snd, after) in enumerate(pages, 1):
        assert len(nums) == 23, (n, nums)
        # the 23 pushed words, in the order 1b2e:0f46 reads them
        # The 23 words, in the order 1b2e:0f46 reads them. Positions 3/4
        # and 14/15 are the two frame-list far pointers and are None here.
        (secs, ax, ay, _ap, _aps, ahi, aw, ah, acount, _as1, _as2, aframe,
         bx, by, _bp, _bps, bhi, bw, bh, bcount, _bs1, _bs2, bframe) = nums
        def q(v):
            return f'"{v}"' if v else "nullptr"
        w(f"    {{kPage{n}, sizeof(kPage{n}) / sizeof(kPage{n}[0]), {secs},")
        w(f"     {{{ax}, {ay}, {aw}, {ah}, {acount}, {aframe}, {q(snd[0])}}},")
        w(f"     {{{bx}, {by}, {bw}, {bh}, {bcount}, {bframe}, {q(snd[1])}}},")
        w(f"     {q(after)}}},")
    w("};")
    w("")
    w("// The two frame lists, recovered from the loads and the pointer")
    w("// copies in `1b2e:1651` - 10 files across 26 slots and 16 across 17.")
    w("const char* const kWriteFrames[kWriteFrameCount] = {")
    for nm in frame_list(slots, -0x78, 25):
        w(f'    "{nm}",')
    w("};")
    w("const char* const kBlowFrames[kBlowFrameCount] = {")
    for nm in frame_list(slots, -0xBC, 16):
        w(f'    "{nm}",')
    w("};")
    w("")
    w("// `2321:0ac0`, extracted the same way: nine fills in terms of the")
    w("// panel's own x, y, w, h.")
    w("const PanelFill kPanelFills[kPanelFillCount] = {")
    for f in PANEL:
        w("    {%d, %d, %d, %d, %d, %d, %d, %d, %d}," % f)
    w("};")
    w("")
    w("}  // namespace tubes")
    print("\n".join(out))
    print(f"{len(pages)} pages, {sum(len(p[0]) for p in pages)} items",
          file=sys.stderr)


if __name__ == "__main__":
    main()
