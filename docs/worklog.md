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

### Experiment 0: the input bit map, settled

Closed the contradiction recorded earlier the same day, and it turned out there
was never one to close.

`CLAUDE.md`'s top-ranked method - *find a second consumer of the same data* -
applied directly: `KEYBOARD.DRV`, 641 bytes, already sitting extracted in
`assets-extracted/drivers/`. Its INT 9 handler compares the scancode against six
slots and ORs a bit into a mask byte, and the slot-to-bit map is
`1, 4, 8, 2, 16, 32` - **not** the identity. That single fact makes
`SETUP.CFG`'s slot order (up/left/right/down) and the `.SCR` bit reading
(`0x02`=down, `0x04`=left, `0x08`=right) both correct simultaneously. The
"contradiction" was manufactured by assuming slot *n* mapped to bit *n*.

Verified live rather than left as a reading. The driver's base comes from the
INT 9 vector; six key injections, six single-bit results, every one matching the
prediction, each clearing on release:

    0x01 up      0x02 down    0x04 left
    0x08 right   0x10 A = Left Ctrl   0x20 B = Left Alt

Two things this buys. The `.SCR` bit table had only ever been inferred from
run-length statistics, and is now confirmed against the input handler - so
**`DEMO.SCR` is safe as the correctness oracle** §5 depends on. And the buttons
are named, which was open.

Incidental findings: the game patches the driver's scancode table at load time
from `SETUP.CFG` (the shipped driver keeps its own defaults in the high byte of
each slot word, and has Ctrl/Alt the other way round, so `SETUP.CFG` governs);
the old INT 9 vector is saved at driver `+0x18` and reads `F000:E987`.

A tooling limit found doing it: held-key state cannot be observed through the
MCP. `dosbox_press_key` blocks for its whole `hold_ms` and only returns after
the release, and **both** the GDB stub and the QMP server are single-client, so
a second connection cannot be bolted onto a running MCP instance - the socket is
accepted but no QMP greeting arrives. Anything needing a key held across a
memory read has to be a standalone script using `key_down`/`key_up`. That is
what `exp0_input_bits.py` is.

### Experiment 1: the atom array, located live

Confirmed `12 records x 28 bytes` and confirmed `(303, 186)` is a spawn marker,
but *not* by the route the plan proposed.

Breaking at `1000:3a67` and reading the static link went wrong in an instructive
way. The prologue cooperates - `enter 0x3c6, 0`, then `mov di,[bp+4]` at `3a70`,
so breaking at `3a73` hands you the parent frame in `DI` with no stack walking.
But the frame it hands you contains the Pascal strings `Tubes 101`, `Tubes 201`,
`Tubes 301`, `ifficu`(lty) and `Exit`: **`3a67`'s first call is the
difficulty-selection screen**, not the game loop. No atoms exist yet, and
`parent - 0x163` scores 3 plausible records out of 12 there - noise. Scoring
every candidate base in a ±0x220 window found nothing better than 6/12, which is
the right answer for "the array is not in this frame".

What worked was a signature, the same move that settled DGROUP. The records
initialise to `(303, 186)` and x=303 is off the right of the play area, which
ends at 245 - so it cannot be a mid-play coordinate. Searching conventional RAM
for `2f 01` plus a plausible y gave **ten hits at an exact 28-byte stride**,
spanning precisely twelve slots (`0x2419e + 11*28 == 0x242d2`).

Ten of twelve, and the two absentees were records 1 and 2. The screenshot from
the same halt shows **exactly two atoms on screen**. That is the confirmation -
a record leaves the marker exactly when its atom is in transit. Worth noting the
screenshot was the load-bearing evidence, not the arithmetic; "render it, or
listen to it" again.

Corrections forced along the way:

- `+0x1e`/`+0x1f` are not atom-record fields. `0x1f` = 31 does not fit in 28
  bytes; they were measured on the test tube and merged into the atom table by
  mistake. Caught by an IndexError, which is a cheap way to find out.
- The three difficulties are named `Tubes 101` / `Tubes 201` / `Tubes 301`.

Still open: the stable frame-relative offset. `parent - 0x163` remains
unverified; it needs the breakpoint re-armed *after* difficulty selection so the
static link is read during actual play, then cross-checked against the base the
signature search finds in the same run.

Free findings from the screenshots. The cutscene names all eight elements -
Redium, Greenium, Bluium, Cyanium, Purplium, Yellowium, Pinkium and
**Flashium** - against grid cells that cycle 1..7, so seven ordinary colours plus
one special is the likely split, with `Flashium` the obvious candidate. The
in-play HUD reads `Chains` top-left and `Drops` top-right with two counters
between. The title screen animates an atom along the tube logo.

Rig lessons, all now in `docs/debug-rig.md`: a screenshot needs the guest
*running* (halted, it times out); a timed-out call desynchronises the GDB stream
and every register reads back `0`, which is a broken connection and not guest
state, fixable only by restart; Mode X planes are not readable at `0xa0000`, so
the framebuffer cannot be used as a cheap screen-state test; and menu navigation
must not be timed, because the cutscene length varies - pressing Enter until the
breakpoint fires is crude but the only thing that worked repeatably.

### Gameplay from the user, and what the resources confirmed

The user supplied behaviour from play. Rather than just filing it, each claim was
checked against the resource inventory, and the inventory turned out to settle
two open questions.

Reported: `ANTIBALL` is antimatter and destroys any balls in the beaker on
contact; `GOLDBALL` is an orange bonus ball that travels the tube **very fast**,
scores a bonus when caught and is believed to become a random ordinary ball;
`XENBALL` is an inert grey sphere; **Flashium has no static sprite of its own** -
it matches any colour and displays the other sprites while settled, and only
three Flashiums matched together give a distinct animation. Provenance matters
here and is recorded: this is the shareware release, whose preview modes
demonstrated the bonus ball and antimatter, so those two are solid; the crystal
and the letter balls are unknown to the user.

**Rendering the sprites corrected one of my assumptions.** I had guessed
`XENBALL` might be the "X ball". It is not - it is a featureless grey sphere. The
X ball is `EVILBALL`, one of six grey **letter balls** marked `?` `X` `M` `B` `C`
`F` (`MYSTBALL`, `EVILBALL`, `MULTBALL`, `BLOCBALL`, `CONVBALL`, `FILLBALL`). The
names alone were ambiguous; looking at them was decisive, which is the same
lesson this project keeps relearning.

Measured while there: 25 ball sprites split exactly, seventeen at 16x13 and eight
at 8x7, so the `S*` family is half-size versions of the seven colours plus a pale
`SWBALL`. Where the small ones are drawn is still unknown.

**Flashium explains a loop we had already recorded but not understood.** The
playfield notes said cell values run 1..7 and the draw loop "increments a cell
and wraps 8 back to 1 - a colour-cycling effect", with no reason offered. The
reason is Flashium: a wildcard with no sprite, rendered by cycling its cell
through the ordinary colours. Two independent sources meeting from opposite
directions - a loop in the binary and behaviour in play.

**The 66 fade sprites then decoded themselves.** `11 families x 6 frames` had
never been broken down. The families are the seven colours plus `AFADE`,
`GLDFADE`, `CRFADE` and `FFADE` - anti, gold, crystal and Flashium. So there is
no `FLASHBALL` anywhere in `TUBES.RES` but there *is* a complete `FFADE` family,
which independently confirms the user's account; and `FFADE1` is a ball made of
every colour at once. It also delimits the **matchable set**: only those eleven
have fade families, so the letter balls, `XENBALL`, `OBSTBALL` and the small
balls are not cleared by matching and must do something else.

New: `CRYSTAL` is a red rounded block, not a sphere, and has a fade family - so
it is clearable, function unknown. `MARKER` is a red X crosshair, unidentified.

Two concrete leads for the debugger, both cheaper than blind search:

1. **Atom speed.** `GOLDBALL` moving very fast means speed is not global - it
   varies by ball type. The record's only type field is the colour/sprite index
   at `+0x0b`, so look for a speed *selected by type* rather than a speed
   variable. It also gives Experiment 4 a far better second condition than
   holding B: gold versus ordinary from one save state.
2. **The Flashium marker.** If a Flashium's cell value is being cycled through
   the ordinary colours, the cell cannot also record that it *is* a Flashium, or
   the three-Flashium match could not be detected. The beaker grid is three
   parallel arrays of stride 6, so the marker is very likely in one of the other
   two. Dump all three with a Flashium settled.

#### Correction, same session: `FFADE` is `MULTBALL`'s, not Flashium's

I mapped the four non-colour fade families by their initials and presented the
result as identified. The user corrected `FFADE` from play: the multicoloured
mosaic is what three matched **`MULTBALL`s** produce. `F` belongs to a ball whose
sprite is marked `M`, so the initials are not a reliable key, and `AFADE` -> anti
is now flagged as resting on nothing but its letter.

A claim was withdrawn with it. I had argued that `FFADE` existing independently
confirmed Flashium having no static sprite, i.e. two sources agreeing. It does
not, since `FFADE` is not Flashium's. What survives is the single weaker
observation that no `FLASHBALL` exists anywhere in `TUBES.RES`.

The mapping should be *measured*, not spelled: `9e53` fills the far-pointer table
the draw code indexes, so settling a known ball and clearing it shows which fade
sprites are used. Cheap now, and it retires the argument entirely.

Then two further accounts closed the loop. Flashium "matches any colour, no
static sprite, flashes the others while settled, three together give the unique
animation" and `MULTBALL` "matches any colour but inherits that stack's colour
animation when cleared" are descriptions of the **same ball** - the wildcard,
called Flashium in the cutscene and `MULTBALL` in the resource. Which yields the
rule that matters for the port: **the fade family is selected by the colour
actually matched, not by the ball's identity**, with `FFADE` reserved for three
wildcards together. That is also why eleven families suffice for twenty-five
balls.

Net: one wrong identification, caught by the user within minutes because it had
been written down as a claim with its basis stated. Inferring from resource names
is fine; presenting it as identification was the error.

### The sprite type table, measured - 19 ball types

The fade-to-ball mapping had been guessed from initials, then "corrected" from
play, then guessed again. Measuring it ended the argument in one run.

Method: dump the guest heap, then identify every pointer by matching the
**entire** `.CSP` payload against RAM. That is exact rather than probable - a
compiled sprite is executable code, so it must load byte-for-byte. A first pass
used 24-byte prefixes and 11 sprites collided, because a `.CSP` opens with plane-
setup instructions that are identical whenever the top row matches. Full-content
matching resolved all 105.

The structure came out of the data. Frame 5's first fade pointer sat at
`DS:0x1f26` and frame 6's at `DS:0x1f72`, so the frame stride is 76 = 19*4, and
`0x1f26 - 4*76 = 0x1df6`. The ball table ends at `0x1da6 + 19*4 = 0x1df2`,
immediately before it. Two adjacent 19-entry tables, Pascal 1-based - and the
`0x1da6` base was already in the notes, so this confirms it and supplies the
length.

        balls:  DS:0x1da6 + 4*type
        fades:  DS:0x1df6 + 76*(frame-1) + 4*(type-1)

Types 1-7 are the ordinary colours, which measures the `1..7` cell encoding
directly. 8 has `FFADE`, 9 `ANTIBALL`/`AFADE`, 10 `GOLDBALL`/`GLDFADE`,
11..17 and 19 the specials with **null** fades, 18 the crystal with `CRFADE`.

**Type 8's ball pointer resolved to `BLUEBALL` - the same address type 3 uses.**
There is no `FLASHBALL` resource at all, so the wildcard's slot borrows a colour
sprite, and this sample caught it on blue. That is the "flashes the other
sprites" behaviour seen from the pointer side, and the same mechanism as the cell
cycling. One sample cannot separate "rewritten as it flashes" from "aliased to
blue"; two reads a second apart would, and that is the next cheap check.

Null fades for 11-17 and 19 delimit the matchable set: only the colours,
Flashium, AntiMatter, Bonus and the crystal are ever cleared.

**Two of my own conclusions were wrong and both are now retired.** `FFADE` =
Flashium was originally right but for a bad reason (its initial); I then accepted
a correction to `MULTBALL`, which the table disproves - `MULTBALL` is type 12
with no fade family, so it can never be matched and cleared. The lesson is not
about either guess, it is that a five-minute measurement was available the whole
time and would have skipped both.

### Published sources, and what they corroborate

Searching turned up RGB Classic Games' description, which lists all nine special
atoms and their effects. Read against the measured table it gives 8 Flashium,
9 AntiMatter, 10 Bonus, 11 Xenon, 12 Multiplier, 13 Evil Multiplier,
14 Convertor, 15 Blocker, 16 Filler - **the same order, from two unrelated
sources**. That corroborates the type numbering and the prose at once.

Named at last: **`XENBALL` is Xenon**, the atom that will not react with any
colour - which is exactly the "inert grey sphere" behaviour reported from play.

Three corrections to the play-derived notes: AntiMatter destroys the
*surrounding* atoms rather than the whole beaker; Bonus turns into **Flashium**
when caught, not a random ordinary ball; and the test tube holds **five** atoms,
with `FILLBALL` permanently reducing that by one - a much better account of
varying capacity than difficulty, and it undercuts the 5/3/2 guess still sitting
in `src/game.cpp`.

Also recorded: v1.0 June 1994 (Software Creations) and v1.1 November 1994
(Impulse Software), with Gold Medallion Software also credited; Endurance and
Wave modes; and the registered version adding 50 waves, five backgrounds and the
AntiMatter and Bonus atoms - which is why those two were only ever visible in
the shareware's preview modes. Year is inconsistent across sources: the title
screen and RGB say 1994, MobyGames and the Internet Archive item say 1993.

Still unexplained: `OBSTBALL` (type 17), `MYSTBALL` (type 19), `SWBALL`, and
where the seven half-size balls at `DS:0x200a` are drawn.

#### `MULTBALL` confirmed as the Multiplier

The user identified the mix-up themselves: `MULTBALL` is the `M` ball and fills
the test tube with **random** balls when caught - the "matches any colour" and
"inherits the stack's colour animation" descriptions were Flashium's all along.
Three sources now agree: the measured type table (type 12, no fade family, so
never cleared by matching), the published description, and play.

The "inherits the stack's colour animation" detail is still worth an answer,
since fades are looked up by type and a type-8 lookup could not yield a red
animation. The model that reconciles it: a settled Flashium's cell holds the
colour it **matched**, not 8 - so the type-indexed lookup gives that colour's
fade naturally, and `FFADE` is reached only when the cell really holds 8, i.e.
three wildcards matching each other. Which is also what the cell cycling `1..7`
looks like from outside: the cell walks the candidate colours and settles on the
one that pays off. Recorded as a hypothesis with a direct test - settle a wildcard
beside two of one colour and watch the cell value across the match.

#### Retraction: Flashium always uses `FFADE`; the *sound* is what varies

The hypothesis recorded a few entries above - that a settled Flashium's cell
holds the colour it matched, so a type-indexed lookup would produce that colour's
fade - is **withdrawn**. It existed only to explain a reported behaviour that the
user has since corrected: a Flashium fades with the multicoloured `FFADE`
animation *always*, regardless of what it matched with. Which is exactly what the
measured table says without any reconciliation. The elaborate model was invented
to rescue a misremembered detail, and the plain reading of the measurement had
been right the whole time.

The real variable is audio, and checking the inventory corroborated it
immediately: **eleven of the twenty-four `.SFX` resources carry names identical
to the eleven fade sprite families** - `RFADE`, `GFADE`, `BFADE`, `CFADE`,
`PFADE`, `YFADE`, `PNKFADE`, `FFADE`, `AFADE`, `GLDFADE`, `CRFADE`. One match
sound per family, named in lockstep with the animation.

So the two are indexed differently: animation per **ball** by its own type (a
Flashium in a red match still shows `FFADE`), sound per **match** by the colour
the stack resolved to (`FFADE.SFX` only for an all-Flashium chain). The animation
half is measured; the sound half is reported and coherent but unproven - and
checkable the same way the sprite tables were, since sounds are loaded resources
and a type-indexed pointer table should exist in DGROUP.

Worth noting the shape of this exchange: two of the last three corrections came
from the user re-examining their own recollection, and in both cases the
measurement had already been right. Play memory is good for pointing the
debugger at a question and poor as evidence - which is why the notes keep
reported and measured claims on separate lines.

#### `DROP` resolves what the drop limits are counting

Three findings from different places collapse into one. `DROP` is the sound for
**missing** a ball; the in-play HUD reads `Drops` at top right; and `9 / 6 / 3`
"drop limits" were measured in the binary sessions ago, before either of the
other two was known.

So a "drop" is a *missed* ball and the limit is the **miss allowance per
difficulty** - not, as the name naturally reads, a count of atoms dumped into the
beaker. Difficulty is therefore a miss allowance, selected by
`Tubes 101 / 201 / 301`. The obvious pairing is easiest-gets-nine, but which name
carries which limit is not confirmed and should not be assumed.

This also reinforces retiring the 5/3/2 tube capacity: the tube holds five at
every difficulty, `FILLBALL` is what reduces it permanently, and the per-difficulty
variable is the drop allowance.

Corrected while here: `DROP` was guessed as the sound for *dumping* an atom into
the beaker - the opposite of what it is. And `CLAP`, `NOOOO` and `WHATTHE` belong
to the **cutscenes**, not to wave-complete or loss events as guessed. `HITGLASS`
was right. `BUBBLE` remains unidentified.

### The game documents itself: capturing the in-game Instructions

The best source available was inside the program the whole time. The main menu
has an **Instructions** entry - a slide show - and driving the rig to it and
capturing every slide produced more hard information in one run than several
sessions of inference. `grab_instructions.py`.

**The atom-speed question is answered outright:** "Press Button B **or Down** to
increase the speed of any atoms in the tube **directly above the test tube**."
So the boost is *positional*, gated on the atom's column matching the tube's, and
both B and Down trigger it. It also reinterprets the `.SCR` demo - those long runs
of bit `0x02` are the player holding **Down to speed atoms**, not "holding down to
drop faster" as the behavioural reading had it. The bit assignment was right; the
story about what the player was doing was wrong.

**Scoring is solved:** vertical 250, horizontal 500, diagonal 1000 (two diagonals
= the "4 chains" the slide mentions); chains = atoms - 2; simultaneous chains
apply a bonus multiplier; a Bonus atom adds 1000 to a Bonus Jackpot and awards it.
Those replace invented constants in `src/game.cpp`.

**Capacity is a flat 5** and the drop allowance *is* the difficulty setting -
"You are allowed to drop some atoms depending on your difficulty setting" - with
both game modes ending "if you drop more atoms than allowed". That is the third
independent confirmation of the drop-limit reading and the final nail in the
5/3/2 capacity guess.

All nine specials are described in the game's own words, in two labelled groups -
**Special Atoms** (Xenon, AntiMatter, Bonus) and **Penalty Atoms** (Multiplier,
Evil Multiplier, Convertor, Blocker, Filler). Two corrections to the third-party
list: **Convertor** changes *every* occurrence of the atom it lands on across the
whole beaker, not just the ones underneath; and **Filler** works by permanently
*adding an atom to the bottom of the tube* rather than decrementing a counter.

That last one identifies a sprite. Play reported "a sticky black one that cannot
be dumped from the tube"; `OBSTBALL` is near-black, has no fade family - right for
something that lives in the tube and is never matched - and reads as
*obstruction*. So type 17 is very probably Filler's parked atom. Strongly
supported, not proven.

**The Penalty Atoms' slide order is exactly measured types 12..16.** Third-party
prose already matched types 9..16 and the game's slides match 12..16, so three
independent orderings now agree on the type numbering.

Also settled: `XENBALL` is Xenon and carries **no letter**; the `X` is the Evil
Multiplier, which *fills the tube with Xenons* - which is exactly why "X stands
for Xenon" is a natural thing to half-remember. Both readings were reconcilable.

