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
