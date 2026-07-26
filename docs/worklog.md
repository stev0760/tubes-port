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

### Music playing in-engine

Vendored Nuked-OPL3 (LGPL 2.1, unmodified) into `third_party/` and ported the
sequencer to `src/mus.cpp`, driven from the SDL audio callback so tempo comes
off the sample clock rather than the frame rate.

The synth is deliberately *not* the thing being trusted. `--dump-regs` prints
the register stream the sequencer produces, and it is diffed against
`tools/mus_decode.py`. **All 10 songs match to the byte — 23,319 register
writes.** That makes the port provably correct independently of whether the
emulator sounds right, which is the same separation the MIDI/DRO split was
built for.

One bug found while wiring the bank dump: the bass drum was printed as
single-operator. The driver's two-operator test is `ch <= 6` while the
melodic/percussion split is `ch < 6`, so channel 6 sits on the wrong side of
the obvious guess. `FmPlayer` had it right and only the new display code was
wrong — but it is exactly the kind of off-by-one that would have sounded
subtly bad and been blamed on the emulator.

Also settled a question raised by listening: the MIDI and DRO exports sounded
different, with more percussion in the DRO. Measured rather than assumed —
percussion note-ons and 0xBD rising edges match exactly in all ten files, so
neither export drops or adds hits. The difference is timbre: synthesized
rhythm-mode drums against sampled GM ones.

And a negative result worth keeping: the whole soundtrack uses only 12
distinct OPL patches, but a standard OPL bank (OP2, WOPL) cannot hold them,
because those formats key on GM program number and this game reuses program 0
for four unrelated patches. The engine needs no bank at all — patches arrive
inside each `.MUS` and load at runtime.

### Playfield geometry, and a whole-program map

Picked up the gameplay constants. First result was a correction: `1000:9e53`
had been labelled "the playfield renderer" and is nothing of the sort - it is
the game *session*, which loads the play-area art, seeds difficulty, and runs
the frame loop. `1000:3a67` takes no arguments yet reads its caller's frame,
so it is a **nested Pascal procedure** sharing `9e53`'s locals. That single
structural fact explains why decompiling `3a67` alone had been so unrewarding.

Wrote `ghidra_scripts/MapProgram.java`, which pairs the call graph with the
ShortStrings each function references. Borland Pascal puts one unit per code
segment and leaves literals in plain sight, so what a function does is usually
answerable from the text and resource names it touches. It identified every
interface stage in one pass - both splashes, the title/menu, the blackboard
cutscene, the test-tube screen, the game session - and incidentally explained
`SOFT.ANM`, the one `.ANM` in the game, which belongs to the developer splash.

The playfield array turned out to be **6 x 5**, not the 7 x 10 taken from the
manual - three parallel arrays indexed `[i * 6 + j]` with the inner loop
running 1..6 and the outer 1..5. Wrong in both dimensions.

Then the cell-to-pixel mapping, off the draw loop at instruction level:
`y = row * 13 + 121`, and x from a six-entry table giving 107, 125, 143, 161,
179, 197. **Column pitch is 18 while the sprites are 16 wide** - assuming the
pitch equalled the cell size was the single wrong assumption doing all the
damage. Two independent checks agreed: the table is stored as two descending
triples that only read as a uniform progression under the derived column
order, and the resulting grid centres on 160, exactly the centre of the
x 74..245 gap measured earlier from `GAMEFG.GFX`.

### The dispenser, and five self-corrections

The user described the mechanic from play: atoms spawn bottom-right, trace up
and over the tube arc as a colour preview, then fall. The test tube slides on
a rail and holds several atoms; A tips one out, B speeds an atom along.

That description immediately explained a number already in hand. The 12
records of 28 bytes initialise to (303, 186) - off the right of the play area,
at the bottom row. Read as a "parked" sentinel while atoms were assumed to
fall downward; read as the **spawn point** the moment their direction was
known.

The rest of the session was mostly being wrong in instructive ways. Five
corrections, every one caused by a *tool* being wrong rather than the binary
being obscure:

1. **`BEAKER.CSP` dismissed twice.** Measured as "widest clear gap at three
   sample rows", which read the space between internal structures rather than
   across the interior. Occupancy over the full height gave walls at 4-5 and
   108-109: interior 106 px and height 65, both exactly the grid. It had even
   been noted that 65 = 5 x 13 and written off as coincidence.
2. **"No waypoint table in DGROUP."** The scan required smooth runs of 8 or
   more words. The table has 6. The filter excluded the answer, which was
   sitting at `DS:0x26` immediately after the column-x table.
3. **A correct finding withdrawn.** "How many `*28` index computations are
   followed by a sprite draw" returned 1 of 18, taken as disproof that the
   records were drawable objects. That one site *was* the draw, and the only
   one that needed to be.
4. **Two structures conflated repeatedly.** `parent - 0x163` (12 x 28, the
   atoms) and `parent - 0x16a` (a single struct) were assumed to share a base.