Still undocumented by the slides: `MYSTBALL` (type 19) and the crystal (type 18).

**A side observation worth checking:** the slides include AntiMatter and Bonus,
and published notes say those two are *registered-version* additions. So the copy
in `..` may already be the full version rather than shareware. Not conclusive -
shareware slides could describe features it does not ship - but the wave count
would settle it.

### The external intel dossier

A research dossier at `~/Projects/Tubes/tubes-reverse-engineering-intel.md`
(produced by another agent) was cross-checked. Two genuinely new items:

- **The test tube is a LIFO stack** - atoms exit from the top, so speeding a
  source tube is how the player controls which atom ends up on top. This makes
  sense of the speed boost being a *targeting* tool rather than just haste.
- **Lose condition given as the beaker exceeding 5 rows**, which independently
  matches the measured 6 x 5 grid. Note the game's own slides give the lose
  condition as exceeding the drop allowance, so either both apply or the overflow
  claim is inferred.

Four things in it are wrong and should not be carried into the port:

1. **"VGA Mode 13h, direct framebuffer writes."** It is **Mode X** - unchained
   planar with multiple pages, stated on the game's own credits screen and forced
   on us by `.CSP` decoding, which needs the plane-switch sequence. The BIOS
   *reports* mode `0x13` because Mode X is reached by reprogramming out of it,
   which is probably the source of the error. A linear-framebuffer model would be
   the wrong architecture.
2. **LZEXE described as "header strip + inflate."** It is a custom LZ77 bitstream,
   not deflate; `docs/reversing-notes.md` has the grammar.
3. **"Element table - array of structs."** Measured as *parallel pointer tables* -
   a 19-entry ball table and a 19 x 6 fade table, adjacent in DGROUP.
4. **"Sound effects - if any (likely PC speaker or AdLib)."** Both: 8-bit 8 kHz
   PCM `.SFX` and OPL2 `.MUS`, with selectable `PCSOUND.DRV` / `SBSOUND.DRV`.

### Experiment 2: the dispenser path, observed

`PLAN.md` called this "the single genuine unknown left", and a whole session had
failed to find the code that moves atoms. It turned out not to need finding: the
atom array holds live x/y, so **sampling it over time is the trajectory**. Four
hundred reads without halting, and the route prints itself.

    spawn (303,186) -> bottom of an outer vertical tube at y=187
                    -> ascend at constant x
                    -> across the top, y ~ 0..3
                    -> descend into a play column

One trace carries the whole shape: `(58,95)` up to `(58,7)`, over the top along
`y=0` to `(97,0)`, then turning down at `(105,9)` - and 105 is the left play
column (107) less the 2 px the sprites are drawn at.

The x histogram lands on the tube artwork: 34, 58, 107, 179, 197, 246, 270 are
all positions already read from literal draw coordinates, plus **294** which is
outermost and had not been seen. Most-visited y values are 26, 13 and 0 - the top
band, matching the arcs drawn at y=13 and y=26.

So the account from play was right in every particular, and now has coordinates.

**What it does not establish, stated because it would be easy to over-read:** the
sampler is wall-clock paced at 50 ms, coarser than the game's frame rate, so
positions alias. Vertical runs show a clean `-4` in y between consecutive samples
over dozens of samples, which is tempting - but the arc shows deltas of 2, 4 and
7 px, which no single uniform step produces. 4 px/frame is **not** measured.
A game-synchronised sampler is needed for that.

**`1000:3a67` is not the frame update.** The stage table said it was. A
breakpoint at `3a73` re-fired **zero** times in four seconds of active play: it is
entered once and loops internally. That one measurement explains why Experiment 1
never got a usable frame offset - the static link is readable only at that single
entry, which happens while the difficulty menu is up and no atoms exist. Both the
notes and `PLAN.md` are corrected.

A locator bug worth remembering: the array base came back one record high,
because the finder returns the lowest record still parked at the spawn marker and
record 0 happened to be in flight. **The lowest parked record is not necessarily
record 0.** The true base is stable at `0x2419e` for this conf across runs, DOS
memory layout being deterministic.

Incidentally: the in-play HUD in this mode shows only `Chains` and `Drops`, with
no colour counters - consistent with the report that ball and match-type counters
appear only in certain modes, which is where the seven half-size balls at
`DS:0x200a` most likely live.

### Loading the save: waves, the save format, and the half-size balls

Loading the user's saved game answered three open questions at once, and started
with a mistake worth recording.

`TUBES.SAV` has 24 non-zero bytes in 960, and I looked at that sparsity and said
there was no level-6 save on disk. Wrong - it *was* the save, just a sparse
record. Reading it properly gave a Pascal ShortString "Stephen" at `0x1e0`, a u32
at `0x1ff`, and `06` at `0x206`. The lesson is a familiar one in this project: a
low byte count is not evidence of an empty file, and the fix was to decode rather
than to eyeball a hexdump.

Loading it under the debugger then **confirmed three fields against the running
game**, which is the strongest kind of check available for a save format - the
program renders the values:

    0x1e0  "Stephen"   -> slot list shows Stephen
    0x1ff  24500       -> in-play HUD shows 24500
    0x206  6           -> slot list shows Wave 6

and two more match a single sample: `0x207` = 11 against "You are allowed 11
drops", `0x20d` = 30 against "live through 30 atoms".

**The slot list is filtered by game mode.** Endurance showed five
`(UNAVAILABLE)` entries for a save that Wave Mode listed immediately. So the
first attempt looked like a missing save and was not.

**Wave briefings are the wave definition, in prose.** Wave 6: survive 30 atoms,
**Yellowium disabled for the wave and will not disappear**, 11 drops allowed. So
waves carry an objective, a disabled element, and their own drop allowance - which
refines the 9/6/3 triple to being the *Endurance* difficulty setting. A
disabled-but-still-spawning element is a mechanic the port has no concept of.

**The seven half-size balls are Wave-mode HUD counters** - the open question about
their draw site. Drawn top-left beneath `Chains`, with the count overlaid, and
absent in Endurance, which is why earlier captures never showed them.

And a correction inside the same session: I sampled the counter every 3.5 s,
saw cyan and magenta, and wrote it up as a two-colour blink with the semantics
unresolved. The user suspected rotation; re-sampling at 0.6 s showed **all seven
element colours** cycling. A sampling interval chosen for convenience aliased a
7-cycle into a 2-cycle - the same class of error as the wall-clock sampler in
Experiment 2, twice in one session.

That connects to Flashium, which cycles `1..7` for want of a sprite. Two things
rotating the same seven colours points at one shared rendering path, and gives a
concrete explanation for the type-8 pointer being caught on `BLUEBALL`: an index
mid-rotation. Testable by reading that pointer repeatedly.

### Confirming the type-8 rotation

Polled `DS:0x1da6 + 4*8` five hundred times with **type 3 as a control**. The
control held `BLUEBALL` for all 500 samples; type 8 took seven distinct values -
every colour sprite - cycling `RED -> GREN -> BLUE -> CYAN -> PURP -> YELW ->
PINK -> wrap`, i.e. table types 1..7 in order, with near-uniform dwell.

The control is the part that makes this worth anything. Without it, "the value
changed while I polled it" is equally consistent with a flaky read path, and this
session has already produced two aliasing artefacts from sampling intervals
chosen for convenience. A static pointer polled the same way, through the same
code, at the same rate, rules that out.

Rate is ~4 colours/second, full cycle ~1.7 s, but that is stated as approximate:
consistent runs of 3 samples at 83 ms could still be an alias. The *order* is not
in doubt.

**This settles a reading that had been carried for two sessions.** The playfield
notes described a loop that "increments a cell and wraps 8 back to 1" as a
colour-cycling effect, implying a settled Flashium's *cell* walks 1..7. Three
facts now say otherwise: fades are type-indexed (measured), a Flashium always
clears with `FFADE` (reported), and a cell cycling 1..7 could never produce a
type-8 fade lookup. So the cell holds **8** permanently and the flashing is
entirely in the sprite pointer. The loop in question is probably the code driving
that pointer, not a cell mutation.

Three independent observations - the always-`FFADE` behaviour, the type-indexed
fade table, and the measured pointer rotation - now agree, where before two of
them were in tension and I had invented a model to reconcile them. The model was
wrong and the measurement was cheap.

### The frame-synchronised sampler, and why the obvious design failed

Experiment 2 refused to claim a per-frame step size, correctly - the sampler was
wall-clock paced and slower than a frame. Fixing that properly took two attempts,
and the failed one is the more useful record.

**Attempt 1: breakpoint once per frame, so the guest waits.** The textbook
inversion. Finding the anchor looked easy - search the image for `mov dx,0x3da`
(retrace) and `mov dx,0x3d4` (CRTC) and a tight cluster turns up at image
`0x1331e`..`0x133ea`, plainly a Mode X setup followed by a page flip:

    mov  bx, [0x2376]      ; page index - the address the notes already had
    xor  bx, 1             ; toggle
    mov  [0x2376], bx
    shl  bx, 1
    mov  ax, [bx+0x2366]   ; that page's start address

Textbook, corroborated by an address we already knew, and **never executed**.
Zero breakpoint hits: it belongs to code this game does not use. A diagnostic
that armed every site referencing `0x2376` plus every retrace site found exactly
two that fire, and the hot one (`0x11aa2`, a per-sprite dirty-rect save) fires
**thousands of times per frame** - servicing it over RSP never completed a single
frame in five minutes. There was no cheap once-per-frame anchor to be had.

Also worth recording: I read `lcall 1321:014f` near that anchor as pointing into
"a second graphics unit" and expected the real flip there. Wrong - segment
`0x1321` just addresses part of CODE_0, and image `0x335f` is ordinary game code.
The segment arithmetic was right; the inference about unit boundaries was not.

**Attempt 2: slow the guest instead.** Cycles change how fast the guest computes,
never what it computes - so game logic per frame is untouched and only wall-clock
per frame changes. DOSBox-X drops cycles on Ctrl+F11, which QMP can send. 42
presses from `cycles=fixed 20000` gave **5.3 reads per frame**, comfortably
oversampled, with frame boundaries read off the page index changing.

**Result: atoms move 4 px per frame**, in both axes - `dy=-4` 503 times against
`-8` 87 times, and the `8`s are exactly `2x4`, occurring at about the rate a
boundary is missed at 5.3 reads/frame. Not a second step size. Larger jumps are
records being reassigned to another tube column, and a few absurd values are torn
reads mid-update.

That confirms the `-4` Experiment 2 saw and declined to claim. The 50 ms interval
had happened to sit near one frame, so the value was right by luck - the refusal
to claim it was still correct, and it cost nothing to verify properly.

Contrast worth carrying into the port: the **test tube** moves 6 px/frame from
static analysis, atoms move 4. Different rates.

### Chasing `dy = +18`: it is the descent speed

Left flagged as "real but unexplained" - 62 occurrences, not a multiple of the
4 px step, not the 13 px row pitch. The fix was methodological rather than
clever: a histogram of deltas discards *where* each transition happened, so it
cannot answer this. Re-running with the full context of every change - record,
from-position, to-position - answered it on the first run.

Every `+18` and `+36` has **`dx = 0`** and sits at a **play column**. One
descent, per frame:

    x=125:  46 -> 68 -> 68 -> 104 -> 122 -> 140 -> 140 -> 158 -> 176 -> 176

So atoms have **two speeds**: 4 px/frame travelling the tube network, and
**18 px/frame** falling down a play column. `+36` is two steps across a missed
boundary; `+29` and `+11` are compound and partial steps where an atom enters
the descent around y = 48.

`68` is the top lane already on record for the tube struct, and the descent runs
68, 86, 104, 122, 140, 158, 176 - 18 px from wherever the atom entered, so the
lattice is per-atom. It is **not** the beaker's 13 px row pitch: falling is free
motion, and settling into a grid cell is a separate step.

Left explicitly unsettled: the frames where nothing moves. That could be a real
cadence - 18 px every other frame, averaging 9 - or a sampling artefact at
~5 reads/frame. Saying which needs more oversampling or a genuine per-frame
breakpoint, and it is not worth guessing.

Three different speeds in one game - 4 in the tubes, 18 falling, 6 for the test
tube - which is enough on its own to retire the single `fallSpeed` constant in
`src/game.cpp`.

### The speed table, and a flaw in my own experiment

The user spot-checked their own DOSBox session and reported that a missed atom's
fall looks different from a boosted atom in the tube. Checking that exposed a
flaw in the previous measurement - in the experiment, not the arithmetic.

**No input was ever sent in any run.** The test tube therefore never moved, never
caught anything, and every descent I sampled was an atom that had already been
missed. Labelling 18 px/frame "the descent" was over-reading a result produced
under exactly one, unrepresentative condition.

Re-ran with input as the controlled variable, and hit a second problem first: 95 s
with nothing held followed by 95 s with Down held gave a second phase of 15
frames and **zero motion**. With no input every atom is missed, wave 6 allows 11
drops, and the wave had simply ended - phase B was sampling a game-over screen.
When the control condition itself ends the game, long phases cannot work.
Interleaving six short blocks fixed it and controls for progression too.

Result, upward steps inside the tube network:

    dy        no input    Down held
    -4          188          66
    -8           11          32
    -18          10          29
    -36           0          10

plus `dx = -18` appearing only with Down held. So the boost is **18 px/frame**
against a normal **4**, in whatever direction the atom travels.

The detail worth the whole run: **-4 does not vanish under Down, it drops to
66**. A global speed-up would eliminate it. Its persistence is precisely what the
Instructions say - "any atoms in the tube *directly above the test tube*" - so
the measurement independently confirms the wording, which is a much stronger
result than the raw number.

Below the mouth, 18 dominates in both conditions, consistent with the boost not
reaching atoms already past the tube.

Left open honestly: the user's perceived difference between a fall and a boosted
atom is **not** step size - both are 18. It may be direction and path, or a
distinction this experiment cannot make, because with no left/right input the
tube sits in one column and the `y > 68` band mixes atoms descending *into* the
tube with atoms falling *past* it. Separating them needs the tube deliberately
parked under a known column.

### Caught vs missed: the difference is continuity, not speed

Parked the test tube at the leftmost column by holding Left, confirmed by screen
capture, then classified every atom by whether its x matched the tube's column.

Over 379 frames, frames near the mouth (y 40..75) against frames deep below it
(y >= 120):

    x=107 (tube column)   226 near     0 deep
    x=125                   4 near   303 deep
    x=143                   8 near   197 deep
    x=161                   8 near   205 deep
    x=179                   8 near   159 deep
    x=197                   4 near   276 deep

Total contrast, and the absence is the signal: in the tube's column atoms arrive
at the mouth, **stop**, and accumulate for hundreds of frames without ever
appearing below y=120. Everywhere else they cross the mouth in a handful of
frames and plunge to the beaker floor.

So the reported impression that a missed atom looks different from a boosted one
does **not** need a fourth speed. Both descend at 18 px/frame; what differs is
that a caught atom decelerates to a halt at the mouth while a missed one
continues without interruption to the beaker. Continuity, not rate.

Two things fell out of it.

**Caught atoms may leave the array.** They never appear below y=120, yet the tube
hangs 69..134 and holds five 13 px atoms, which would fill it to 134. If caught
atoms were still tracked in the 12-record array at their drawn positions, some
would have to show up below 120. They do not - suggesting an atom leaves the
array on capture and the tube's contents live elsewhere, which also fits the tube
being a LIFO stack. Not proven.

**The test tube struct was not found.** Diffing a +/-1 KiB window around the
array while pressing Right turned up no word holding a waypoint target
(104..194) or stepping by 6. `PLAN.md` puts it at `parent - 0x16a`, seven bytes
below the array base and well inside that window. So either the position is not
stored as one of those values or the struct is elsewhere; the static reading is
still unconfirmed against a running game, and is now marked as such.

### Two corrections: misses are lost, and slots are overwritten

The user corrected a mechanic I had quietly interpreted rather than measured. I
wrote that missed atoms "plunge to the beaker floor" - they do not. A missed atom
is **lost**; that is a *drop*, which is what the drop allowance counts. The beaker
is filled only by catching atoms in the test tube and tipping them in with Button
A. Nothing reaches the beaker without going through the tube.

The phrasing was the tell: "plunge to the beaker floor" is a description of what
I assumed was happening, sitting in a document that otherwise separates measured
from inferred line by line. The data said only that y reached 177..186.

That correction predicted something testable. If a missed atom is lost, its
record must become reusable - and the obvious mechanism was a reset to the spawn
marker `(303, 186)` all twelve initialise to. Every previous analysis had
**filtered spawn-marker transitions out as uninteresting**, so exactly the
evidence needed had been thrown away every time.

Measured: over 382 frames, **zero** transitions into the marker and **zero** out
of it. The hypothesis is dead.

The surviving model, which also explains the large positional jumps already in
the data: the twelve records are a **pool of slots**; `(303, 186)` is the initial
value of a slot never yet used; a slot is reused by **overwriting x and y
directly**; and a lost atom's record just retains its final position until
reallocated. The `+/-49`, `+/-91`, `+/-133` jumps are allocation, not movement.

One practical consequence: the spawn-marker signature that locates the array
works because early in a wave most slots are untouched, and becomes **less**
reliable the longer a wave runs. Worth knowing before leaning on it late in a
session.

### The beaker grid, located and proven

The last core data structure. A before/after diff around the tipping action
failed first - the region churns every frame and the candidate list filled with
atom-record bytes - even with a volatile-byte control pass. Diffing was the wrong
instrument.

**The shape was the signature.** With the tube parked under the leftmost column
every tipped atom lands in column 1, so in a row-major stride-6 array every
filled cell must sit at an offset that is a multiple of 6. Scanning for 30-byte
windows that are zero except for bytes at offsets `% 6 == 0` holding 1..19 cut
straight through the noise.

Then the layout was **proven rather than assumed**, because a single column of
values cannot distinguish row-major from column-major. Tip into column 1, move
one column right, tip again:

    column 1 filled  0x2432c
    column 2 filled  0x2432d     <- exactly +1, so row-major

with two stacked atoms landing 6 apart. Grid base `0x24314`, 30 bytes, 6 per row,
bottom row at `+24`, cells holding ball types.

**A settled cell was caught holding 8.** That was the model recorded earlier -
the cell holds 8 permanently while the flashing lives in the sprite pointer -
which had been flagged as unproven because it was inferred from three facts
agreeing rather than observed. Now read directly out of the grid. It also
confirms cell values are not limited to 1..7.

**One claim did not survive.** `PLAN.md` called the beaker three parallel arrays
of stride 6. The 30 bytes after the grid hold a Pascal string `"CRFADE6.CSP"`, a
resource name, not a second grid. The 60 bytes before are all zero, which is
consistent with two empty companion arrays but is equally consistent with
nothing - zeroes are not evidence. Marked open, with a concrete test: settle a
special and see whether a flag appears alongside its type.

### The test tube struct, found by reversibility

The earlier attempt diffed +/-1 KiB around the atom array while nudging the tube
two columns and found nothing. Three things were wrong with it: the signal was
small, the scan read 16-bit words when the fields turn out to be **bytes**, and
there was no way to separate the tube from everything else that changes.

What worked was a **reversibility control**. Park left, park right, park left
again, and keep only bytes that changed *and returned to their original value*.
Counters, RNG state and score never go back, so that single constraint removed
essentially all the noise. Three bytes survived, two of them adjacent and holding
an index/target pair matching the static `+0x1e`/`+0x1f` layout, which placed the
base at `0x245d0`.

Then - the part that makes it evidence rather than a fit - it was **tested
against a field it had not been fitted to**. `+0x04`, the state, was driven
deliberately: idle gave 0, hold Left gave 1, hold Right gave 2, Button A gave 3.
Exactly the documented machine, from a struct located without reference to it.

The same run confirmed x stepping by 6 (the rail speed), the six waypoint targets
104..194 with the index tracking 1..6, and - independently for the third time -
that Button A is Left Ctrl.

