# Work log

Chronological record of the reverse-engineering and porting effort. Format
specifics live in `reversing-notes.md`; this file records the sequence,
the reasoning, and the mistakes.

---

## 2026-07-25 — Session 1

Start: a directory of original game files and Ghidra. No prior analysis.

### Unpacking the binary

`TUBES.EXE` is LZEXE v0.91 compressed — the reason a disassembler shows only
noise. Decoded the packed MZ header and the LZEXE info block by hand
(`SS:SP = 1A70:2000`, `CS:IP = 0000:AABA`), then wrote a Python unpacker.

It was subtly wrong. The stream desynchronised at exactly the 16th bit
because the reference `getbit` samples the bit *before* decrementing the
counter and skips the shift on refill. The first 15 bytes still decoded into
valid x86, so it looked correct. Discarded the reimplementation and used
upstream `unlzexe`; `tools/unpack.sh` builds it. Result: 99,728-byte image,
2,150 relocations, recovered entry state matching the info block exactly.

Also noted: a web source had the two LZEXE escape codes swapped, and
following it would have introduced a bug. The C source was authoritative.

### Toolchain identification

The credits screen states the game was written in **Borland Pascal v7** using
planar 320x200x256 (Mode X). Confirmed by the BP7 RTL error strings.

This mattered a lot: BP7 is non-optimising and deterministic, so a *matching
decompilation* is realistic — compile candidate Pascal and diff bytes, which
gives provable correctness without playtesting.

Obtained Turbo Pascal 7.0 and verified it compiles headless under DOSBox.

### Version check

Compiled a reference program and byte-compared its RTL against the game's.
A first classifier reported "version mismatch" — **wrong**. It only accounted
for segment relocations, not intra-segment address operands (jump
displacements, DS-relative variable offsets), which necessarily differ
between two programs. Decoding the context showed every *opcode* identical.

The decisive evidence: a single constant delta held across 1000+ byte spans,
which a codegen change would break. Final tally across 3,878 bytes of RTL:
360 operand-only differences, **0 structural**. TP 7.0 is correct; no 7.01
hunt needed.

### Ghidra

Imported as `x86:LE:16:Real Mode`. 297 functions, entry `1000:aaba`.

Ghidra's 24 `CODE_*` segments correspond to Borland Pascal **units**.
Cross-checking against the RTL byte-match split them cleanly: game code in
`CODE_0`..`CODE_15` (~84KB), RTL in `CODE_16`..`CODE_23` (~16KB), with
`CODE_22` identified as the System unit.

Stock analysis found 50 strings and zero references, because Turbo Pascal
uses length-prefixed `ShortString` and Ghidra hunts for NUL-terminated C
strings. Wrote `FindPascalStrings.java`: **811 strings, 709 references, 75
functions**. That produced the first subsystem map and, unexpectedly, exposed
the resource names inside `TUBES.RES`.

### Resource container

First attempt ranked *shared callees* of resource-using functions and
confidently returned a palette-fade routine and a UI panel builder — every
function that loads a resource also draws with it. Switching to **argument
flow** (find the `CALL` immediately after each resource-name string) split
the layers correctly and identified `2407:0146` as the container open.

Decompiling it gave the format directly, because BP7's plain codegen left the
RTL calls visible: `Reset(f,1)`, an `IOResult` check, `BlockRead(hdr,39)`, a
version compare, `Seek`, `GetMem(count*26)`, `BlockRead`.

Validated with zero discrepancies on both containers — payloads contiguous
from offset 39, directory ending exactly at EOF.

### Compression

Payload compression is **Okumura LZSS** (4096-byte ring pre-filled with
spaces, 12-bit offset, length `(b & 0xf) + 3`), recovered from `2475:115c`
and `2475:10dc`. My day-one guess of an `FF`-marker RLE was wrong — reading
it off the code beat pattern-staring.

**239/239 payloads decompress to exactly their recorded size.** Independent
confirmation: all three `.PAL` resources come out at exactly 768 bytes
(256x3 VGA entries), which the size oracle does not constrain.

### Sprites

`.CSP` = **Compiled Sprite** — not a bitmap but generated x86 that draws with
unrolled stores and `retf`, with transparency implicit in the absence of an
instruction.

First parse assumed the Mode X plane switch was an atomic 6-byte block:
103/108, with all five failures being shadow sprites. They repeat `rol`/`adc`
without an intervening `out` to skip empty planes. Tracking the carry flag
across independent instructions took it to **108/108**.

Rendered against `TUBES.PAL`: correct artwork, eight atom colours matching
the eight elements of the story, plus fade animations and lettered specials.

### Images, demo, fonts, audio

`.GFX` — `u16` width, `u16` height, planar Mode X pixels. All 73 render.
I first wrote it as chunky; every header check still passed and only
*rendering* exposed it (image tiled 4x horizontally, squashed 4x vertically).
Three `0xE5`-prefixed resources are genuinely chunky — and they are exactly
the three the game fetches through a different routine, which is what drew
attention to them. Palettes are per-scene, not global.

`.SCR` — **not a cutscene script**, as assumed from the extension, but a
per-frame input recording for attract mode. Its referencing function is the
attract-mode setup, and the pointer it stores is consumed by the main game
loop. Bit assignments inferred from behaviour: across 11,970 frames the
impossible combinations (up+down, left+right) never occur while the four
legal diagonals and direction+button do.