5. **A mutation census reporting 9 instead of 38.** The regex matched only
   positive displacements, so every stack local was invisible. Caught because
   the same regex claimed zero mutations across 1392 bytes of a sibling
   function, which cannot be true of real code.

The largest correction came out of chasing the dwell timer: the single struct
at `parent - 0x16a` is **the player's test tube**, not a travelling atom. It
acts only when stopped, then calls the input driver and branches on button
bits. Its waypoint targets are the six column x's minus 3 - the exact offset
the test tube is drawn at, a number already sitting in our own rendering code.
Button A puts it in state 3, which runs a four-phase counter indexing a sprite
table: the tipping animation. Which in turn means `TESTUBE1/2/3` at 22x65,
20x42 and 20x27 are **tipping frames**, a tube foreshortening as it tips, not
three capacities for three difficulties.

Atom speed was not found. It is not in the tube struct, not in any sibling
function, and the atom array's x/y are never incremented - they are written
whole, so positions are computed rather than accumulated and there may be no
speed field at all.

### Where the method stopped paying

The reliable move all session was *targeted* disassembly against one specific
question: searching for `imul ax,ax,0xd` produced the cell mapping outright,
and reading a single draw call gave the record layout. Reading the decompiler's
output for a 9382-byte Pascal procedure with nested frames produced almost
nothing but the five errors above.

Recorded in `PLAN.md`: the next step is DOSBox-X's debugger, already installed
on this machine. A memory breakpoint on the atom array identifies the writing
code directly rather than inferring it, and speed settles by observation.

### State at end of session

37 commits. Every asset format solved, music playing in-engine through
Nuked-OPL3 with a byte-verified sequencer, playfield geometry measured, the
whole interface mapped. The port is roughly 40% - reversing well ahead of the
game itself.

---

## 2026-07-26 — Session 3

No reversing progress by design. The previous session ended with static
disassembly exhausted, so this one built the replacement instrument.

### The rig

`lokkju/dosbox-x-remotedebug` (DOSBox-X 2025.12.01 plus a GDB remote-serial
stub and a QMP server) and `jdmichaud/dosbox-mcp` (24 tools over the two of
them). Both cloned to `~/Dev/tubes-tooling/`, outside this repo — the drive it
mounts is copyrighted game data and the paths are machine-local. `uv` installed
user-local; the fork built with `--enable-remotedebug --disable-libfluidsynth
--disable-mt32 --disable-avcodec`. Full account in `docs/debug-rig.md`.

`--disable-avcodec` was a deliberate risk reduction: the machine has ffmpeg
8.1 and the AUR recipe needed an ffmpeg-4.4 shim even for the 2022 release.
Video recording is irrelevant here and screenshots go through libpng, so
nothing is lost.

The DOS drive is symlinks to the game files plus *copies* of the two files the
game writes (`SETUP.CFG`, `TUBES.SAV`), so an experiment cannot corrupt the
user's real save.

### Two corrections to the plan, found by reading rather than running

Both were in `PLAN.md`'s section 0, and both would have cost a session.

1. **Which binary the rig runs.** Set up against the shipped `TUBES.EXE` at
   first, on the assumption that was the target - wrong, and the user corrected
   it: `assets-extracted/TUBES_UNP.EXE` has existed since day one and is what
   Ghidra analysed. It matters for the entry breakpoint. Packed, break-on-exec
   stops in LZEXE's stub and `CS` is the packer's; unpacked, entry `CS:IP` is
   `0000:aaba` - image-relative segment 0 - so `CS` at entry *is* the load
   segment and `PLAN.md`'s mapping holds unchanged.

   Worth recording how the wrong turn happened: the packed EXE was sitting in
   the game directory with the obvious name, and "LZEXE means you cannot break
   at the entry point" is a true statement that produced a false conclusion
   because it was applied to a file we had no reason to run. The unpacking was
   already in `docs/worklog.md`, session 1.

   The mapping is now **proven statically**, which needed no emulator at all.
   DGROUP is Ghidra segment `0x2785`, so `0x1785` paragraphs into the image, at
   file offset `0x1785*16 + 0x2200 = 0x19a50`. The waypoint targets read out of
   the file there are `104, 122, 140, 158, 176, 194` - byte-exact against the
   measured values. Six values matching by chance is not credible.

   Also caught statically, before running anything: the 24-byte signature
   invented for the runtime scan **does not exist**. `DS:0x1a` does not hold the
   columns ascending; it holds two descending triples,
   `143, 125, 107, 197, 179, 161`, indexed 3,2,1,6,5,4 - which
   `reversing-notes.md` had documented correctly all along, several lines below
   where the reading stopped. The lesson is the cheap one: check a guess against
   the file before building a tool on it, and read the notes before inventing a
   signature the notes already contradict.