**Two corrections.** `PLAN.md` puts the atoms at `parent - 0x163` and the tube at
`parent - 0x16a`, seven bytes apart with the tube below. Measured, they are
`0x432` apart with the tube **above**. The static offsets are right about the
fields *within* each structure and wrong about the relationship between them.

And it is probably not a self-contained record: the bytes read from `0x245d0`
include the **score** as a u32 at `+0x17`, reading 24500 and matching both the
HUD and the save file. A tube record would not contain the score, so these are
adjacent locals of `1000:9e53` rather than a struct - which is what a Pascal
nested-procedure frame looks like from the outside.

Useful by-product: the live score is at `0x245e7`.

### MYSTBALL and the crystal: a negative result and a retraction

Four runs, no answer. Worth recording properly rather than quietly dropping.

**The good part.** Type injection works: writing a type byte into an atom
record's `+0x0b` takes effect and the record flies on with the new type, so rare
atoms no longer have to be waited for. And it produced one real finding - an
injected type-19 atom that fell past the tube had its type byte go `19 -> 0`, so
**type 0 marks a free slot**. That completes the lifecycle: slots are not
recycled through the spawn marker, the position is left where the atom died, and
the type is what gets cleared.

**The retraction.** An earlier run reported the value 19 appearing at `0x24546`
and I read it as a caught `?` ball entering the tube's storage. Watching that
address over time shows it counting down - 20, 19, 18, 17, 16, 15 - and
`0x245bc` counting 9 down to 1. Both are **counters**, and my detector ("any byte
holding 12..19 that was not there before") caught one passing through the type
range. The instrument could not tell a value from a trend, which is exactly the
kind of filter this project has been burned by before.

**Why it stalled.** Catching an injected atom needs the atom and tube in the same
column simultaneously. An atom descends at 18 px/frame, so the usable window is a
couple of seconds; steering the tube costs about a second per column; parking
first means the chosen column may get no atom at all; and the wave ends after 11
drops, which with a parked tube arrives inside a minute. Two runs finished with
nothing moving and the grid reading garbage because the locals had been reused.

**The fix, not yet done:** freeze the drop counter. The rig can write memory, the
drops value is on the HUD so a diff-on-change hunt would find it quickly, and
holding it constant removes the time limit that makes all of this a race. That
turns catching an injected atom from luck into patience, and it would unblock the
crystal too.

`BUBBLE` was not reached.

### Level-warping the save: the wave table, the crystal, and MARKER

The crystal hunt was failing because it was aimed at the wrong wave. Waves have
different objectives and constraints, and the crystal is gated to late ones - no
amount of playing wave 6 would ever have produced it.

The way in was already measured: `TUBES.SAV` holds the wave number at `0x206`.
Editing that byte and loading warps to the wave, and **there is no checksum** -
the slot list reads back "Wave 20" and the briefing that follows is that wave's.
Validated on consecutive waves (10, 11, 12 all differ), so the byte really does
select the definition rather than just relabel it.

That turns the whole wave table into something readable without playing, and it
answered three open questions at once.

**The crystal is the "Mischief Crystal"** (wave 50): *"Using Anti-Matter remove
all the Mischief Crystals that are contaminating the beaker."* So it is
destroyed with **AntiMatter, not by matching** - which resolves the oddity of
type 18 carrying a full `CRFADE` family while never forming a chain. It also
corrects an earlier note of mine that called types 1-10 plus 18 "the matchable
set": a fade family means *clearable*, which is not the same as *matchable*.

**`MARKER` is solved** (wave 20): *"Form chains to remove marked atoms from the
beaker. Marked Atoms: 3"*, illustrated with the red X that `MARKER.CSP` draws. It
overlays beaker atoms a wave objective requires clearing. Previously filed as
"unidentified; a cursor or a target indicator".

**The half-size balls are the Task Display** (wave 25): "the colour specified in
the Task Display". So they show the *required colour* for the current task -
which also explains why wave 6's display cycled all seven colours: that wave has
no required colour.

Objective kinds seen across eight waves: survive N atoms; form N chains of a
given orientation and colour; form N chains of a colour that changes per task;
remove N marked atoms; remove N crystals with AntiMatter. Modifiers seen: an
element disabled for the wave, beaker atoms morphing every 45 s, atoms hidden
until they leave a tube. Every sampled wave allowed 11 drops.

Worth noting the shape of this: the failing approach was *more* sophisticated -
type injection, tube steering, frame-accurate sampling - and the thing that
worked was editing one byte in a save file. The expensive machinery was aimed at
a question the game answers on a slide.

### `BUBBLE` identified: the intro cutscene

The last unattributed sound, and it is not a gameplay effect at all - it is the
intro cutscene, where the beaker foams and bubbles as the elements go unstable.

Everything measurable had pointed away from gameplay without being able to say
what it *was*: 0.865 s is far longer than the blips used for actions, the
internal name is "Bubbles" plural, and it was the only sound in the set with no
counterpart among the fade families, UI actions, collisions or splash screens.
The blackboard cutscene already owned `CLAP`, `NOOOO` and `WHATTHE`, and its
resources are `WRITE0..9.GFX` plus `EXPLOD1..4.GFX` - an explosion sequence,
which is that scene exactly.

All 24 `.SFX` now have an owner: 11 match/clear (one per fade family), 6 gameplay
events, 4 cutscene, 3 splash.

Recognition settled it in one line, where analysis had only narrowed it - the
same pattern as the `MULTBALL`, `DROP` and missed-atom corrections. Play memory
keeps proving unreliable as *evidence* and excellent at *pointing*, which is the
reason these notes keep reported and measured claims on separate lines rather
than merging them.

#### `BUBBLE` corroborated from the binary

The cutscene identification came from recognition, so it was worth checking
against the image rather than filing on one source. Resource name strings turn
out to be emitted in **contiguous blocks**, one per unit, and the blocks line up
with the grouping: the 11 fades plus `DROP`/`HITATOM`/`HITGLASS`/`SELECT` at
`0x9d53`, `CLAP`/`SLIDE`/`SWITCH` at `0xaa85`, **`WHATTHE`/`NOOOO`/`BUBBLE` at
`0xc5fc`**, and `WOOSH`/`LIGHTN`/`ABSMAGIC` at `0x11809`.

`BUBBLE` sitting directly beside two known cutscene reactions, with all three
referenced from segments inside CODE_1 - the unit holding the blackboard cutscene
- is independent structural support for what was a memory.

Recorded with its caveat: a string's position shows the unit that emitted it, not
every unit that uses it. `SELECT` is in the gameplay block while the program map
has the title/menu referencing it, because a far pointer crosses units freely. So
adjacency is good evidence of grouping and poor evidence of exclusive ownership.

### The atom record layout is largely wrong, and it voids the MYSTBALL work

The wave-30 test returned "type 19 seen 0 times" in both the test and the
control - a clean negative. The line above the verdict is what mattered though:
"types seen in transit: Redium=141", i.e. **every atom in the game reading as
type 1**, in both waves. A field that never varies cannot be the colour, and the
beaker grid plainly holds 2, 3, 4 and 8 in settled cells.

Dumping whole records and tallying every offset over 2242 in-transit samples
confirms `+0x00` = x and `+0x02` = y, and contradicts the rest. `+0x0b` only ever
holds 0 or 1. `+0x14`..`+0x19`, the claimed saved-position slots, are **always
zero**. There are undocumented x-like fields at `+0x06`, `+0x0a`, `+0x1a` and
y-like at `+0x04`, `+0x08` - plausibly the saved pairs, stored as (y, x) at
different offsets than the static read gave.

**The MYSTBALL injection experiments are void.** They wrote 19 to `+0x0b` and
read the same byte back to confirm the injection "worked". Reading back a byte
you just wrote proves the write landed and nothing else - there was never a check
that the *game* reads that field. So no atom was ever turned into a `?` ball, and
the wave-30 negative means only that nothing was set to 19 anywhere the game
looks. The hidden-atom hypothesis is **untested, not disproved**.

Worth being precise about the mistake, because it is not "the notes were wrong".
The static read of a single draw site was reasonable evidence. The error was
building three experiments on it without once checking the field against
observation - and then treating a read-back of my own write as confirmation. A
control was available the whole time and cost nothing: does this field ever hold
a value the grid also holds?

What survives is everything resting on `+0x00`/`+0x02` - the dispenser path, the
speed table, caught vs missed, the slot lifecycle - and everything measured
independently: the grid, the tube struct, the sprite tables, the sounds, the wave
vocabulary. The type field itself is now a correlation experiment: follow one
atom's whole record through a catch and a tip, and see which offset's value turns
up in the grid cell. `+0x0e` (spans 1..7) and `+0x11` (reaches 12) are the
candidates.

## 2026-07-26 — Session 4

Drove the game live under the debugger for a full play session, and let play
settle three questions that static analysis and automated sweeps had each got
wrong.

### The game "stuck paused" — and what it actually was

Reported symptom: music playing, game frozen, unpausable. The obvious readings
were all wrong, and were checked rather than assumed:

- QMP `query-status` returned `running`, `emulator-paused: false` — not an
  emulator pause;
- BDA `0040:0018` bit 3 clear — not the BIOS Pause/hold loop;
- BDA `0040:0017` = `0x10` — Scroll Lock only, no latched Ctrl/Alt;
- keyboard buffer head == tail — nothing jammed;
- `0040:006c` advancing 18 ticks/s — IRQ0 alive.

So the emulator was executing and the game was not. The 390-second freeze
covered exactly the lifetime of a sampling logger reading 2.3 KB every 0.35 s,
and killing it resumed play instantly. **Correlation only** — the A/B test has
not been run — but continuous loggers are now kept light on that basis, and the
suspicion is written up in `docs/debug-rig.md`.

That freeze also explains the session's most expensive dead end. The
drop-counter hunt scanned a window that *contained* the right address and
reported nothing, because the game was frozen for its entire run and no drop was
ever lost. Another search that could not have found what it was looking for —
the fourth this project has logged.

### The live drop counter, and the end of the drops confusion

`0x245bc`, u8. Found by scanning DGROUP for the HUD's value and keeping only
bytes that **decrement by one**, then confirmed against the display: the script
read 8 while the HUD showed `8 Drops`. Predicted-then-observed, not the
read-back-your-own-write mistake that voided the MYSTBALL work.

It had already been recorded — the parked-tube run logged `0x245bc` counting
`9 -> 1` and dismissed it as an unidentified counter. With the tube parked every
atom missed, which is precisely what the drop counter must do. Right
observation, missing label.

Every mechanism is now measured separately, and the long-standing contradiction
between the binary's `9/6/3` and the 11 in every save is resolved — they were
never competing claims (the mapping is from playing all three settings; a
`11 -> 3` session start seen in the log turned out to be **attract mode**, not a
new game, and measures the demo rather than the seed):

    new game         drops = 9 / 6 / 3 by difficulty (101 / 201 / 301)
    a miss           -1
    a Bonus atom     +1
    clearing a wave  unchanged
    the briefing     reports the current value, it does not set it

The wave-clear case is the one play had wrong: the repeated return to exactly 11
was attributed to clearing, but the log shows the `+1`s landing mid-wave twelve
seconds apart with a populated beaker — Bonus atoms. The count read 8 on both
sides of the 52/53 boundary, and wave 53's briefing then announced "8 drops
allocated". That is also the confirming test the notes had queued (edit `0x207`,
see whether the briefing echoes it), delivered by play without tampering.

A cap at 11 was floated here and is **withdrawn**: gains stopping at 11 twice
was sampling, and play reports starting a wave 6 save at 12.

What made the boundary readable was detecting it *independently*, by the beaker
emptying rather than by the drop count doing something interesting. A detector
keyed to the change it hopes to see cannot report that change's absence.

### `TUBES.SAV` is two banks of five slots

A second save, written under a different name, landed `0x50` after the first
with every field repeating at that stride. So a slot is `0x50` bytes and the
960-byte file is two `0x1e0` banks — one per game mode — of five slots plus a
trailer.

This is not arithmetic fitted to a guess: it accounts for the two bytes the old
notes listed as unexplained. `0x1bb` and `0x39b` are both at `+0x2b` of their
bank's trailer. And it matches the menu, which lists five slots and showed five
`(UNAVAILABLE)` entries under Endurance for a save Wave Mode displayed at once —
bank 0 Endurance and empty, bank 1 Wave with both saves.

Consequence: the `0x206` wave byte every level-warp sweep edited is **bank 1,
slot 0's** field, not a global. It only ever worked because one slot was
occupied.

### Smaller findings

- Between waves: a `Wave <n> Stats` blackboard with **two** chain counters,
  per-wave and cumulative, plus score and a `High Score!` line.
- Game Options holds only Music / Sound FX / Input Device — difficulty is chosen
  on the Start Game path.
- The main menu wraps: from index 2, six `Up` presses landed on index 4.
- Running a second DOSBox on its own ports, drive and capture directory lets
  automation run while someone plays, avoiding `DOSBoxInstance`'s
  `pkill -9 -f dosbox-x`. Used today with both instances surviving.

## 2026-07-26 — Session 5 (autonomous)

Ran the game agentically through attract mode, probed the remaining menus, and
landed the measured rules in the port.

### Attract mode as the rig's player

`DEMO.SCR` replays through the normal game loop, so the demo is a full play
session with nobody at the keyboard. Determinism was verified first, since it
is the premise for everything else and the header u32 is only *inferred* to be
an RNG seed: two cold-boot runs produced identical ordered grid transitions,
identical score sequences and identical drop sequences.

With that established, `+0x0b` was settled as the atom type field - 96.2% over
79 settle events, against 7.6% for the next best offset. The test predicted a
*different* structure (does the beaker cell take the value this offset held?)
rather than checking value ranges, which several offsets satisfy. The earlier
observation that appeared to refute `+0x0b` had used array base `0x2419e`, six
bytes early. So the original static reading was right, and the MYSTBALL
injection technique is sound after all - those particular runs stay void only
because they wrote at the wrong base.

Also measured: the score **ramps** toward its award in roughly sixths rather
than jumping, in the game's own variable rather than a display layer.

### Two failures worth more than the successes

**Ctrl+F11 via QMP does nothing here.** An adaptive harness measured the state
rate after each batch of presses and it never trended down. Two runs were
aliased before this surfaced - and the aliased run cheerfully produced a smear
of "per-frame" deltas (-16, +36, +18, +27...) that were differences across an
unknown number of frames. The technique is recorded as not working so it is not
retried.

**Both frame-counter candidates were filter artefacts.** A scan for
strictly-increasing fields across 7 snapshots, over ~28,000 candidate offsets,
will throw up hits by chance; checking the full series killed both (`0x24dc0`
reads 2568, 2319, 1553, 513 across one trace). This project's standing rule is
"when a search comes back empty, suspect the search" - the same scepticism is
owed to a search that comes back **full**, and the tell was immediate: a frame
counter that disagrees with itself is not a finding.

### Menus

The flow is Main menu -> Start Game -> Game Mode -> **DIFFICULTY (Tubes 101 /
201 / 301)** -> play. Difficulty is *not* in Game Options, which holds only
music, sound effects and input device. High Scores is headed "Endurance Mode
High Scores", so each mode keeps its own table - corroborating the two-bank
`TUBES.SAV` layout from an entirely different direction.

### The port

Atom types now use the original's numbers, which matters beyond tidiness: a
settled beaker cell holds the type byte directly, so a captured trace can be
compared against this engine's board with no translation table. The old order
had green/blue and yellow/cyan transposed.

Drops were modelled backwards - the port counted misses up to a limit, where
the original seeds a pool and counts down. Tube capacity is a flat 5, per the
in-game Instructions, retiring the 5/3/2 guess taken from sprite heights.
Scoring uses the measured 250 unit, with the board now reporting run
orientation, and `awardForRun()` states plainly that the two measured awards
are confounded between length and orientation rather than hiding it behind a
formula.

19/19 tests pass and the headless render was checked visually - the atom
renumbering would have silently swapped every colour otherwise, and only
looking at the output would catch it.

### Implementation, same session

The measured rules went into the port, and one of them arrived by correcting
myself. `awardForRun()` first fitted a curve `250*(len-2)^2` through the two
live measurements (a vertical 3 paying 250, a diagonal 4 paying 1000) and noted
that length and orientation were confounded. They are not confounded at all -
the game's Detailed Instructions state the table outright, **Vertical 250 /
Horizontal 500 / Diagonal 1000**, award by orientation with no length scaling.
The curve matched both points by coincidence. The evidence had been sitting in
`reversing-notes.md` since the Instructions were transcribed.

That is the project's oldest lesson in a new place: read the other consumer of
the data rather than modelling the first one.

The dispenser path is the headline change - "atoms currently just fall" has
been in `game.h` since the beginning. It now follows the measured route and was
verified by tracing the simulation, not just by eye: rise holds x constant at
270 while y runs 159 -> 63, cross runs x 262 -> 210 along y=0, and the catch
lands at (197,72), where 197 is play column 5 and 72 is one 18 px step past the
measured mouth lane at 68.

Moving to a fixed frame step introduced a bug worth keeping: the caller updates
at 60 Hz, the step runs at ~18 Hz, so most calls advance zero frames - but
`prevButtons_` updated on every call, so a press arriving on a zero-step call
had its edge consumed and never reached the simulation. The scripted player
caught nothing and burned eight of nine drops. The symptom was a plausible
"the AI just plays badly", which is exactly the kind of result that gets
explained away rather than investigated.

## 2026-07-26 — Session 6: back to transliteration

Pivoted from black-box reconstruction to reading the code, after the user
observed that the port was being *re-implemented* rather than ported and that
this would be lossy. `CLAUDE.md` now carries a prime directive making the order
of authority explicit: decompiled code settles a rule, the game's own text
corroborates, live measurement locates and validates but never derives.

The pivot paid for itself immediately. Everything below came out of the code in
one sitting, and most of it was **invisible to measurement in principle** - not
merely missed.

### Structures

- **Gameplay state is on the stack, not in DGROUP.** `FindScalarRefs` returned
  nothing for the grid, score and drops as DS offsets, and could not have
  succeeded: `DS = 0x1fa9`, `SS = 0x2294`, and every gameplay address is above
  `0x22940`. The grid is 30 bytes at `SS:0x19d4` with a sampled `BP` of
  `0x19f2` - it is `[BP-0x1e]`. Drops, tube and score live in the *enclosing*
  frame, confirming from data that `3a67` is a nested procedure sharing
  `9e53`'s locals.
- **The beaker is three parallel `array[1..5, 1..6] of byte` planes**, not one.
  The `+7` bias on the base is the fingerprint of Turbo Pascal 1-based
  two-dimensional indexing.
- **A cell holds `type + 19 * fadeFrame`**, not a type. Plane B flags the cell
  as animating and plane A's value climbs by 19 a frame. This closes a loop with
  a formula derived years earlier: `0x1df6 + 76*(frame-1) + 4*(type-1)` expands
  exactly to `0x1da6 + 4*(type + 19*frame)`, so one lookup draws settled and
  fading atoms alike and the drawing code never branches.
- **Plane C is an overlay** - `MARKER.CSP` drawn over flagged cells when
  `DS:0x1d4e == 6`. Those are the red X marks seen on beaker balls in play.

### Rendering

- The tube network is **not a backdrop**. It is assembled from segment sprites
  in layered passes, and the atoms are drawn *between* those passes so the solid
  pieces overpaint them. That is what makes the tubes read as hollow with atoms
  visibly inside and tubes overlapping - and it is unreproducible by
  compositing one foreground image.
- Sprite slots were named by **spacing fingerprint**: the gaps between the 16
  resource-name strings are known exactly, and only one position in the image
  holds printable Pascal strings at every one of them.
- `BEAKER.CSP` is drawn **last**, after the settled atoms, so the glass front
  overlaps the balls. The port had this inverted - same sprite, same
  coordinates, wrong order, and no behavioural test could ever have caught it.