`.816`/`.88` — headerless VGA glyph tables, 256 chars, MSB leftmost. Five
fonts. `THIN8X8.816` is an 8x8 design padded into 8x16 cells.

`.SFX` — `0xf1` marker, Pascal name, `u32` rate (always 8000), `u16` count,
unsigned 8-bit PCM. All 24 convert to WAV. Self-documenting: each carries a
readable name in the header. **The user listened and confirmed they sound
correct** — which retroactively validates the LZSS decoder for every format,
since a subtly wrong ring buffer would still yield correct-length output.

### Renderer

C++/SDL2, 320x200 indexed framebuffer, integer scaling with nearest-neighbour
and letterboxing. Loads everything from the user's `--gamedir`.

Two porting bugs, both recorded:

- **Negative offsets need floor division.** C++ truncates toward zero where
  Python floors, turning a 16x13 sprite into 334x12 with origin (-190,-1).
  The pixel *count* stayed correct, so every count-based check passed; it
  rendered as a flat sliver.
- **Sprite origin is provenance, not placement.** Adding `originX` (~128,
  reflecting the original base pointer) to a draw position pushed sprites
  off-screen.

### Gameplay

`Board` (grid, matching in four directions, settling) and `Game` (dispenser,
test tube, drop limits, cascades). Input uses the same bit layout as a `.SCR`
recording, so a demo can later drive the same update path.

`tubes-tests` covers the matching rules — 19 checks. It caught that my own
cascade *fixture* was wrong (clearing `111` under `8.88` leaves a gap, so no
second match forms); the implementation was correct.

Also fixed movement that was purely edge-detected, so holding a direction
moved one column and stopped. Only the scripted player exposed it — the first
version accidentally masked the bug by oscillating its target.

### Music — partial

`.MUS` is OPL2, proven by disassembling the canonical AdLib register-write
sequence in `FMMUSIC.DRV`. The body appears to open with 14-byte records.

Ruled out by measurement: not a raw register log, not fixed 4-byte events.
Best-case register validity is ~50% against ~39% expected by chance, with the
winning phase differing per file.

Stuck. Statistical probing of the data is exhausted; next step is the
sequencer inside `FMMUSIC.DRV`.

### State at end of session

17 commits. Every asset format solved except `.MUS`. Engine renders and
plays. `.SPR`, `.ANM` and `.BIN` never examined.

---

## 2026-07-25 — Session 2

The user reports the engine runs against their own copy: it renders, the
basics work, the beaker is invisible and stuck at the top of the screen.
Goal restated as a faithful 1:1 recreation plus quality-of-life additions.

### Music — solved

Picked `.MUS` back up. The previous session left it blocked on "the
instrument-table length cannot be inferred", which turned out to be the wrong
question — there is no instrument table. Those `00 1n` records are ordinary
events that happen to carry a delta of 0.

What broke it open was reading `GMMUSIC.DRV` next to `FMMUSIC.DRV`. Both
drivers consume the identical byte stream, one emitting OPL2 register writes
and the other General MIDI, so every field is pinned twice. Guessing stopped
being necessary.

The grammar is `<delta:u8> <cmd:u8> <args>` with the command's high nibble
selecting one of six handlers and the low nibble selecting a channel. Framing
confirmed by an oracle before any semantics were assigned: **all 10 resources
parse to exactly EOF with zero trailing bytes.**

Two things had made this look impossible:

- Channel indices run to 10, which cannot be right for a 9-voice chip. The
  operator table at `cs:0x3a` explains it — the driver enables OPL2 rhythm
  mode permanently, giving 6 melodic voices plus BD, SD, TT, CY, HH.
- The leading records looked like a table because every one carries delta 0.
  They are just setup events sharing timestamp zero.

Tempo needed no guessing either: `init` programs the PIT with divisor
`0x4000`, so ticks run at 1193182/16384 = 72.827 Hz, and the ISR runs a
Bresenham divider so the BIOS keeps its 18.2 Hz.

Independent confirmation, in ascending order of hardness to fake: durations
match filenames (3.1 s logo, 5.5 s death, 87 s title); the melodic instrument
byte decodes to GM program 32, Acoustic Bass, on a channel playing E2/G2; and
the percussion instrument bytes come out 36, 38, 42 — Bass Drum, Acoustic
Snare, Closed Hi-Hat in the standard GM drum map — on exactly the channels the
operator table assigns to BD, SD and HH. That map is an external standard, so
it cannot be an artifact of the decoding.

`tools/mus_decode.py` dumps events and exports two things: MIDI, which is a
transcription of the game's own GM driver rather than an approximation of it,
and DRO v2, an OPL2 register log transcribed from the FM driver write-for-
write. Splitting them deliberately isolates "are the notes and timing right?"
from "is the FM synthesis right?" — so a bad result names its own cause.

Rendered through fluidsynth and confirmed by ear, then confirmed again by the
user through `adplay` on both exports.

One real limit hit: the driver's reset sweep blanks registers `0x01..0xf5`,
245 of them, which overflows DRO's 127-entry codemap. Pruning writes that do
not change chip state fixes it and is lossless from reset, since OPL registers
are state rather than triggers. The C++ player will keep the full sweep.

The lesson worth keeping: a whole session of histograms and stride tests
produced nothing, and reading a second driver produced everything.