2. **The GDB stub has no memory watchpoints.** `handle_breakpoint` in
   `gdbserver.cpp` declines every `Z` type except 0, so `Z2` — break on write —
   is unavailable, and `dosbox_debug.py` hardcodes `Z0` anyway. This gates
   Experiment 3, the plan's self-described highest-value moment. Worth noting
   that `qSupported` advertises `hwbreak+`, so trusting the handshake would
   have been misleading; and that `docs/REMOTEDEBUG.md` states the limitation
   plainly, which is why it was checked against the source rather than assumed
   either way. DOSBox-X's internal debugger does have it (`BPM`, and this build
   is `C_HEAVY_DEBUG=1`) — it is simply not wired to the wire. The fix is a
   small patch mapping `Z2` onto `CBreakpoint::AddMemBreakpoint`.

### A contradiction in the input map, found from `SETUP.CFG`

Reading `SETUP.CFG` (previously an open question) resolved most of it: bytes at
`0x04`/`0x06`/`0x08` are SB base `0x220` / IRQ 7 / DMA 1 — which happens to be
DOSBox-X's default, so no tuning needed — and six consecutive words at `0x10`
are scancodes `48 4b 4d 50 1d 38`: the four arrows plus left Ctrl and left Alt.
So the remappable controls are arrows + Ctrl + Alt.

But that order is up/**left**/**right**/down, and the `.SCR` bit assignments in
`reversing-notes.md` were inferred as up/**down**/**left**/right. The ends
agree — up first, then the two buttons — which is what makes the middle
disagreement worth taking seriously. Exactly one is wrong and neither is
proven: the `.SCR` reading is explicitly behavioural, and its load-bearing step
was reading the long-held bit as "down, to drop faster" — but the mechanic is
now known to be a sliding tube where **button B** speeds an atom along, which
is at least as good a candidate for a long hold.

Left unresolved on purpose. Guessing here is precisely the failure mode that
cost the last session, and `PLAN.md` §5 plans to use `DEMO.SCR` as a
correctness oracle — an oracle on a wrong bit map silently validates wrong
behaviour. Six QMP key injections against the input driver's byte at
`ds:0x2352` settles it by measurement, and is now Experiment 0.

### The rig works, and the game runs

`bringup_tubes.py` passes. `CS` at the entry breakpoint is `0x0824`,
`EIP - CS*16` is `0xaaba` exactly as the unpacked header says, DGROUP lands at
`0x0824 + 0x1785 = 0x1fa9`, `DS:0x26` reads back the measured targets, and an
independent RAM scan for the `DS:0x1a` bytes finds them at `0x1faaa` -
`0x1fa90 + 0x1a`, precisely where the entry-point route predicted. Two
independent methods, one answer. `L` is not a constant to memorise; it follows
the DOS memory layout and must be re-measured each session.

Getting there needed one real piece of reversing. The game refused to start with

    This game requires complete control of your computer.  Please run from DOS!

which is not in the startup-message block and is not about `SETUP.EXE` despite
the first check's wording (that one tests for `SETUP.CFG`; the executable is
irrelevant). Ruled out `SETUP.CFG`, EMS, XMS, UMB and HMA by sweeping conf
variants - six runs, all refused - which said the guard was not about memory
managers at all.

Then a **single A/B run settled what a sweep could not**: the *packed* binary
fails identically from the same drive. So it was never an unpacking artifact,
and `TUBES_UNP.EXE` was cleared. Worth noting the user's report pointed at the
unpacked EXE and at `SETUP.EXE`, and both readings were wrong - the packed one
had simply been run in a directory that had `SETUP.CFG`, so it got further.

Finding the actual check took no emulator at all. Both message strings had to be
referenced from the same code segment, and a Pascal string constant is passed by
the address of its length byte - so for a segment at paragraph `S` the immediate
must equal `img - S*16`, which forces `imm mod 16` to match `img mod 16`.
Inverting that over every `push imm16`/`mov reg,imm16` in the image left exactly
**one** candidate segment, Ghidra `0x21ea`. Disassembling there:

        mov  ax, 0x1680          ; INT 2Fh, release current VM time-slice
        int  0x2f
        not  al                  ; 0x00 back if something serviced it,
        mov  cl, 7               ; 0x80 (unchanged AH) if nothing did
        shr  al, cl
        cmp  byte [0xd3e], 0
        je   +9                  ; continue into driver install
        mov  di, 0x167           ; the message

A Windows/DPMI multitasker check, entirely reasonable for a program about to
hook interrupts and reprogram the VGA. DOSBox-X services that call to idle the
host CPU, so it looks like Windows. `[dos] dos idle api = false` fixes it and is
now in `tubes.conf`.

The game then reaches its splash screens - the Absolute Magic logo (`2178:00eb`)
rendered with animated plasma, captured headless. **First time this game has run
under instrumentation in this project.**

Also recorded, from the same read: the six startup checks and their messages,
now in `reversing-notes.md`. And two rig quirks - QMP's screendump ignores its
`file` argument, and captures come out 640x400 rather than 320x200 because mode
13h is double-scanned.

`dosbox-mcp` is installed local-scope to `tubes-port`, all 24 tools verified to
list over stdio. Needs a Claude Code restart to appear as `dosbox_*` tools.