- Rendering is **dirty-rect over two Mode X pages**; the 16 x 13 atom cell is
  the unit of redraw.

### MYSTBALL

Settled after three failed experiments, by two lines of code: the draw is
`ball[type]` or `MYSTBALL` chosen by a flag, at the same coordinates. It is a
**rendering state**, so scanning the atom array for type 19 could never have
found it, and injecting 19 into a type byte could not have produced it.

### One retraction

`DS:0x1d4e` was written up as selecting the tube layout. It does not - it gates
the overlay. The error came from grouping draw calls by line range between
conditions; a brace-aware grouper showed the furniture is drawn unconditionally.
The claim was labelled unverified when written, which is the only reason it was
caught quickly. Notably the error pointed towards *more* work than the truth
required, and nothing would have failed to reveal it.

### Still open

The movement code. The tube x positions appear in `3a67` only inside draw calls,
never in a comparison, so the route is not hard-coded position tests there.
`src/game.cpp`'s three-leg path remains an explicit placeholder.

### Session 7: the pixel-diff harness

Built the thing that was missing all along - a way to compare the port against
the original that cannot be argued with.

The harness captures the original's frame **together with the state that
produced it**, puts the port into that exact state via a new `--render-state`,
and diffs per pixel. Every earlier comparison was between two runs that merely
looked alike, which cannot separate a rendering bug from a divergence in the
simulation, and that ambiguity produced two wrong conclusions in a single
session.

Freezing the frame uses the game's own **Pause** key. Halting via GDB does not
work - QMP `screendump` times out because the emulator's main loop is blocked -
so halting and capturing are mutually exclusive. Pause freezes the game loop
while the emulator keeps running. Verified directly: state unchanged across
1.5 s paused, changing across 1.0 s running, screendump answered in both.

Two calibrations mattered more than the harness:

- the backdrop is excluded, being random and animated over with `STAR1..4`;
- greys compare with a tolerance of 6, because DOSBox expands the 6-bit DAC
  with `v<<2` and the port with `v*255/63`, so **every** grey lands one unit
  apart. Without it the harness read 34% and would have started a hunt for a
  palette bug that does not exist. With it, 4.6%.

It found the test tube misplaced and a few missing vertical pieces - and then
immediately caught a bad fix: shifting the tube by the measured 6 px made the
diff *worse*, 4.6% to 8.5%, because the sprite has more than one wall. Reverted.
That is the harness doing its job on its first day.

Also in this session, before the harness: the network topology measured per
column (feed x, lane y, destination x), the atom router transliterated from
`1000:0f80`, and one full round trip - GAMEFG declared the whole network, the
passes removed, the arcs lost their vertical walls, the change withdrawn. The
withdrawal is recorded rather than quietly reverted, because the reasoning that
produced it was a comparison at a crop where the deciding difference was
invisible.

## 2026-07-27 — Session 8: the layering, settled by disassembly

The question was how tubes, atoms, the test tube and the beaker layer in the
frame loop. It turned out to rest on two graphics primitives and one sprite
field, none of which was in evidence, and answering it took the pixel diff from
**4.42% to 0.02%** on a clean capture.

### Method: stop reading the decompiler for structure

`1000:3a67` is a nested Pascal procedure sharing `1000:9e53`'s frame through a
static link, and Ghidra folds both frames into one set of `local_XXX` names, so
two different bases print as the same expression. That is what produced the
earlier reading of one 28-byte array as both the atom pool and the tube's
contents. Added `ghidra_scripts/DisasmRange.java`; the listing distinguishes
`[BP-n]` from `SS:[DI-n]` and every structural claim this session came from it.

Also added `ghidra_scripts/DumpBytes.java`, which read the four geometry tables
straight out of DGROUP and confirmed column 1's feed x - carried as "inferred
from the symmetry" for two sessions because no atom used that column while
sampling.

### What the layering actually is

* **Six atoms, one per column**, not one. The array index *is* the column: the
  spawn indexes by the column it rolled. Records 7..12 are a separate pool for
  atoms falling out of the tube into the beaker.
* The six draw sites interleave with the furniture as **1/6, then 2/5, then
  3/4** - the network's mirror symmetry, outermost tubes deepest.
* `2321:0874` re-stamps a 16x13 box of **GAMEFG** over each atom after drawing
  it. Not a snapshot of the composed screen: the session setup blits GAMEFG
  with `2321:0711` and registers *that buffer* at `DS:0x238e`.

The last point is the whole look of the game. GAMEFG is solid along the long
horizontal runs between the arcs and transparent inside the feed tubes, so an
atom crossing lane 13 near x=217 is clipped by the tube walls while the same
atom rising at x=246 is not - there the walls come from furniture sprites drawn
before it. Both were measured against the original and both now reproduce.

A previous session had painted a guessed `TUBEH` or `TUBEV` over each atom,
with special cases for bends and descents. That was reaching for this, and it
is withdrawn: the real thing needs no cases, because it *is* the artwork.

### The single biggest fix was a sprite field we had dismissed

`.CSP` sprites carry a placement offset. Over all 108 sprites the minimum is
`(128, -2)` and 84 sit exactly there - that is the shared base, and the excess
is real. `screen.h` had it documented as "useful provenance but meaningless as
a placement offset" and dropped it.

It is worth 6 to 11 pixels on `TESTUBES`, `TUBEVS`, `TUBEVLS` and `TUBEVRS`,
which is precisely what `PLAN.md` had been calling "the missing vertical pieces
in the arcs" and "sweep the test tube x offset". Neither needed sweeping.

The proof it is not a decoder artefact is `GFADE1..6`, whose origin walks
`+0,+3` `+0,+6` `+2,+6` `+4,+6` `+7,+5` `+7,+5` as the sprite shrinks. A
contracting animation must move its origin inward to stay centred; nothing else
produces a monotone walk.

### Placeholders retired on the way past

Reading the spawn and the difficulty block closed several items that `PLAN.md`
listed as invented or unmeasured:

* **spawn period** 70/60/50 frames by difficulty, was `29`, an outright guess;
* **network velocity** 2/3/4 px/frame by difficulty, both stepping every
  fifteenth wave;
* **the type distribution**, including that slot 11 is the whole special family
  sharing one eleventh of the roll, and that Flashium is in the ordinary pool;
* **the test tube's slide**, a flat 6 px/frame with input accepted only when
  parked - the port stepped a whole column every three frames, so it was never
  between stops and a capture that caught the original mid-slide could not be
  matched;
* **the Down/B boost**, `0x480` = 9 px/frame, applied to one atom and never
  reset. The velocity field has exactly three writers and **none of them fires
  when an atom starts descending**, so the port's inferred 18 px/frame descent
  is withdrawn.

### The harness had an off-by-one record

`capture_frame.py` read the atom array at `0x241A4`, which is `array[2]`. The
tell was that the emitted index ran exactly two behind each record's own column
field on every sample, and the spawn requires the two to agree. Base is
`0x24188`. Nothing in the old harness could have caught this, because it never
compared the two - it only started mattering once the port keyed a draw slot
off the index.

`capture_frame.py` now also records the tube's exact x. It slides 6 px a frame
and a paused capture regularly catches it between stops; rounding to the
nearest stop was costing ~3% of the diff and had been mistaken for a sprite
offset that needed sweeping.

### Where it stands

Eight captures, all fresh or re-diffed: **0.02% to 0.22%** of structural pixels.
The floor is three pixels at (59, 10..12), where the original leaves a GAMEFG
pixel erased and we do not know why. Recorded rather than explained.

## 2026-07-27 — Session 9: the three-plane beaker

The beaker looked right and was structurally wrong. `1000:22a6` - the per-frame
beaker update, called unconditionally from the frame loop - turned out to be
the largest unread block in the game, and taking it settled the match rules,
the fade, gravity, scoring and the chain bonus in one pass.

### What the three planes are for

Cells hold `type + 19 * fadeFrame`, not a type. A second plane marks a cell as
clearing, a third carries wave objectives. The payoff is that ONE sprite-table
lookup draws a settled atom and a fading one alike, so the drawing code never
branches - and a fading cell, holding a value above 8, is automatically
excluded from matching by tests that were already there for other reasons.
There is no "is clearing" flag anywhere in the matcher because the encoding
makes one unnecessary.

The port had a single plane of types, so it could not show a clear at all: a
match removed the atoms on the frame it happened.

### The four matchers

Four nested procedures over four ranges, and the ranges alone identify them -
they are exactly the seed positions where a run of three fits. Awards are
literals: 250 vertical, 500 horizontal, 1000 either diagonal. **That confirms
the Detailed Instructions from code**, the first source better than the manual
that rule has had.

Four things came out of the matcher body that were previously guessed:

* the wildcard ADOPTS - a Flashium seed becomes the next non-empty cell's
  colour, which is why one Flashium serves two runs at once;
* the award is per SEED, so a run of four pays twice and a five pays three
  times - "4 atom molecules count as 2 chains" is a literal description of the
  scan, not a separate counter;
* `disabledElement` is a real per-wave variable the matcher checks, which is
  the "element that spawns but cannot be cleared" modifier;
* the chain bonus multiplier is the number of DISTINCT runs formed at once, and
  the total paid is `pending * multiplier` over six ramp frames. It had been
  left unimplemented for want of a number.

One conflict is recorded rather than resolved: an earlier live measurement had
a diagonal four paying 1000 where this model pays 2000. That measurement is
from the black-box session whose conclusions have already been overturned
twice, so the code wins, but it deserves a check once the HUD can show a score.

### Gravity is one row per frame

The destination cursor and the source row walk down together, so an atom falls
at most one row per frame and a column shifts by one per pass. The port
compacted fully in one step, which no amount of looking at a static screenshot
would have contradicted.

### Verified against the running game

`exp17_fade_encoding.py` injects three Redium into the original's cells plane
and samples it: **1, 39, 77, 96, 134, 0** - every value exactly `1 + 19k`,
ending at 134 before the cell empties. Eight fade steps, six of which have
sprites; frames 7 and 8 are null entries and draw nothing, so the atom is
invisible for two frames before the column falls.

The marked plane read 0 alongside `cells = 134`. That is a torn read - the two
planes come from separate GDB requests while the game runs - and is written up
as such rather than as a finding.

### A type bug the tests caught and nothing else would have

Cells were stored in `int8_t`. The composite reaches 152, which overflows a
signed char. It compiled, the game looked correct, and every match test passed;
only the assertion on the fade frame's numeric value failed. Cells are a Pascal
`byte` and are `uint8_t` here now.

### Where it stands

58 checks pass. The five pixel-diff captures are unchanged at 0.02% to 0.13%,
and the clear now animates - three Redium shrink through the RFADE family over
eight frames and the column settles behind them.

Still unread in the beaker update: `1000:2790` onward, which post-processes
settled specials through a "find a cell of type N" helper. That is the
specials' behaviour and it is the next block worth taking.

## 2026-07-27 — Session 10: two reported bugs, then the specials

### The bugs, and what they were really about

**"The game crashes when the beaker fills to the top."** It froze: the port
ended the game the instant a column reached the top, and `update()` returns
immediately once that flag is set, so the window stayed up and nothing moved.
The rule was invented. `1000:3a67` sets game over in exactly two places - the
drop counter wrapping past zero, and the wave objective being met. A full
beaker is neither; it just means nothing more can be tipped into that column.

**"The speed-up button shouldn't need holding."** It does need holding, and now
does. The last thing `1000:0f80` does to every atom every frame is reload its
velocity: `if type = 10 then 0x480 else sessionBase`. The boost is written
before the router runs, so it is worth one frame.

That second one overturned a claim made **one commit earlier** - that the
velocity field had "exactly three writers" and nothing reset it. The search
behind it covered `1000:3a67`; the fourth writer is in `1000:0f80`, which had
never been disassembled. The search was sound and the conclusion was still
false, because a list is only closed over what was searched. `CLAUDE.md` warns
"when a search comes back empty, suspect the search" - this is the same failure
with a search that came back *full*, and it is worth adding to that list.

The same line also settled a lead that had sat in PLAN.md since a play session:
GOLDBALL travels the tube very fast. It is type 10, and it is fast by type.

Added `--demo`, which lets the scripted player drive the LIVE loop instead of
simulating first and drawing once. Hunting the first bug needed a render path
exercised across a whole session, which `--auto` cannot give.

### The specials

`1000:2790` - the tail of the beaker update, running every frame after gravity.
Everything there acts on atoms that have settled, so the trigger is just "a
cell of this type exists", and each routine handles one per frame.

* **AntiMatter** destroys the 3x3 block centred on itself, narrowing the span
  at the edges rather than shifting the block inward, so a corner blast is 2x2.
  Every caught cell is **rewritten to type 9** and marked - which means the
  single `ball[cell]` lookup draws AFADE over all of them and the blast needs
  no case anywhere in the renderer. The notes had guessed that AFADE was "the
  blast applied to everything caught in it"; this is the code doing it.
* **Blocker** turns itself and every cell above it in its column to Xenon.
* **Convertor** takes the type of the ONE cell below it and converts every atom
  of that type **board-wide**. That is stronger than the published description,
  which reads as a local effect.
* **Bonus, Multiplier, EvilMultiplier and Filler** go inert if they settle -
  their real effects fire when the tube catches them, dispatched at
  `1000:180c`, which is the half still to do.

`DS:0x1d48` gates the Blocker here and three of the four catch-time routines,
but not the Bonus. Nothing in `9e53` writes it, so it comes from further out.

95 checks pass, up from 64. The five pixel captures are unchanged at 0.02% to
0.13%.

## 2026-07-28 (later) - the specials at catch time, and two things the tube does

The other half of the specials: `1000:180c` in the router, dispatching Bonus
(`07db`), Multiplier (`08d2`), EvilMultiplier (`0a27`) and Filler (`0b55`) on
the frame a caught atom finishes sliding to its slot. All four are in
`docs/reversing-notes.md`; three things came out of them that were not the
point of the exercise.

**The test tube's record fell out first.** Every one of the four routines opens
with `DI := link^.link^ - $16A`, and everything after is off that base, so the
`Move` calls and the byte they index by pin the whole layout down: x, y, the
count at `+0x21`, and `array[1..5]` of ordinary 28-byte AtomRec at `+0x07+28n`.
Slot 1 is the bottom - each fill writes the slot's y offset from a literal,
52/39/26/13/0, the 13 px row pitch again.

**The tube is a stack, and the port had it as a queue.** `1000:4715` tips
`slot[count]`, the atom caught *last*; a catch lands in `slot[count]` too. The
port tipped slot 1 and emptied it oldest-first. Same site gives records 7..12
their meaning: the tipped atom is `Move`d whole into the first free one, with
state 9, and `n = 6` with none free is a fatal error.

**The Bonus award grows.** `1000:0846` adds 1000 to a word of its own and then
pays the *whole word*; the only other write is the zero in the session
prologue. So the second Bonus of a session is worth 2000 and the third 3000.
Nothing observable would have shown that - it needs two Bonus atoms in one
session and a HUD to read, and the port does not have the HUD yet.

Type 17 is settled as `FILLBALL` - previously "almost certainly". The Filler
shifts slots 5 downto 2 up by one and writes 17 into slot 1 **without touching
the count**, so the Filler itself is pushed off the top and discarded, and the
tipping code simply refuses to tip a 17. That is the whole of "permanently
reduces your tube's capacity": no capacity variable is written anywhere.

### The score ramp's clock was in the wrong place

Fixing the Bonus's award exposed it. `1000:22a6` pays one sixth (`1000:2410`);
the decrement and the flush are a **separate statement** at `1000:58c5`. The
port had them fused into one function that runs before the router - so a Bonus,
which arms the ramp from *inside* the router, got seven sixths of its award
instead of six. A match was unaffected, because its award is raised inside the
beaker update itself, which is exactly why the fusion looked right: the only
case that distinguishes them is one the engine could not produce until today.

Two frame addresses settle the order, and neither is an inference:

    1000:47d0   call 1000:22a6      { the beaker }
    1000:4819   call 1000:0f80      { the router }
    1000:58c5   Dec(rampSteps) ...  { the clock }

Also: `1000:1c63` does **not** clear the increment - the only zero into it is
at `1000:58d2`, when the ramp runs out - so an award landing mid-ramp extends
the ramp at the rate already running rather than recomputing it. The port
cleared it and recomputed.

111 checks pass, up from 95. The eight pixel captures are unchanged at 0.02%
to 0.22%.

A note on the harness, since it cost twenty minutes. `diff_frame.py` prints a
percentage per candidate backdrop and *then* the result line; grepping the
first percentage out of it reported 9-12% and looked like a catastrophic
regression. The number was real, it was just not the one that means anything.
Read `best backdrop:`, not the first match.

## 2026-07-28 (later still) - the tipping animation

Pressing A used to teleport an atom into the beaker. It now tips the tube over
and the atom falls. Five pieces, all in `docs/reversing-notes.md`:

* **The tube's slots are real records now.** `tube_` was a vector of types; the
  original holds `array[1..5] of AtomRec` inline at `tube + 7 + 28n`, and the
  animation writes their x and y directly, so it had to carry positions.
* **The in-tube slide**, router state 8. A catch lands at the *mouth* and
  descends 9 px a frame to its slot. That arrival, not the catch, is what fires
  a catch-time special.
* **The animation**, `1000:463a`. A 2-frame divider, four phases, six frames.
* **Records 7..12 and state 9.** The tipped record is `Move`d whole into the
  first free one and falls to the first free row of its column.
* **Rendering**: contents at their own positions, the six records between the
  `MARKER` overlay and `BEAKER.CSP`.

### TESTUBE1/2/3 are the animation, not three capacities

`1000:5922` draws the tube from a table indexed by the phase. The heights
65/42/27 are a tube going over. They were read as the tube holding 5/3/2 by
difficulty for several sessions - a guess the flat capacity of 5 in the
Instructions had already contradicted without explaining, which is exactly the
shape of thing that sits unresolved until the code turns up.

There is no fourth sprite because phase 4 never survives to a draw: the body
that reaches it resets the phase to 1 before the frame renders. The rendered
sequence over one tip is 1, 2, 2, 3, 3, 1.

### A is not edge-detected

`1000:4511` sets the tipping state whenever the button bit is set. The only
gate is the input block sitting inside `if tube.state = 0`, so holding A tips
once every six frames - which is exactly how long the animation takes to hand
the state back. The port edge-detected it, so holding A did nothing.

### Two near-misses worth recording

**The grid's column order.** The draw loop pairs grid column 1 with `DS:0x1e`,
and the network's column table is 143/125/107/197/179/161 - so it looked for a
while as though the beaker array ran in the network's order, which would put
non-adjacent screen columns next to each other in the matcher. Dumping DGROUP
settled it: `DS:0x1e` is **107**, not 143, and the grid runs left to right. The
two orders are the same six words read from different offsets. Reading the
table rather than reasoning from the draw order is what caught it.

**The pixel diff, again.** A sweep across all four capture directories reported
9-12%, which looked like the refactor had broken rendering. It had not: I was
grepping the first percentage out of `diff_frame.py`, which prints one per
candidate backdrop before the result line. Same mistake as this morning, so it
is now in `docs/debug-rig.md`'s terms in the worklog twice - read
`best backdrop:`.

124 checks pass, up from 112. The eight pixel captures are unchanged at 0.02%
to 0.22%; none of them catches a tip in progress, so the animation's own
pixels are verified by transliteration and by eye, not by the harness.

## 2026-07-28 (last) - the HUD, and a text renderer under it

Chains, the centred score, Drops and the two score pop-ups. The interesting
part is underneath: `2000:35ec` does not draw text in a colour, it draws text
in a colour *walk*. The index sits in `BH` and is adjusted after every
scanline, so a glyph is a vertical gradient off one palette entry. The HUD's
cyan is index 127 plus mode 1, reading down the ramp the palette holds at
112..127; the pop-ups are index 168 plus mode 3, which brightens to the middle
of the cell and dims again.

There is no second colour constant anywhere in the HUD. The labels and the
numbers are both 127 and look different only because the ramp runs over eight
scanlines in one font and sixteen in the other. I would not have guessed that
from a screenshot - and did not: I read `Chains` as green off a scaled crop,
and the pixels say 127, 126, 125 ... straight down the cyan ramp. Sampling the
capture rather than looking at it is what settled it.

### Identifying the font by its glyph

The big font had four candidates. Rather than eyeball them, I pulled the digit
`0` out of the captured HUD as eight bytes and compared it against `'0'` in all
four `.816` files. `FUTURE.816` matches byte for byte; the other three are not
close. That is the cheapest possible oracle and it took one script.

### The HUD is invisible to the pixel-diff harness

`diff_frame.py` compares only structural - grey - pixels, because the backdrop
is random and animated. The HUD is cyan, so the harness reads exactly the same
0.02% to 0.22% with the HUD drawn as without it. It cannot regress there and
it cannot confirm it either.

So the HUD was checked by a separate direct comparison of the y 0..25 band in
palette indices, original against port. **Two** differing pixels out of the
whole band, both the shadow of the `s` in `Chains` at x = 36. Not a
per-character effect - `Drops` also ends in `s` and matches completely - and
the dirty-rect restore over the chains value starts at x = 37, one column short
of explaining it. Recorded and left, like the three-pixel GAMEFG floor.

The state file now carries `score`, `chains`, `drops` and `pending`, so a
future capture taken mid-game can be matched. The existing eight are all from
the opening frames, where the HUD reads 0 / 0 / 9 - which is exactly what the
port shows by default, and is why the comparison above was possible at all.

134 checks pass, up from 124.

## 2026-07-28 - three reported issues, and a rule the descent was missing

All three came from playing the build, and one of them turned up a rule that
had never been read.

### "A missed ball should fall faster than it travels the tubes"

Correct, and `1000:13ed` is the first thing router state 7 does:

    if rec.y >= 50 then rec.acc := rec.acc + $480
                   else rec.acc := rec.acc + rec.velocity

The difficulty's 2 / 3 / 4 px a frame applies only to the **top fifty pixels**
of a play column. Below that every atom falls at a flat nine - the same 0x480
the Down/B boost and the Bonus use. The port fell the whole way at the
difficulty speed.

Reading that region settled three more things in the same twenty lines:

* **The catch is a window, 60..70**, not "at or past the mouth at 68". The port
  would scoop up an atom most of the way to the floor.
* **The tube will not catch while it is tipping** - `tube.state = 3`
  disqualifies it outright.
* **A missed Bonus costs no drop.** The port already had that exemption for a
  tipped atom finding its column full, and not for a miss.

It also invalidates a test. `testSpeedBoostNeedsHolding` measured the boost at
y = 70, which is *below* the acceleration threshold, where the velocity field
is not read at all - so the "boosted 9 px a frame" it checked for is what an
unboosted atom does there anyway. It passed by coincidence. Both descent tests
now run above y = 50, where the velocity is actually consulted.

### The invisible ball in the test tube

Type 8, Flashium, has **no sprite**, and the port skipped anything whose sprite
slot was null - so a Flashium was invisible everywhere, in the tube, the
network and the beaker. It showed up in the tube first because a Multiplier
fills with `Random(8) + 1` and 8 is in that range.

The original does not special-case it in the renderer either. It **rewrites the
ball table**: `1000:486b` sets `ball[8] := ball[flash]` every fourth frame,
cycling 1..7. That is the cycle a play session measured at "~4x/sec"; at 18.2 Hz
four frames is 4.55 Hz. Now from code.

The port substitutes the same colour at each of the four draw sites, which is
the same picture and leaves the cell value 8 - which is what makes a Flashium
always clear with FFADE however it is drawn.

### Tipping onto a full column

Already fixed, by the tipping animation. Verified rather than assumed: the tube
empties, the atom falls, it is destroyed on arrival and it costs a drop. The
behaviour reported was the old `Board::drop` returning false with the ball
staying in the tube, which went away when the tip started going through records
7..12.

144 checks pass, up from 134. The eight pixel captures are unchanged.

## 2026-07-28 - sound effects, and the driver as a second consumer

The `.SFX` format was already "solved" from the file side. Reading
`SBSOUND.DRV`, which eats the same byte stream, corrected two things and
settled a third that could not have been settled any other way.

Its play entry at offset `0x344` walks the header directly - marker, rate,
flag, length, data - and that is enough to see that **the rate is a word at
0x20, not a longword**, and that **the unknown byte is at 0x22, not 0x24**. The
file side could not tell either: every shipped sound is 8000 Hz, so the rate's
high half is zero, and every shipped sound has the flag clear.

"Find a second consumer of the same data" is the top entry in CLAUDE.md's list
of what has actually worked, and it paid again for the cost of one `objdump`.

### One voice, and it is from code

`0x344` opens by calling the driver's own stop routine, and the whole 1,158
bytes hold a single position/length pair. **A new sound cuts off whatever was
playing.** So `Game` keeps one pending sound and a later event in the same
frame simply replaces the earlier one - which is exactly what calling
`PlaySound` twice does on the original, rather than a simplification.

I had been about to implement a four-voice mixer on the grounds that it "sounds
better". It would have been wrong, and nothing in play would have made that
obvious.

### The sound table is indexed by atom type

`sound[t]` at `F9 - 0x72 + 4t`, and index 0 of the same array is `DROP` - so a
lost atom is the sound of type nothing. That fell out of noticing that CRFADE
sits at `-0x2a`, which is `-0x72 + 4*18`, and 18 is the Crystal. AFADE at
`-0x4e` is type 9 and GLDFADE at `-0x4a` is type 10, both of which check.

Every emit site in `game.cpp` now names the address it came from. The settle
sound is worth one note: `1000:278c` fires it **once** at the tail of the
gravity pass if anything moved, not once per atom.

### Verification

Headers and PCM were both diffed against `tools/sfx_decode.py`: name, rate and
sample count agree on all 24, and the PCM is **byte-identical** on all 24
against the reference WAVs. `--dump-sfx` is the standing version of that check,
the same idea as `--dump-regs` for music.

One real bug found on the way out rather than in play: the audio callback holds
a bare pointer into the sound table while a voice is live, and the two were
declared in the order that destroys the samples first. Swapped, and checked
under ASan with a voice still playing - not by running the game under `timeout`,
which kills it before any destructor runs and would have proved nothing.

154 checks pass, up from 144.

## 2026-07-28 - the demo oracle, and what it found in its first run

`--play-demo` replays `DEMO.SCR` through the live loop; `--demo-trace` runs the
whole 11,970-frame recording headless in seven milliseconds and prints every
atom the dispenser rolls. The spawn sequence is the sharpest form of the check
because the recording stores only the player's buttons: which colour appears in
which column is decided entirely by `Random`, by the generator *and* by how many
times each frame calls it.

### Three things settled on the way

**`DS:0xd24` is `RandSeed`, not "the demo pointer".** The notes had it as a
pointer for several sessions. The RTL's own generator reads it at `2000:75bb`,
and `1000:6008` writes the four bytes `1000:5fd9` reads out of the demo - so
the "[inferred]" seed in the `.SCR` header is now proven, and it is why attract
mode is deterministic from a cold boot.

**`Random` is `(RandSeed * n) shr 32`**, not `mod n`. `2000:75bb` is
`RandSeed * $08088405 + 1` written in shifts and adds; `2000:755e` keeps the
top 32 bits of a 48-bit product. The port's xorshift could never have replayed
a recording however correct the rules were.

**The input bits are now read out of the handler** rather than inferred from
run statistics - `1000:4511` tests `$10` for tip, `$04` left, `$08` right,
`$02`/`$20` boost. The old inference was right, which is a nice result for it.

### What the oracle found

First run: the port loses all nine drops by frame 979 of 11,970 and catches
almost nothing. The recorded player was reaching for atoms that were not there.

Cause, at `1000:43d6`: **the test tube starts in a random column.**
`tube.stop := Random(6) + 1`, and it is the session's *first* call to the
generator. The port parked it in the middle. That is wrong twice - the tube in
the wrong place, and every subsequent roll off by one call, so the entire spawn
sequence differed.

Fixed, and the sequence changed. It still diverges: the demo now dies at frame
1,049 instead of 979. So there is at least one more difference, and guessing at
it from this side has reached its limit - the next step is capturing the
original's own spawn sequence on the rig and diffing the two.

That is the point of an oracle. Six sessions of rules landed on unit tests and
eyeballing, and the first thing this one did was find a bug none of them could
see.

161 checks pass, up from 154. The pixel captures are unchanged.

### Handoff for the rig session

Two addresses worked out now so the next session does not re-derive them, both
in `tubes.conf` where `L = 0x0824`:

| what | Ghidra | runtime linear |
|---|---|---|
| the `Random` LCG step | `2000:75bb` = `2685:0d6b` | **`0x1F7FB`** |
| `RandSeed` | `DGROUP:0xd24` | **`0x207B4`** |

A breakpoint on the first counts every call the original makes; the second can
be read directly and compared against the port's generator state after the same
number of frames. That pins the divergence to a call rather than to a symptom,
which is a much sharper instrument than diffing two spawn lists.

And a trap: **`demo_trace.py` has `ARRAY = 0x241A4`**, which is `array[2]`.
That is the same off-by-one-record base this project already chased for two
sessions and which the rig skill warns about. It should be `0x24188`. Fix it
before capturing anything, or the trace will be one record out and the next
session will go hunting a phantom.

## 2026-07-28 - the demo replay, from diverging at spawn 4 to spawn 25

Four findings, all from code, all found by the oracle rather than by looking at
the game. The port now replays `DEMO.SCR` through 24 spawns with the roll count
matching the original's exactly at every one.

### The instrument, after the intended one failed

The plan was to break on `Random` and count hits per frame. It does not work on
this rig. The negative was checked before being believed, because a negative
result is only as good as the filter that produced it:

* the entry address is right - a scan of the whole address space finds **one**
  copy of the LCG's signature, at runtime linear `0x1f7fb`;
* the stub works - a breakpoint at the seed write `1000:6008` fires, and
  `DX:AX` there is `0x322d385e`, exactly the seed in `DEMO.SCR`;
* the code runs - `RandSeed` demonstrably advances.

Yet `Z0` on the RTL never traps, on `core=normal`, first breakpoint or not. Left
unresolved, because the generator hands over a strictly better instrument for
free: `RandSeed * $08088405 + 1` is a **bijection**, so the orbit from the demo's
seed visits each value once and a single read of `RandSeed` maps back to *how
many times `Random` has been called*. No breakpoints, no per-call round trips,
and it samples a free-running guest.

Paired with the spawn count that gives a comparison needing no frame alignment
at all: at spawn N, how many rolls has each side made? Spawn N is spawn N in
both runs. It also validated the port's generator on the way - **737 samples,
every one on the orbit**.

### 1. The demo runs at Tubes 301

The first sample said `drops = 3` before the session had made its first
`Random` call. The port assumed 101, whose seed is 9.

`1000:a483` switches on `DS:0x1d4f` and is the only writer of the three
constants a session runs on - drops, velocity and spawn interval, `9/$100/70`,
`6/$180/60`, `3/$200/50`. The menu's View Demo arm sets `[0x1d4f] := 2` at
`1000:b272`. **`DS:0x1d4f` is the difficulty index, not the mode flag the notes
had it as.**

It read as a mode flag for a good reason worth recording: `1000:b1ee` presets
the block to the *101* values once before the menu loop, and the View Demo arm
sets two neighbouring flags as well - so 101 looked like what the demo
inherits. `a483` rewrites the block on entry to every session.

Confirmed twice live: the difficulty block reads `3 / $200 / $32` twelve seconds
in against `9 / $100 / $46` at the menu one keypress earlier, and a screenshot of
the same moment has the HUD saying `3 Drops`.

### 2. The first dispense is on frame zero

`1000:3be0` seeds the dispenser countdown with **1**, not with the interval, as
the last act of session setup. The tick is `Dec; if = 0 then dispense`, so the
first atom appears immediately.

The port seeded it with the interval, delaying the first atom by a whole period
and sliding the recorded input stream 50 frames out of step with the game for
the rest of the session.

### 3. The Down/B boost is in the wrong place twice

`1000:4534` sits **inside** `if tube.state = 0`, so a sliding or tipping tube
grants no boost - the port ran it every frame. And it sits **before** the
Left/Right handler updates `tube.stop`, so on a press frame the boost goes to
the slot the tube is leaving - the port moved first and boosted after.

### 4. A `.SCR` is one byte per IDLE frame

The load-bearing one, and it is a consequence of *where* the input read sits
rather than of the demo format. `1000:44f0` jumps past the whole input block
when the tube's state is not 0, so the driver vectors are never **called** on a
frame where the tube is sliding or tipping. Live, that is invisible - not
reading the keyboard and reading it then ignoring it look identical. In demo
playback those vectors **are** the recording, so calling one is what advances
it.

A replay that steps the stream unconditionally therefore drifts the first time
the player moves and never recovers: a slide is three frames, so three bytes get
consumed that the original held back.

Measured, and exactly: the demo's first Left presses are at stream indices 16,
17 and 20, and the original ends at stop 3 - which needs all three, only
possible if the middle one waits for the slide to finish. Modelling both
readings against six `(atom y -> tube x)` pairs sampled off the running
original, with the atom's own 4 px/frame rise as the frame clock, the idle-gated
model matches **6 of 6** and the unconditional one matches 1.

### What it was worth

| | first spawn whose roll count differs | reached |
|---|---|---|
| before | 4 of 15 | frame 1,049, score 0 |
| after | **25** of 35 | frame 1,700, score 4,000, 7 chains |

Spawns 1..24 agree on the roll count exactly, so the column re-rolls, the type
rolls and the network occupancy all agree with them. Something remains at spawn
25, where the port spends two rolls the original does not.

And the physics is right, not merely close. Comparing how long each record stays
occupied - the thing that decides whether the spawn has to re-roll - against the
original's own array sampled over sixty seconds:

    spawn   col   original   port        spawn   col   original   port
      1      1      52.9      53           8      4      62.4      63
      2      2      53.1      54           9      6      48.2      50
      3      4      46.1      47          10      5      50.6      51
      4      4      43.7      44          11      4      50.6      51
      5      1      46.0      47          12      6      57.6      58
      6      4      46.0      46          13      1      62.3      62
      7      1      78.2      78          14      6      73.7      73

Fourteen consecutive atoms, same column, same lifetime to within the +/-1 frame
of converting the original's wall-clock samples at 62 ms a frame. Before the
fixes the port was taking 86 to 95 frames where the original took 47.

170 checks pass, up from 161. The eight pixel captures are unchanged at 0.02%
to 0.22%.

## 2026-07-28 - the spawn-25 divergence was the measurement, not the port

Went looking for the divergence at spawn 25 reported at the end of the previous
entry. It does not exist.

That entry compared the port's spawns against a list built from the *plateaus*
of the original's `Random` call count - one plateau, one spawn. But a Multiplier
caught by the tube also moves that count, four `Random(8)` rolls at
`1000:08d2`, without dispensing anything. Spawn 23 dispenses a type 12, the
tube catches it, and the fill shows up as a plateau that shifts every later
index by one.

Rebuilt against spawns actually detected in the original's atom array:

    n  | orig col type rolls | port col type rolls
     1 |     1    11     4   |    1    11     4
    ...                      | ...
    35 |     5     1    95   |    5     1    95

**All 35 spawns identical** - column, type and cumulative roll count. Since the
spawn re-rolls its column up to ten times looking for a free record, a matching
roll count at spawn N means the network occupancy matched at every spawn up to
N too. The dispenser is exact.

The lesson is this project's usual one, inverted: a *positive* result is only as
good as the thing it counted. The filter that produced "spawn 25" could not tell
a dispense from a tube fill.

### What is actually wrong: the tube runs ahead

The port's tube makes the same 142 column moves in the same order as the
original. It just makes them too early - about **fourteen frames** ahead by
frame 1,550. At the point it bites, on the same atom crossing the top lane:

    ORIGINAL record 3   x = 67, 75, 87, 95, 103   tube x = 158,158,158,146,134
    PORT     record 3   x = 67, 76, 85, 94, 103   tube x = 104,104,104,104,104

Samples are ~2.4 frames apart, so the original crosses at 4 px a frame and the
port at 9 - the port's tube has already reached stop 1, which boosts slot 3,
while the original's is two columns away. The atom then arrives at the mouth
after the tube has gone. Two atoms die that way, at frames 1,627 and 1,674, and
that is what ends the replay at spawn 35.

Running ahead means the port has MORE idle frames, so it eats the recording
faster. Both busy states were checked and are right: the slide is 3 frames, and
the tip is 6 frames in state 3 of which **5** skip input - the press frame still
reads a byte, because the input block runs before the state machine that sets
state 3. Counting the press frame as skipped is the easy off-by-one, and a test
was written asserting the wrong number before the arithmetic was done properly.

A one-byte offset at the start is also measured and deliberately NOT applied.
The demo's first three Lefts are at indices 16, 17 and 20 and the original acts
on them at frames 15, 18 and 23, which the idle gate predicts only if frame 0
consumes index 1. Skipping a byte makes the tube track exactly for ~57 frames
instead of diverging at 16 - but nothing in the decompiled setup has been found
that consumes it, and fitting an offset to make a measurement come out is the
move the prime directive forbids. It also does not account for the drift: with
the byte skipped the same two atoms are still lost.

### One real fix that came out of it

The catch tests `rec.x = tube.x + 3` - the tube's ACTUAL x, not its stop index.
The two differ for the three frames of a slide. The port compared stop indices,
catching an atom up to three frames early, which matters because the tube holds
five: an early catch can fill it and make `tube.count <> 5` refuse a later atom
the original had room for. The boost at `1000:4534` does use the stop index, so
the asymmetry is the original's.

172 checks pass, up from 170. The eight pixel captures are unchanged at 0.02%
to 0.22%.

## 2026-07-29 - the endurance ramp, and why a green ball dropped

The demo replay is closed. All 44 spawns the rig capture covers now agree with
the original in column, type and FRAME.

The symptom, reported from watching the build: everything looks perfect and then
it falls apart with a green ball missed. That ball is spawn 32, column 3, type 2
- Greenium - lost at frame 1,627, and it turned out to be the exact atom the
oracle had been pointing at.

### The cause: `1000:235c`, and it fires on MATCHES

    if runsThisFrame >= 1 then
      if (waveMode = 1) or (waveMode = 0) then begin
        Inc(counter);                            { [fe84], a BYTE }
        if counter = 0 then exit;
        if counter mod 5 = 0 then begin
            if not latch5 then begin
                spawnInterval := spawnInterval - 5;  latch5 := true end
        end else latch5 := false;
        if counter mod 10 = 0 then begin
            if not latch10 then begin
                velocity      := velocity + $20;
                spawnInterval := spawnInterval + 5;  latch10 := true end
        end else latch10 := false
      end

Five frames off the dispense interval per ten matches, with the velocity
climbing 0x20 alongside. Not per atom, not per wave, not on a timer - **per
match**, which is why nothing about it shows up early and why watching the game
would never have produced it.

Measured on the running original, its spawns are 50 frames apart for 29 atoms
and then 45:

    spawn 29  frame 1400.1        spawn 32  frame 1535.5
    spawn 30  frame 1445.3   <-   spawn 33  frame 1580.6

The port dispensed at 50 forever. Everything else was already right - the tube
was in the correct place on the correct frame the whole time - so its atoms just
arrived later and later. The catch window is eleven pixels against a nine pixel
step, one frame wide, so it only took a few atoms before one arrived after the
tube had gone. Hence: perfect, perfect, perfect, dropped ball.

### Three of the four steps to it disproved something

**The input vectors, read rather than assumed.** Demo playback swaps
`DS:0x2352` / `DS:0x2356` from the keyboard driver to its own reader at
`24c1:00a6` / `24c1:00bc`. `read` advances the stream and `avail` only
bounds-checks, so one byte per frame the game calls `read` - confirming the
model the port already had, rather than correcting it. `CS:[0x1e]` is the stream
index at linear **0x24c2e**, and it starts at 6 because it is a file offset: the
loader ate the count and the seed through the same buffer.

That address was already in `demo_trace.py`'s comments, as one of two candidates
a monotonicity scan had dismissed as false positives. It was the real one.

**The second `[ds:$2352]` call site was a red herring.** `1000:32d3`, in
`FUN_1000_2dd0`, is reached only from `1000:5cfc` behind `2000:5e52`
(KeyPressed) - a real keypress, which never happens during attract mode.

**The 14-frame drift did not exist.** An earlier comparison had the port's byte
consumption running ahead by a smoothly growing margin. It was the fit:
calibrating the guest's frame rate through the ORIGIN forced the line through a
wrong anchor and manufactured exactly the kind of slow monotonic error that
reads as a real drift. Fitting slope and intercept collapsed the residuals from
±12 to ±1.7 frames over 680 samples. The byte schedule had been correct all
along, which is what pointed at the atoms instead.

That is now three entries for the same lesson from three directions: a negative
result is only as good as its filter, a positive result is only as good as what
it counted, and a trend is only as good as the model fitted to it.

### Also fixed

The catch tests `rec.x = tube.x + 3` - the tube's actual x, not its stop index,
which differ for the three frames of a slide. Catching up to three frames early
matters because the tube holds five: an early catch can fill it and make
`tube.count <> 5` refuse a later atom the original had room for.

### Where it stands

| | first divergence | reached |
|---|---|---|
| two sessions ago | spawn 4 of 15 | frame 1,049, score 0 |
| last session | none in 35 spawns | frame 1,700, score 4,000 |
| now | none in 44 | frame 2,912, score 11,500 |

The port's first missed atom, frame 911, is the one the original misses too. Its
next two are at frames 2,912 and 2,963, just past the end of the 175-second
capture, so whether those are real needs a longer run.

181 checks pass, up from 172. The eight pixel captures are unchanged at 0.02% to
0.22%.

### Verified over the whole run, after the fact

A 400-second rig capture settles the range question. The port and the original
agree on **which frame every byte of the recording is consumed at** - 1,184
common byte counts, residuals -0.7 to +1.6 frames - across frames 0 to 2,963,
the port's entire life.

Two things fell out of it. **Attract mode does not play the whole recording**:
`DEMO.SCR` holds 11,970 input bytes and the original consumes 2,395 before its
session ends at frame ~4,572, so the stream outlives the session and the
reader's `avail` bounds check is never what stops it. And the remaining gap is
now two specific catches - the port loses atoms at frames 2,912 and 2,963 that
the original keeps, with everything measurable identical up to that point.

## 2026-07-29 - what is after the demo (nothing), and why it ends where it does

Two questions from watching the build, both now measured.

### "It seems to end in the same spot - I wonder what's after?"

Nothing. `DEMO.SCR` holds 11,970 input bytes and real play - `02` Down, `04`
Left, `08` Right, `10` A - runs from the start to about byte **3,950**. After
that only six lone `0x05` bytes appear, at 4,216, 4,234, 4,269, 5,356, 7,585 and
8,930, and nothing beyond. The recording is roughly four thousand frames of play
sitting in a twelve-kilobyte buffer; the `count` field describes the buffer, not
the performance.

### And it ends on DROPS, not on the stream

Reading the drops counter through a run:

    bytes   693   3 -> 2      a miss
    bytes  2188   2 -> 3      a BONUS caught - the only thing that gives one back
    bytes  2276   3 -> 2
    bytes  2320   2 -> 1
    bytes  2352   1 -> 0
    bytes  2367   0 -> 255    the byte underflows: game over

So the recorded player is beaten by the allowance about 1,600 bytes before their
own input runs out, and the reader's `avail` bounds check is never what stops
playback.

The likely reason is worth stating because it is testable: **the recording was
probably made at an easier setting than the one attract mode replays it at.**
Nothing in a `.SCR` carries a difficulty - only the seed - and View Demo
hardcodes Tubes 301 with its three drops. A performance recorded at 101's nine
would run much further than the replay of it does.

### "How are there 2 catches missed still?"

They are one atom, and everything else is right.

The port's first miss is the original's, byte for byte: both lose a drop at byte
693 with the score at 1,000. What the port never gets is the **Bonus at byte
2,188** that hands the original a drop back.

The beaker settles the rest. Comparing the original's cell plane against the
port's - raw `type + 19*fade` on both sides - 649 of 663 comparable samples are
identical, and all 14 exceptions follow the one late atom.

That atom is record 4, dispensed around byte 2,087 in both. It reaches the catch
window at byte 2,125 in the port and 2,122 in the original, and in those three
frames the tube leaves stop 6 - so the original catches it at y = 63 and the
port watches it fall. Traced back, the port's copy is already behind during its
RISE: at byte 2,092 the original is at y = 119 and the port at y = 162. That is
where to pick up.

### A measurement bug that gave a confident wrong answer

The port's CSV first wrote `Board::typeAt()`, which strips the fade, against the
original's raw cells. Every clearing cell then looked unmatched, and the diff
reported - with the right cells, the right types and a plausible mechanism -
that the port's matcher was missing a Flashium-seeded diagonal. It was not:
those cells sat at fade frame 4 and were clearing normally. Compare like with
like, and prefer the rawest form of both sides.

`--demo-csv` now emits the raw cell plane, the drops, the score, the tube's
contents count and per-frame `state:y` for all six network records, which is
exactly the set the rig capture holds for the original.

181 checks pass. The eight pixel captures are unchanged at 0.02% to 0.22%.

## 2026-07-29 - the ramp counts per RUN, and the replay closes

The atom's rise was not slow. Measuring it settled that in one step: 4.25
px/frame in the port against 4.33 in the original, the same velocity within the
timing error. It was **dispensed later** - which led to the interval, and the
interval led back to the ramp.

### The ramp body is a loop

`1000:240a` decrements the run count and `1000:240d` jumps back to the
`runs >= 1` test at `1000:2342`, falling through to the score multiplier at
`1000:2410` only once the count reaches zero:

    while runs >= 1 do begin
      if (waveMode = 1) or (waveMode = 0) then begin
        Inc(counter);  ...mod 5... ...mod 10...
      end else Inc(counter);
      Dec(runs)
    end

So the counter advances **once per run**, not once per frame with runs. Runs are
counted once per SEED, so a line of four pays twice and simultaneous runs in
different orientations each count - one good clear can walk the counter through
a crossing by itself.

Yesterday's transliteration hung the body off `if runs >= 1`, and the cost was
invisible for thousands of frames: the counter reached 14 by frame 2,792 where
the original was past 25, so the interval never made its second step and every
atom after that ran progressively late.

That is the same shape of error as the scoring award, which is also **per seed**
rather than per event. This binary counts seeds in more than one place, and
"once per event" has now been the wrong assumption twice.

### The replay now matches end to end

| event | original | port |
|---|---|---|
| a miss, score 1,000 | byte 693 | byte 693 |
| **a Bonus caught, +1 drop** | byte 2,188 | byte 2,184 |
| a miss | byte 2,276 | byte 2,274 |
| a miss | byte 2,320 | byte 2,319 |
| a miss, drops now 0 | byte 2,352 | byte 2,353 |
| the drops byte underflows: game over | byte 2,367 | **byte 2,367** |

Final score 13,000 on both sides, 71 spawns, and the session ends on the same
byte. Over 752 common byte counts the two agree on which frame each byte is
consumed at with a median difference of -0.2 frames; all 71 spawns land within
4.7, which is the measurement's noise rather than the port's.

Progress across the four sessions this took:

| | first divergence | reached |
|---|---|---|
| four sessions ago | spawn 4 of 15 | frame 1,049, score 0 |
| then | none in 35 spawns | frame 1,700, score 4,000 |
| then | none in 44 | frame 2,963, score 11,500 |
| now | none | the original's own last byte, score 13,000 |

`--demo-csv` also stopped calling a Bonus a miss: `1000:180c` gives a drop BACK,
and labelling every change to the counter a loss made the trace read as three
misses where one was a gain.

185 checks pass, up from 181, including one that fails on the per-frame reading.
The eight pixel captures are unchanged at 0.02% to 0.22%.

## 2026-07-30 - the wave table, and it is 75 literal arms

The demo replay closed the day before, so the next item was the largest one
left: wave structure. It came apart in a single pass, and the question that
opened it was small - **who writes `DS:0x1d4e`?**

`FindScalarRefs.java` answers with a contiguous run of 22 functions between
`1000:62f1` and `1000:8320`, each setting the mode byte and sharing nothing
else. They are the objective templates, one procedure per briefing.
`MapProgram.java` then names their single caller: `1000:86b8`, the briefing
screen, whose body is an if-chain **75 arms long** on the wave number.

Everything followed from there:

* 25 objective routines, including `1000:8581`, Mystery Wave, which rolls one
  of four and runs it outright;
* the six counters they read, written as **immediates** at `1000:a4cd` - and
  they turned out to be `3, 30, 2, 0, 3, 8`, the exact list `PLAN.md` had
  carried for two sessions as "difficulty seeds, variables not yet named";
* the progression at `1000:a616`, which runs **only on a cleared wave**;
* `1000:192f`, the scoring hook, where an all-Flashium run satisfies any
  colour;
* `-0x1ff`, the flag that makes a Continue replay a wave with the objective it
  already had.

The check that mattered came free. A black-box session months ago had warped a
save to sample nine briefings out of the running game. Feeding the seeds
through the decompiled table reproduces **all nine**, kind and count -
including that waves 10 and 15 share an objective, which the sweep could only
record as a coincidence, and wave 50's "Mischief Crystals: 1", which is seed 0
with `1000:66cb`'s own `Inc` in front of it. Two independent readings of the
same six numbers.

Three commits: the notes, `src/wave.{h,cpp}` with its tests, and the wiring
into `Game`. Then `1000:2a4a`, the Task Display, and `--wave N` to play one.

**Two things worth remembering.**

The scoring hook is called from `1000:1c3b` - after the distinct-run test and
before `ADD [pending], 250`, on the path every seed takes. So it fires **once
per seed**, and a run of four spends two of the objective. That is the third
time this binary has counted seeds where an event would have been the obvious
guess, and the first time it was checked before being written.

And the argument order of `OutText` is `x, y, colour, mode, text`, which had to
be read off the HUD's own drops draw at `1000:5782` rather than taken from the
decompiler - Ghidra lists call arguments in reverse push order. Getting it
backwards would have put the Task Display on its side.

One correction landed with it: the seven 8x7 balls at `DS:0x200a` were recorded
as the wave-mode HUD counters because they were the right size and the right
count and nothing else claimed them. The draw site indexes `DS:0x1da6` instead,
the ordinary ball table. `0x200a` is unowned again, and `1000:2894` is the
candidate. Shape is not ownership.

### Later the same day: the four setup routines

`PLAN.md` had them as the gate on nine of the 25 objectives, and they came out
in one pass too. Three share a single idiom - pick a column that is not full,
let the atom fall to rest - so `1000:0000`, `1000:035e` and the two cover loops
are the same eight lines with a different fill value.

The morph, `1000:4bf6`, is the one that would have been got wrong. It is a
**rotation**: every settled ordinary atom steps to the next colour, 7 wrapping
to 1. That is a permutation, so every chain already in the beaker survives it
intact - which is the opposite of what "the atoms in the beaker will morph into
another atom" suggests. What it actually destroys is the player's plan, since
the test tube and the network do not morph with it.

`1000:0236` gave up more than it was asked for: the Crystal has a 10-byte
record, and following it produced `1000:0560` (the teleport), `1000:04ca` (the
record following its cell down the gravity pass) and a **correction** -
`1000:041c`, carried for months as "the Crystal's teleport", is the *removal*,
called by the AntiMatter blast. Nothing else calls it, which is exactly why a
crystal is "removed with Anti-Matter, never by matching".

The teleport also settles an old loose end honestly: `CRFADE` is one family
played out and then back in, and `CRFADE1` is its resting sprite because frame
0 is where it lives. That had been the standing guess since the type table was
read; it is derived now.

Two small self-corrections landed with it, both from re-reading rather than
from a failing test: record `+0` is `active` and `+1` is `arriving`, not the
reverse; and the blast and the gravity pass test for a Crystal **two different
ways** - `cell mod 18 = 0` and `cell mod 19 = 18` - which agree only because
types 11..17 have null fade pointers. Both are transliterated as written.

Wave 19 now opens with eight atoms in the beaker, wave 20 with three carrying
the `MARKER` overlay - the first time that plane has had anything to show - and
wave 50 with a Crystal that teleports and can be blasted. 390 checks.

### And `1000:2894`

Small, and it closed two things at once.

It draws **three half-size balls in the shape of the required chain** - a row
for code 1, a column for code 2, and a diagonal that **alternates direction**
every flash tick because both diagonals count. So the orientation numbering is
confirmed a fourth time, and visually rather than by inference.

The sprites come from `DS:0x200a`, which these notes had attributed to the Task
Display on the strength of size and count, then withdrawn when `1000:2a4a`
turned out to index `DS:0x1da6` instead. Both were right: the count modes draw
one full-size ball, the chain modes draw three small ones. The loader at
`1000:ad41` names them `SRBALL` .. `SPNKBALL`.

It also forced a proper reading of the **draw argument order**, and that
corrected yesterday's Task Display coordinates. `Draw` pushes `x, y, sprite`
and Ghidra lists arguments in reverse push order, so the LAST decompiled
argument is x. `2894` only lays out as a row and a column that way, and
`2a4a`'s ball and `MARKER` differ by `(+2, +1)` that way - the same offset the
beaker uses. The ball is at (6, 10), not (10, 6), which puts the count squarely
on it and centred, since a 16-wide ball at x = 6 has its centre at 14 and the
count is centred about 14.

And the tail of the Flashium tick at `1000:48ab` explains the rotating counter
the wave 6 sampling measured: when a wave names no colour, `taskColour` is
pointed at the same cycling variable Flashium uses. When the orientation is
free, the chain picture cycles as well. Both are cosmetic - they move
`1000:3a67`'s copies, never the objective's.

### The hidden-atom modifier, and `MYSTBALL` is not a ball

Six readers of `-0x189` in `1000:3a67`, all the same two-line swap, and
identifying which record each belongs to - by the type field it loads, at
`base + 28*i + 0x0b` - gives records 1, 6, 2, 5, 3, 4: the six **network**
slots in the original's interleaved draw order.

The useful half is where it stops. `3a67` makes 21 sprite-table draws and only
those six test the flag, so the test tube's contents, records 7..12 and the
settled beaker all show the real atom. The concealment ends the instant an atom
is caught - which is what "hidden until they leave a tube" says - and it is
done with **no per-atom state at all**: one global flag and a substituted
sprite.

That also closes type 19. `MYSTBALL` has been carried since the type table was
read as "still unidentified", with a play report that a `?` *resolves* into one
of the letter balls on capture. It is not a ball and it resolves into nothing:
it is the sprite a real atom wears while a hidden wave runs. The atom keeps its
type the whole way and still matches, still counts, still fires its special.
The play report is superseded and kept only as a record of the wrong turn.

## 2026-07-31 - the high score viewer was findable all along

The player reported two things about the screen the menu opens: the chalkboard
should not have writing on it, and there are two tables - wave and endurance -
with a key moving between them. Both are corrections to what shipped in
`5395e84`, where the viewer was written up as **not decompiled** and its layout
invented to be legible.

It was decompilable. `1b2e:61b6` references the strings `Endurance Mode High
Scores` and `Wave Mode High Scores`, and one `awk` over the `MapProgram` dump
that was already on disk finds it:

    awk '/^@FUNC/{f=$0} /@STR/{print f" || "$0}' map.txt | grep -i "high scores"

The previous session searched for it with `FindScalarRefs` on the two bank
addresses, found only the loader and the entry screen, and reasoned that a
scalar scan cannot see a routine that takes the bank as a parameter. The
reasoning was sound; the conclusion was wrong, because `1b2e:61b6` does name
the banks with plain literals. What was actually missing was a **different kind
of search** - by string rather than by scalar. `CLAUDE.md`'s "when a search
comes back empty, suspect the search" has been about filters that were too
narrow; this is the same rule one step out, about the *kind* of filter.

There was a second tell that went unread. `docs/reversing-notes.md` has said
since an early menu probe that the table is headed "Endurance Mode High
Scores", and that a probe "captured the same blackboard three times while
believing it was walking the Start Game path". Those are this screen's two
pages: they differ only in the heading and the ten rows, so paging through them
looks like one screen that will not dismiss. A quoted title in our own notes is
a string to grep for.

Both player reports come straight out of the function. The chalkboard is blank
because `2321:060b(10, 37, 299, 118, 111)` - the entry screen's own panel -
covers everything but the board's frame. The two tables are one per **video
page**: Endurance is drawn to page 0, Wave to page 1, and `2321:01b5` flips.
ESC on the first page leaves at once, any other key advances, and any key on
the second leaves; a thirty-second timeout does whatever a key would.

Three things the port had wrong and now does not: the equations showed through,
both banks were crammed side by side in invented columns, and the scores were
left-aligned where the original writes `Str(score:10)`. That last one is the
entry screen's bug too, since both screens share the code path.

The layout also self-checks. With the 8-pixel advance, a 25-character name -
the longest the typing loop allows - ends at x 216, and the ten-character score
field runs 220..300 inside a panel that ends at 309. Constants read
independently would not be expected to fit that tightly by accident.

Not corroborated against a capture of the original: every number is off the
disassembly. That is the right way round for this project, but the rig would
settle it in a minute and is worth doing before this is called finished.

The same `[0x230e]` reading then paid out on the screen next door. `1000:96db`'s
tail plays `CLAP.SFX` when the typing loop ends, waits `23e7:0024(0x78)` - 120
retraces, 1.71 s - and only then copies the record into the bank and writes the
file. So the applause is not the viewer's alone, and the entry screen holds
while it plays instead of vanishing on the keypress, which is what the port did.
Reading a function's tail properly needs the *vector* identified first; with
`[0x230e]` unnamed, that call was just an indirect jump into the unknown.

### And then the rig said zero

Both viewer pages captured from the original and diffed whole - all 64,000
pixels, no mask, no structural-pixel filter - come back at **0 differing
pixels**. `grab_hiscores.py` and `diff_hiscores.py`, both in the tooling
directory.

The sharpness is the point. `diff_frame.py` has to compare only grey structural
pixels and mask every atom, because a gameplay frame has a random backdrop,
stars animating over it and atoms that move during the capture; its floor is
0.02%-0.22% and three known pixels at (59, 10..12) that nobody can explain. A
menu-side screen has none of that noise, so an exact whole-frame comparison is
available, and an exact comparison that passes says something a filtered one
cannot. Reach for it on every static screen from here.

The key model was tested rather than assumed, by running the screen twice: RET,
RET, RET gives Endurance, Wave, menu; RET, ESC gives Endurance, menu. The ESC
frame is 0.2% off the other menu capture (the title atom moved) and 77.6% off
the Wave page, so there is nothing to interpret. The two page-0 captures are
byte-identical, which also says the screen is deterministic.

Two habits worth keeping from the capture side. `DOSBoxInstance.start()` runs
`pkill -9 -f dosbox-x`, which a capture script has no business doing to a
machine that may have a game running on it - `grab_hiscores.py` subclasses it
and neuters that, then uses `tubes-sweep.conf`'s own ports. And the drive it
runs on has no `TUBES.HSC`, which is the only reason the two sides are
comparable at all: both show the twenty shipped defaults, neither seeded.

## 2026-07-31 - a wave that ended at the dispenser, not at the beaker

The player: wave 2's levels "end abruptly", and a mode that asks you to make as
many chains as you can out of 30 atoms "should end when the last atom is placed
in the beaker, not when the atoms count to zero".

That is the code, exactly. Two counters run in a mode-4 wave and the port had
them locked together:

    -0x1f4   the OBJECTIVE, seeded 30, decremented in the DISPENSER (1000:4b3b)
    -0x1be   the IN-PLAY count, seeded 30 too, decremented when an atom LEAVES
             play (1000:158d, 1000:17b4, 1000:0c6b)

and `1000:5d30` requires both to be zero. The port wrote `task_.count =
objective_.counter` on every dispense, so they emptied on the same frame and the
wave ended with six atoms still in the air.

`-0x1be` had been carried as a Task Display field called `count` since the wave
work, because `1000:3af0` seeds it from the objective right beside the colour
and the chain. **It is never drawn.** The number beside the balls is the
objective counter itself. The tell was available: `FindScalarRefs` on `0xfe42`
returns five sites in `1000:3a67` and not one of them is in the drawing code -
two of the five are not even the variable, they are `[BP + DI + 0xfe42]`, which
is the atom record's `+0x08` state field reached through the array's biased
base. A field that is only ever seeded and tested is not a display field.

Reading the three decrement sites settled the shape of it. Every one is the
same three lines gated on the mode, and every one sits where an atom stops
existing: the descend arm's `y > $bb` branch (lost at the bottom), the tipped
arm's landing join (settled, or destroyed against a full column), and the
Filler pushing the top slot out of the tube. The two fill routines INCREMENT it
inside their loops, one per ball, because those balls have to leave play too.

Which means a tube left full holds the wave open indefinitely - the atoms in it
have not left play. That is now a test, and it is the sort of behaviour that
would never have been guessed from watching.

### The clear timer was one frame short, everywhere

Found while writing the test rather than looked for. `1000:5d29` tests the
clear timer and `1000:5d3c` decrements it - in that order. The port decremented
at the end of `updateBeaker()`, which is early in the frame, so a timer set to
10 by a match was already 9 when the same frame tested it.

One frame, and it applies to every wave in the game, not just mode 4. With the
order corrected a chain-mode wave now clears its nine cells at frame 7 and
raises the banner at frame 10 - three frames of settled board rather than two.
That is the whole of the "chains should finish animating" complaint that was
not the counter bug.

The lesson is the ordinary one: a statement's POSITION in a frame is part of
the transliteration. `docs/reversing-notes.md` recorded the tail's statements
in the right order all along; the port just did not put them there.

### The banner that was never seen - a bug, not user error

Follow-up report on the same session: wave 1 ended without showing Wave
Complete at all. It is a bug, and reading `1000:5d64`'s three arms says exactly
why.

A banner is five steps and only the middle one reads input:

    PlayMusic; WriteCentred x2
    Delay($28)                        { 40 retraces = 0.571 s, DEAF }
    ClearKeyBuffer
    repeat key := ReadInput until ([DS:$22ce] = $ff) or (key <> 0)
    StopMusic

with another `Delay($28)` at `1000:5ef0` on the way out. The port had the
middle line and nothing else.

That is fatal in precisely the case the player hit. A mode-4 wave now ends when
the last atom LANDS - which the player caused by holding the tip button - so
the keypress that ended the wave is still queued when the banner opens, and a
bare key wait consumes it on the opening frame. The banner was drawn for one
frame and gone. The opening Delay plus the flush is the original's guard
against exactly that, and it is not something a black-box reading would ever
have suggested: from outside it looks like a banner that stays up until you
press something.

`[DS:0x22ce]` also closes a `PLAN.md` item that has been open since the player
first described it: the banner ends when its music does. The vector returns
`$ff` once the song has been round once, and **the port already had the flag** -
`MusSequencer::looped()`, the driver's `cs:0x32`, decoded with the rest of the
sequencer and never wired to anything. Nothing needed decoding; it needed a
caller. Worth remembering next time something looks like new work: check what
the existing transliteration already knows.

### And the harness wrote to the player's game directory

Caught by the high-score diff, not by looking: page 0 jumped from 0 differing
pixels to 2,732 between two runs of `diff_hiscores.py` with nothing in between
that touches that screen.

`--auto-advance` walks a whole session, so it reaches the end of a wave,
qualifies, and `saveHiScores()` wrote a real `TUBES.HSC` into
`~/Games/GAMES/tubes/` - the player's own game files. The port then loaded it
on the next run and the Endurance table no longer held the shipped defaults,
which is what the diff was reporting. Nothing was corrupted and the file was
moved to `~/Dev/tubes-tooling/recovered/`; the directory is back to what it
was, since it had no `TUBES.HSC` before.

`saveHiScores` now returns early under `harness`, which is the same flag that
already keeps every capture deterministic. The unit tests were careful about
this from the first commit - "the game directory is not written to by any
test" - and the harness was not, because until high scores landed nothing in
it wrote anything at all. A new write path has to be checked against every
entry point that runs a session, not just the ones that look like tests.

The diff catching it is the argument for keeping a zero-pixel check around: an
exact comparison that has been zero once will report anything that disturbs it,
including things that have nothing to do with rendering.

## 2026-07-31 - saved games: the format, and the load path

`TUBES.SAV` had five fields from comparing two samples and eight more listed as
"matching singles" of unknown purpose. All sixteen came out of the code in one
pass, by the method this project ranks first: `FindScalarRefs` on the two bank
addresses gives four consumers of one byte stream - reader, writer, save screen
and menu - and between them nothing is left to infer.

The best detail is the one that was written up as a mystery. The byte at each
bank's `+0x1bb` "differs between samples including in the bank that stayed
empty, so it is not a checksum". It is `Random(254) + 1`, written into both
banks on every save by `1b2e:00ac`, at what is really record 5's interval byte -
the sixth record of six, the one no slot uses. The reader loads it into a local
and never looks at it again. A nonce with no consumer.

And the record carries `WaveProgress`, so **a save restores the progression and
not just the wave number**. That retro-explains the level-warp sweep seeing
wave-6 counters on a wave-75 save: it edited `+0x26` and nothing else.

### The two banks are labelled differently, and that is what settled the offsets

Reading the menu's slot-list arms turned up what looked like an off-by-two: the
Endurance arm reads `bank + 0x28` and the Wave arm `bank + 0x26`. The save arm
says `+0x26` is the wave and `+0x28` is chains-this-wave, so one of them had to
be wrong.

Neither is. **Endurance has no waves, so its list shows the chain count** -
`Pad(desc,20) + '  ' + Str(chains) + ' Chains'` against the Wave list's
`Pad(desc,20) + '    Wave ' + Str(wave)`. Two arms, two fields, both right, and
a port that printed "Wave n" in both would have been wrong in the half nobody
would think to check.

That was settled by capturing the list from the original rather than by
staring: `gamedrive/TUBES.SAV` has `+0x26` = 75 and 4 against `+0x28` = 3 and
0, and the screen reads **Wave 75** and **Wave 4**. Nothing to interpret. This
is measurement in the role the prime directive allows it - validating a reading,
not deriving one.

### A pixel diff found a bug in code that was already right

The port's slot list came out at 326 of 64,000 differing pixels, all of them
the two stars. `placeStars` measured `kMenuPages[...].items[i-1]` - the page's
OWN text - and a save-slot page is precisely the page whose text is replaced at
runtime. With a 31-character save row the stars belong at 16 and 287; it was
putting them at 88 and 215, the width of the "(Unavailable)" that was no longer
on screen.

That function has been correct since the title screen landed, because until
today no page it was used on had live text. The remaining 252 pixels are the
star's rotation phase, which a single-frame capture cannot control.

### The F2 save screen, and the cursor that was not there

The write half. `1000:2dd0` holds it, which is why it never appeared as a
function of its own: the in-game key handler, the F1 help and the save screen
are one 3,064-byte routine.

Two things came out of capturing it rather than reasoning about it, and both
would have shipped wrong:

- **the number column is drawn for every row.** `1000:31a8` sits after the
  "live description" and "( Available )" arms join, so an empty slot shows a
  right-justified `0`. The port had guarded it on the record existing, which is
  what anyone would write.
- **the background really is the raw play field.** The page calls at
  `1000:3062` looked like they might be clearing a page first. They do not, and
  the original is exactly as hard to read as the port.

And one that came out of reading the listing after the capture raised the
question: **there is no typing cursor.** The high score screen pulses a 4 x 4
block at `1000:9757` and this loop does nothing of the kind - it draws
characters and erases an 8-wide cell on backspace. The port had been given one
by analogy for a revision. A capture cannot refute a cursor (the pulse dims to
nothing at one end), so this is a case where the code had to settle it and the
capture only prompted the question.

The other thing the listing settled: ESC and RETURN are not the same key here.
`1000:3635` jumps ESC straight past the write to the exit, so backing out of
the description abandons the save rather than committing what has been typed.

The screen carries 99.1% of the original's heading ink and 99.5% of its row ink
at the same coordinates. It cannot be diffed exactly, because what is behind it
is a live play field with a random backdrop - the one screen in the game where
the zero-pixel method does not apply.

## 2026-07-31 - Game Options, and the one screen we are not transliterating

The player settled the design before any code: no `SETUP.CFG`, no device
chooser, just remap the six controls, and make it work with an Xbox pad.

The reasoning is worth keeping because it is the first time this project has
deliberately NOT transliterated something. The original asks an input driver
for one byte a frame - Up, Down, Left, Right, A, B - and that byte is real game
logic: `DEMO.SCR` stores exactly one per frame, which is why a recorded demo
replays through the same code path as live play. Nothing about it changes.

Everything BELOW it is DOS hardware plumbing. `KEYBOARD.DRV`, `JOYSTK1/2.DRV`
and `MOUSE.DRV` exist because 1994 had no common abstraction over an XT
keyboard, a gameport and a serial mouse. SDL is that abstraction. Porting a
driver chooser would be transliterating the *absence* of SDL - and it would be
worse than the original, which could bind one device at a time where SDL polls
a keyboard and a pad at once.

So: the two toggles are ported, because they are the game's own flags
(`DS:0x215f` and `DS:0x215e`, the same two F3 and F4 flip) and they now
persist. The third item does what the original's did - remap the six controls -
by a modern route, and there is no device to choose.

`SETUP.CFG` is deliberately not written. The original rewrites it on leaving
the page, but it is the DOS install's hardware configuration, `SETUP.EXE` owns
it, and the port does not read a byte of it. Writing SDL bindings there would
damage a file the real game still reads. Port settings go in the port's own
file under `SDL_GetPrefPath`, which is also the answer for the eventual
non-desktop ports. Same lesson as the game directory, one level out.

`src/input.{h,cpp}` holds the bindings and the settings file with no SDL in it
- a binding is two opaque integers - and `main.cpp` turns them into the mask.
`readKeyboard()`, which hard-coded the arrows and Ctrl/Alt, is now
`readInput(bindings, pad)`.

Small design notes that took a moment each:

- a binding cannot be shared: binding Down to Up's key takes it off Up, because
  the game reads ONE mask and a key setting two directions is a state no driver
  could have produced;
- the keyboard and pad halves move independently, so rebinding on the keyboard
  does not silently unbind a controller;
- when the screen is armed the NEXT press binds, Escape included - there is no
  other way to bind Escape and no reason to forbid it.

### The rebinding screen becomes a classroom

Player note on the first version, which was a bare green rectangle over the
title art: make it a whole classroom, similar to but not the same as the high
score viewer.

It ends up borrowing from both and matching neither, which is the right answer
for a screen the original never had:

    from 1b2e:0a11   the blackboard at (0, 12) and the professor at (267, 121),
                     waving his pointer on his own ten-retrace clock - the
                     "whole classroom", which the high score viewer pointedly
                     does not have
    from 1b2e:61b6   the chalk panel over the board, the roller bar on its top
                     edge, the cursive rows, the centred title
    its own          a panel that stops at x 257 so the professor is not
                     painted over, and two columns instead of name-and-score

The projector slide is left out deliberately. `drawScene` always lays it down
and it is 172 wide - fine for a briefing's small-font prose, hopeless for
"Button A" against "Left Ctrl / A".

One detail that reads as design rather than accident: the control NAMES are in
the viewer's cursive because they are words chalked on a blackboard, and the
BINDINGS are in the heading font because "Left Ctrl" is a label off a keyboard
and has to be read exactly.

### The game's red is 0x2f, and the viewer's heading is genuinely blue

Player asked for the rebinding screen's highlight in red, and thought the high
score screen's header should be red too. The first is a clear improvement; the
second is worth recording because the answer is measured rather than argued.

**There are two high-score screens and they use different colours.** The ENTRY
screen's title is `0x2f`, the same constant that draws "Save Game" and the
abort banner - sampled off a capture, `0x2f` is `(215, 0, 0)`, pure red. The
VIEWER's is `0x9f`, pushed as `-0x61` at `1b2e:63b9`, and sampled off the
captured original its ink is `(0, 0, 215)` with no red component at all. The
port renders `(0, 0, 214)` in the same pixels and the page diffs at 0 of
64,000.

So the red header the player remembers is the entry screen's, and it is already
red. Changing the viewer would break a zero-pixel match against the original to
make it match a memory. Left alone, and the highlight took `0x2f` instead -
which is the better colour on green anyway, and now the game's own red is used
for both.

The board is also wiped across its full width now rather than just behind the
rows. A panel that stopped short of the professor left chalk equations showing
beside him, which read as a mistake rather than as a blackboard; and the
professor is drawn AFTER the panel so he stands in front of a clean board
instead of being wiped off it.

The roller bar moved to the top of the board and the title came inside the
panel. The viewer draws its title across the bar and gets away with it in blue;
in red on grey it is a struggle, and there is no room to clear a 16-tall font
between the board's top edge at 12 and the panel below.

## 2026-07-31 - View Demo, and the dispatch that named two more screens

`1000:b23e` is the whole of the title screen's caller, and reading it settled
View Demo in one pass:

    case result of
      1, 2:  Session                          { play, or load }
      4:     1b2e:61b6                        { the high score viewer }
      5:     1b2e:2d63                        { Instructions }
      6:     DS:0x1d4e := 0; DS:0x1d4c := 1; DS:0x1d4f := 2; Session
      7:     1b2e:411b                        { Credits }
      9:     k := 1b2e:1651                   { the blackboard cutscene }
             if k <> 2 then begin
               DS:0x1d4e := 0; DS:0x1d4c := 1; DS:0x1d4f := 2; Session
               DS:0x1d42 := 0                 { the menu comes back DOWN }
             end

So **View Demo and the attract timeout are the same three stores** - mode 0, a
new game, difficulty 2 - and the session loads `DEMO.SCR` itself at
`1000:5f4b`. The port already replayed the recording end to end under
`--play-demo`; all that was missing was the two ways in.

Difficulty 2 is not decoration. It sets the dispense interval the recording was
made against, and the port learned that the hard way months ago - playing the
demo at 101 desynchronises it within a few spawns. Seeing `DS:0x1d4f := 2` in
the dispatch confirms a constant that had been derived from the oracle
diverging.

Two free finds in the same fifteen lines: **Credits is `1b2e:411b`** and
**Instructions is `1b2e:2d63`**, both of which were guesses in PLAN.md.

### What is deliberately missing, and marked

- the timeout runs the **cutscene first** and skips the demo if it returns 2;
- `1000:a690` calls **`1000:9338`** at the end of an attract session - guarded
  on mode 0, so it runs in no other mode, and nothing else calls it. Unread.

Both are noted at the call site rather than papered over.

### One fix the demo forced

The banner's wait ends on a key or on `[DS:0x22ce]`, the driver's "has the song
been round once". The port only consulted it when music was ON, so with music
off the Game Over banner at the end of the demo waited for a keypress that
attract mode has nobody to supply, and the loop stopped there. With no song
playing the answer to "has it finished" is yes - so the wait now ends, and
attract mode turns over on its own: demo, game over, title, thirty seconds,
demo again. Verified by running 5,200 frames from a cold title screen and
finding a SECOND demo already in progress.

## 2026-07-31 - Instructions, extracted rather than transcribed

`1b2e:2d63` is 4,510 bytes, 152 strings and 22 illustrations across 21 slides.
That is data, and the interesting decision was not to type it.

`tools/gen_instructions.py` reads the disassembly and emits
`src/instructions.cpp`. Every text call is a fixed push sequence, so the x, y,
colour, mode and string offset come out mechanically, and the offsets resolve
against a byte dump of the same segment. 152 strings typed by hand would be 152
chances to mistype a line of the game's own documentation and never notice -
and this project has twice found an answer in these slides that it was deriving
the hard way, so the text being exactly right matters.

The structure is a straight run, not a dispatch: each slide draws, then waits at
`1b2e:0e37(30)` - the professor's key-wait with a thirty-second give-up - and
branches on the code:

    2   leave          { ESC }
    5   previous slide { Up }
    _   the next one

**Slide 1's "previous" arm jumps to its own wait.** There is no slide before it,
so the original clamps rather than wrapping, and it does so by jumping back into
the wait it is already in. Reading the jump targets is what proved the model:
slide 2's arm goes to slide 1's draw, slide 3's to slide 2's, and slide 1's
to itself.

The illustrations turned out to be free. They index the ball table at
`DS:0x1da6 + 4 * type`, so they are atom TYPES and the port already has every
sprite; only two are the slideshow's own, `TESTUBE1.CSP` and `TESTUBES.CSP`,
and the port already loads both.

The background is the CLASSROOM - `1b2e:0a11`, projector slide and all, the
same scene the briefing uses. That is why every x in the table sits between 76
and 244: the sheet spans 74..246. The two navigation lines are the exception,
centred over the whole screen below the sheet.

**8 pixels of 64,000** against a captured original slide, and they are the
professor's pointer mid-wave. The text is a 100% ink match - 10,734 pixels, the
same set.

### Credits, and the game confirms the project's two founding assumptions

`1b2e:411b` is the same screen as the Instructions with a different table -
four pages on the classroom scene, the same two navigation lines, the same
`2 leaves / 5 back / anything forward` key rules - so the same generator
emitted it, parameterised by which disassembly to read.

Two people and a technical note, and the note is the find. Page 3 says:

    Tubes was written in Borland Pascal v7, and uses a planar
    320x200x256 for the multiple pages.

**Borland Pascal 7 and Mode X, from the program's own mouth.** Both have been
in `CLAUDE.md`'s first paragraph since the project started, and neither was
ever confirmed by anything but inference from the compiler's runtime and the
CRTC writes. The game had been saying so all along on a screen nobody had
opened.

The credits also needed one thing the slideshow did not: a FONT per item. The
Instructions set TINY6X8 once and never change; the credits alternate, names in
the heading font and roles in the small one. The generator now tracks the
running `2000:3fab` and emits it per item.

And a bug the second screen exposed in the first: the two navigation lines are
drawn ONCE, before the first slide, and nothing clears them - so they are on
screen for every slide. Extracting them into slide 1's table put them on slide
1 only. They are `kInstructionNav` now, drawn by the screen rather than by a
page, which is what the original's control flow actually describes.

## 2026-08-01 - the classroom's last three animations, and the end of the map

A session against `PLAN.md`'s tail, in order, and the pattern that ran through
it: **every lead this project had written down pointed at the wrong routine,
and each one was wrong in a way that looked right.**

**The projector screen roll-down** was filed under `1b2e:0a11`'s six-frame
animation gated on `DS:0x210e`. That animation moves the SLIDE. The screen is
rolled down by `1b2e:0510`, the routine that builds the classroom from
nothing, and nothing gates it - 15 frames of `Delay(3)` over a word table at
`DS:0xb9c` that overshoots its resting height by five and comes back. What
varies is who CALLS `1b2e:0510`: the Instructions and Credits always, the
briefing only on `wave = 1` and not after a Continue, the other three screens
never.

**The professor's mouth** was filed as "a fourth arm of `1b2e:0656` that has
not been found". It is not an arm of anything. `TALK1..5.GFX` belong to
`1b2e:0cd1`, a SECOND key wait that every screen runs before the one this
project already knew about:

    k := 1b2e:0cd1(bursts);                  { he talks }
    if k = 3 then k := 1b2e:0e37(seconds);   { it timed out - now he waves }

The parameter counts BURSTS of `Random(4)+4` mouths, not frames. Of the two
flags the plan asked about, `DS:0x20c8` is written by nothing but the start-up
clear - so `1b2e:0656`'s clap arm is unreachable in the shipped build and
`CLAP1..3.GFX` are never drawn - and `DS:0x20e3` is set by `1000:9499`.

**The joke slide** the player had reported was `1b2e:084e`, which this file's
own notes had already quoted as "a one-in-twenty easter egg, not ported"
without anyone asking what the egg was. `FLASH.GFX` settles it on sight: 172 x
132, the slide rectangle exactly, and it draws Lanny holding his coat open over
an "AM" T-shirt.

### The 144 pixels, and the method note that cost two sessions

The cutscene's residue had been written up as "the original still holds the
difference and the port rebuilds", and the fix was expected out of the video
page bookkeeping. The page bookkeeping was read - four 16000-byte pages, page 3
the clean backdrop everything erases from, `1b2e:0f46`'s second toggle pinning
a lone track to the shown page - and built on a two-page model, and it measured
WORSE: page 4 went 144 to 148 and 176 came back on the others.

That regression is what sent me to the pixel values, which nobody had read:

    x 90..97, y 164..187      original: (0,0,0) x 144
                              port:     the base pose's greys

The original was BLACK there and the port was drawing - the opposite way round
from the write-up. `1b2e:0f46` blits through `2000:389d`, the OPAQUE thunk, so
a 28 x 66 frame's transparent bottom rows land as colour 0 over the base pose.
The frame lists were loading masked and the figures lived in an overlay that
cannot tell "wrote black" from "wrote nothing". **0 pixels on all five pages,
nothing masked, 42 captures.** A pixel count says where; the values say what,
and that is now in `CLAUDE.md`.

### `1000:9499`, and the map is finished

The wave-75 ending was the last unread screen. The trigger is two instructions
inside the progression block - `if wave >= 75 then RegisteredEnding; wave :=
wave + 1` - so it fires on CLEARING 75, and a replay does not re-run it. Its
first act sets `gameOver` through the static link, which is why the session
ends the moment it does. It is the classroom again with `DS:0x20e3` set: the
jumping arm, and this is the only caller in the program that reaches it.

Twelve strings still got a generator. The game spells it "prove the existance
of the new elements", and there is a test asserting the misspelling survives.

`--wave 75 --make-save FILE` writes a save to reach it by playing, and it
builds the record through the engine because a save carries `WaveProgress`:
wave 75 with wave-1 counters is the warp these notes warn about, not a wave 75.
It writes to the path given and never to the game directory.

### What playing it found that opening it could not

Four bugs, all from the player and none reachable from a harness flag:

* Enter on a splash dropped you at the menu. `1b2e:11b0` is a `void`
  procedure - it uses each splash's key only to decide whether to run the
  SECOND splash and throws it away, so the cutscene plays whichever key ended
  it. Both boot gates turned out to be `ParamCount` (`2685:08aa` reads the
  command tail's length out of the PSP), not a key at all;
* a one-frame beaker at (0,0) before the cutscene's fade - mine, from routing
  the pre-fade scene through a page whose B track is zeroed;
* the professor's mouth left hopping beside him during the ending, because the
  stats screen's talk clock kept running under the jump;
* the high score ENTRY screen showing the classroom through its panel.
  `1000:96db` calls neither `1b2e:0656` nor `1b2e:0a11` - its whole backdrop is
  a blackboard, the roller bar at y 26 and one filled rect, and the port had
  been drawing the whole scene behind it.

The last two came out of a real wave-75 clear. Nothing in the program is unread
now except `.BIN`; what is left is the player's enhancements.

## 2026-08-01 - the Graphics Options screen, on the rebinding screen's terms

The first of the two enhancements in `PLAN.md` section 5, and the second screen
in this port that is **not** a transliteration. The argument is `input.h`'s,
one layer over: the original picks an input DRIVER because DOS gave it no
abstraction over an XT keyboard and a gameport, and SDL *is* that abstraction -
so the display is the same case, since Mode X was the only mode the original
had and `SETUP.EXE` owned whatever choice existed.

Five rows: Display (windowed / fullscreen desktop), Window Size (Fit, or 1x..6x
pinned), Pixels (square, or the 4:3 a 1994 monitor showed), Vertical Sync and
Scanlines. Every change applies and saves the instant it is made, because the
point of a display option is seeing what it does and nothing here can leave the
game in a state the player cannot get out of. `--graphics` opens it.

### The transliterated table stays the image's

The one thing that needed deciding was where the menu item goes, and the answer
is that `kMenuPages` does not change. It is the DGROUP 0x00ca data and it still
reads four items ending in Exit. The port's page is `kOptionsPagePort`, a
separate object with the Graphics row inserted before Exit, and `menuPage()` is
what every layout, navigation and selection path reads. A test asserts both:
the image's row is untouched, and every OTHER page comes back as the image's
own object rather than a copy.

**It costs a layout shift and that is recorded rather than glossed.** Five rows
at the same 26-pixel pitch give `(180 - 26*6) div 2` = 12 where four gave 25,
so page 6 is the one menu page the port no longer renders pixel-identically.
Pages 1 and 3, which measure 0.00%, are untouched.

### Where the options actually live

All of it is render-side, which is the rule the frame-rate item in section 5
sets: a display option changes how a frame is PRESENTED and never how one is
computed, because the fixed 16.11 Hz step is load bearing. The scale and
letterbox arithmetic went into `input.cpp` with no SDL in it, so
`presentRect(winW, winH, g)` is unit-tested over 6800 window sizes for the
invariant that the picture is always a whole number of units - `tubes-tests`
links no SDL at all, and that is the whole reason it could be tested.

Two limitations, written down rather than discovered later:

* **4:3 does not give clean pixel rectangles.** 200 source rows over
  `240 * scale` is 1.2 rows per source row, so at 3x they come out three and
  four pixels tall in a repeating pattern. Inherent to nearest neighbour on a
  320x200 image, and the reason square stays the default. A first draft of the
  comment claimed the opposite and was corrected before it could read as
  settled;
* **scanlines are drawn over the presented image, not into the framebuffer**,
  so `--screenshot` never shows them. Deliberate: a capture has to stay
  comparable with the original.

`--scale N` is also no longer written into the saved settings. It sizes the
window for one run - a capture script must be able to pin a size without
changing what the player chose - and since the screen saves on every keystroke,
a flag that landed in the struct would have become permanent the moment the
player toggled anything. Leaving the setting at Fit inside a window sized to N
costs nothing, because Fit picks the largest whole multiple that fits, which
is N.

714 checks / 0 failures, `--demo-trace` unmoved.

## 2026-08-01 - wave 8 could not be passed: one counter, read by nothing

Reported from play: wave 8 asks for marked atoms, they clear on screen, and the
objective never moves. It was real, and it was worse than one wave - **every
mode-6 wave in the table was unfinishable**: 8, 16, 20, 28, 38, 41, 51, 58 and
64, plain, covered and Xenon-ringed alike.

`1000:24db` onward is the whole rule and the listing is in the notes now. A
marked cell that also carries an objective marker consumes the marker at
`1000:251f` and then, through **two** static links - out of the fade pass,
through `1000:3a67`, into `1000:9e53`'s frame - decrements `[BP-0x1f4]`, the
live objective counter, guarded by a `JBE` so it never wraps.

The port did the first half and dropped the second. `Board::fadePass` cleared
the marker and incremented `objectivesCleared_`, and **nothing anywhere read
that member** - one test asserted on it and that was all. So the counter never
reached zero, and `1000:5cff`'s wave-complete test could not fire.

Three things worth keeping from how it was found and fixed:

* **the note was already right.** `docs/reversing-notes.md` had said "clears
  the marker and decrements a wave counter at `9e53`'s `[BP-0x1f4]`" for
  months. The rule was read correctly and then not wired up, which is a
  different failure from a misreading and needs a different guard: a value
  computed and never consumed;
* a sweep for exactly that - every member declared in the four gameplay
  headers, counted across `src/` - turns up only `fallHeight_` (documented as
  retained for callers) and `moveTimer_` (written in `startWave`, read
  nowhere). Neither is a rule. So this was the only dead one;
* and the other four modes were checked rather than assumed. Mode 2 and 3
  decrement in `creditRun`, mode 4 in the dispenser at `1000:4b31`, mode 5 in
  `removeCrystalAt`. A probe over all 75 waves also confirms none starts with
  a counter already at zero, so no wave is trivially complete either.

The fix is an observer, matching the Crystal's two, because that is the honest
translation of a reach the original makes through static links between frames
that the port does not share.

The test plays all nine waves out - finds each marker, forms a run through it,
and requires the wave to COMPLETE rather than just the counter to move. Stubbed
back out, it fails 11 checks; that check was run rather than assumed.

731 checks / 0 failures, `--demo-trace` unmoved.

## 2026-08-01 - Chains never reset between waves: one byte, held twice

Reported from play: the Chains counts did not make sense as the waves went by,
and a save carried from the port into the original started a wave with chains
already on it.

Both halves are one bug. `-0x17c` is a single byte in the original. The HUD
prints it every frame - `1000:5753` reads it straight into `Str(chains:3)` -
the stats screen displays the same store as "Molecule Chains", and that screen
zeroes it at `1000:8ee4` after adding it to `-0x14e`. A full scan of segment
`1000` for the offset settles the rest: it is written in **exactly three
places** - zeroed for a new game at `1000:a4e4`, loaded from the save at
`1000:a553`, and zeroed at `8ee4`. **There is no per-wave reset in the wave
setup at all. The stats screen is the per-wave reset.**

The port splits the simulation from the screens, so that byte lives twice:
`Game::chains_` and `SessionTotals::chainsThisWave`. `buildStatsScreen` zeroed
the screen's copy - correctly, at the faithful position between the two lines,
with a test asserting it - and nothing ever zeroed the game's. Everything
downstream followed from that:

* the HUD's Chains carried across waves;
* each wave's "Molecule Chains" was the session's running count, not the
  wave's;
* "Total Molecule Chains" summed those running counts, so it re-counted every
  earlier wave - wave 3 of 5, 3, 2 chains reported 10 + 5 rather than 10;
* and `saveTo` writes `chains_` to `+0x28`, so the inflated figure went into
  the file. The original was reading the field correctly; the number in it was
  wrong.

The fix is `enterStatsScreen` in `session.h`, the one place the two copies are
allowed to meet. It takes the live counter **by reference**, so a caller cannot
take the wave's chains without also clearing them, and the zero still happens
inside `buildStatsScreen` at the original's position - the reference just
carries it back.

Worth noting what did NOT catch this. `testStatsScreenAccumulatesExactlyOnce`
has asserted the accumulate-and-zero for months and passes; it tests the screen
side, which was right. The bug was entirely in the crossing, and the crossing
was four lines in a lambda in `main.cpp` that no test could reach. That is the
same shape as the marked-atom counter earlier today: the rule was read
correctly and the wiring dropped half of it. Both are now behind a function
with a test rather than a step someone has to remember.

742 checks / 0 failures, `--demo-trace` unmoved.

## 2026-08-01 - `.BIN`, the last undecoded resource, is the shareware exit screen

`TUBESEND.BIN` was the one thing left in the archive that nothing had read. It
is a **raw DOS text-mode screen dump** - 80 x 23 character/attribute pairs, the
format a Pascal program `Move`s to `0xB800` to print a banner and quit. 3,680
bytes is 1,840 cells and 23 rows rather than 25, because the bottom two are
left for the shell prompt to land under the art. `tools/bin_decode.py` renders
it in CP437 or in colour.

The content is a registration pitch: the Software Creations order address,
phone, fax and BBS, and a box listing what registering buys - *50 more exciting
waves, 2 helpful new atoms, 5 gorgeous new backgrounds*.

**Nothing loads it**, and that is the finding rather than a loose end. The
string "TUBESEND" appears exactly once in the entire game directory - in the
archive's own directory entry. Not in `TUBES.EXE`, not in the unpacked image,
not in `SETUP.EXE` or the drivers, and the unpacked image names 112 resources
of which none has a `.BIN` extension. It is the SHAREWARE build's sign-off,
carried in an archive shared between the two editions. A resource no executable
names stops being a mystery once you work out it belongs to a different
executable.

It settles two things `PLAN.md` had carried as open:

* **shareware is 25 waves.** The table here has 75 arms and the pitch says
  registering adds 50. The port had this written down as "Hypothesis, untested"
  - it is now corroborated by the binary's own documentation, which is the
  second-rank authority `CLAUDE.md` names. That file says the game's own text
  has three times held an answer being derived the hard way; this is the
  fourth;
* **the copy in `..` is the REGISTERED edition**, and not on the strength of
  one `RegisteredEnding` string any more. All three advertised additions are
  present and counted: 75 waves, `AFADE*` and `GLDFADE*` for the two atoms,
  `GAMEBG1..10` for the backgrounds.

The port renders none of it. Nothing in this build shows the screen, so drawing
it would invent a screen the original never displays - the one thing this port
does not do. It is recorded as a preservation finding and as a lead: if the
shareware `TUBES.EXE` names `TUBESEND.BIN`, then it is a real screen of that
edition and porting it becomes faithful.

Also noted for the edition-detection work: the count of `GAMEBG*` and the
presence of `AFADE*`/`GLDFADE*` are both readable from the `.RES` alone, which
is a better tell than anything in the executable.

## 2026-08-01 - both editions, measured: shareware is 25 waves, and the archive is the same

The player supplied both archive.org items, so the "Supporting both editions"
section stopped being a plan and became a measurement.

**Shareware is 25 waves.** `tools/count_waves.py` counts `1000:86b8`'s dispatch
arms out of an unpacked image without disassembling it - the objective dispatch
is a linear if-else chain, one arm per wave, all the same shape. **75 in `..`,
25 in both shareware images**, and the exit screen's "50 more exciting waves"
is exactly that difference. The hypothesis this project has carried for months
is closed.

**The `.RES` does not distinguish the editions at all.** `..`'s `TUBES.RES` is
**byte-identical** to the shareware download's - same md5. Ten `GAMEBG`, both
special-atom fade families, `TUBESEND.BIN`: the shareware archive has all of
it. So the edition lives entirely in the executable, and since the port IS the
executable there is nothing to detect and nothing missing. Reading a shareware
install already works today.

That also **corrects yesterday's reading**, written up here a few hours ago:
"the copy in `..` is registered, and all three advertised additions are present
and counted". Two of those three premises are false - a shareware archive
carries the backgrounds and the atoms too. The conclusion happened to be right;
the argument was not. What actually says registered is `PRIZE.GFX`, the ending
text with its `existance` misspelling, and 75 arms. Corrected in the notes
rather than quietly deleted, because the shape of the error is the point: three
counts that all agreed, and all three were counting the wrong thing.

`msdos_Tubes_1993`, listed as the registered version, is **not** - it is a
25-wave shareware build with `IMPULSE.DAT` in place of the Software Creations
splash. An Impulse re-release. The player's own disc is the only registered
image of the three.

### Two wrong counts before the right one, both from the tool

Worth keeping, since the scanner is now in `tools/`:

* a flat 11-byte arm stride reported **62** arms for the image known to hold
  75. The arms nearest the tail reach it with a 2-byte `EB` rather than a
  3-byte `E9`, so the run broke at the first short jump;
* allowing both jump encodings reported **74**. The last arm has no `JMP` at
  all, the tail following it directly, so it is 8 bytes with `JNZ 4`.

Both were plausible. The only reason neither was believed is that this image's
true count was already known from decompiling it arm by arm - **validate a
scanner against the one input whose answer you already have**, and read the
disagreement rather than adjusting the number.

### What is left of shareware support

One choice - offer the 25-wave progression as a MODE - and one thing unread:
whether the shareware executable's spawn distribution rolls types 9 and 10 at
all. The sprites are in every archive, so that is the only place a shareware
run could differ in RULES rather than presentation. Also confirmed: both
shareware images name `TUBESEND`, so the exit screen is a real screen of that
edition, and the registered one is not.

## 2026-08-02 - the shareware build is a different program, and yesterday's conclusion was too small

The player ran the shareware edition to remember how it differed, and it
differs a great deal more than this log claimed 24 hours ago.

Yesterday's measurement was right and its conclusion was wrong. `TUBES.RES` is
byte-identical between the editions - that held - and from it came "the edition
lives entirely in the executable, so there is nothing to detect and nothing
missing; supporting shareware can only mean offering the 25-wave progression as
a mode". The premise bounded *where* the difference was. It said nothing about
**how big** it was, and it was read as though it had. All of the work being in
the executable turns out to mean exactly that: **all** of it.

What the shareware build actually has, reported from play and then corroborated
against `SW_UNP.EXE`'s strings:

* an extra main-menu item, **Preview Registered**, playing waves from the
  registered additions with the **Mischief Crystal wave first**;
* **no Bonus and no AntiMatter in normal play**, though both sprites are in its
  identical `.RES`. They appear in the Preview;
* an **Ordering Info slide deck**;
* a **registration deck on the way out** - quitting shows the pitch first;
* **`TUBESEND.BIN` dumped to the DOS screen** on exit;
* no wave-75 ending, which follows from 25 waves.

Every one of those names itself in the binary: `Preview Registered`, `Ordering
Info`, `Order by BBS (OPEN Door 5)`, `You can't stop now!`, `TUBESEND.BIN`,
`ExitText Resource Error!`. 666 unique strings against the registered image's
635, and the registered-only set is almost entirely the wave-75 ending.

**`Lanny is 1/3 of the way to his goal`** - 25 of 75, stated by the game, in
agreement with a count taken off `1000:86b8`'s dispatch arms by an entirely
independent route. Fourth time the game's own text has held an answer this
project was deriving the hard way.

Two more found while checking the reports, neither of them visible from play,
and both change the shape of the work:

* **the Instructions are re-wrapped between editions** (`will change every 45
  seconds.` against `change every 45 seconds.`), so `src/instructions.cpp` is
  edition-specific rather than shared. `gen_instructions.py` is already
  parameterised, so it is a second extraction and not a second tool;
* **the background lists are split rather than shortened**. The registered
  image names only the `GAMEBG` prefix and builds names numerically; the
  shareware names the prefix plus `GAMEBG5/6/7/9/10` - the *"other five
  backgrounds"* of the Preview blurb. Which list the literals are is
  **written down as a guess**, because a five-and-five split matching a
  marketing line is the kind of coincidence this project has been burned by.

Decided: a `--shareware` switch, and **savegames and high scores separated
between the modes** - the player's call and the right one. In 1994 the editions
were separate installs and could not collide; one binary that can be either
creates a hazard the original never had, and it is the only part of this work
that can damage a player's own files. So the save and high-score routines get
read before any write path exists, and a mismatched save is refused rather than
reinterpreted.

Also settled, for the exit screen the registered edition never shows: the
mechanism should be to write the banner **to the real terminal** on exit, which
is what 80 x **23** rows are for - the DOS prompt lands in the gap. For
registered it cannot be faithful, but it need not be an invention either, since
the identical `.RES` means the banner is sitting in that player's own files
unreferenced. It goes behind a port-owned affordance, same category as the
planned fifth Credits page. **The port may show the player their own data; it
may not claim the original showed it.**

Nothing ported yet. `PLAN.md` carries the order of work, which starts with
importing `SW_UNP.EXE` into its own Ghidra project and running `MapProgram`
over it the way the registered image was mapped. No code changed, so the test
count is unmoved at 742.
