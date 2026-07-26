# Roadmap

Cross-session tracking. `CLAUDE.md` is how to work in this repo,
`docs/reversing-notes.md` is what the formats are, `docs/worklog.md` is what
happened when. **This file is what is left to do.**

Update it when something lands or when a plan turns out to be wrong. A stale
plan is worse than none.

---

## Where this stands

Roughly **40%** of the port, with the risk front-loaded and already retired.

The split is lopsided on purpose:

- **Reversing: ~70%.** Every asset format is decoded. The program structure is
  mapped end to end. Music is provably correct at the register level. The
  playfield geometry is measured rather than guessed. Nothing is still
  resisting analysis.
- **The game itself: ~25%.** One screen exists, with a partly-wrong mechanic
  on it. Every other screen, every special atom, every animation, all sound
  effects, scoring and the difficulty curve are absent.

What remains is mostly voluminous rather than uncertain. The single genuine
unknown left is how the dispenser tubes route atoms.

---

## Done

| Area | State |
|---|---|
| `TUBES.EXE` unpacking (LZEXE v0.91) | done |
| `.RES` container + Okumura LZSS | done, 239/239 payloads exact |
| `.CSP` compiled sprites | done, 108/108 |
| `.GFX` raster images + `.PAL` | done, 73/73 |
| `.816`/`.88` fonts | done, 5 |
| `.SFX` digital audio | done, 24/24, confirmed by ear |
| `.SCR` demo recording | done |
| `.MUS` FM music | done, all 10; register stream matches the reference byte-for-byte |
| Renderer: 320x200 indexed, integer scaling | done |
| Music playback: sequencer + Nuked-OPL3 via SDL audio | done |
| Program map: every interface stage identified | done |
| Playfield geometry: 6 x 5 grid, pitch 18 x 13, origin (107, 134) | done |
| Beaker rendering | done |

Never examined: `.SPR` (2), `.BIN` (1). `.ANM` (1) is known to belong to the
developer splash but is not decoded.

---

## Known wrong - rebuild, do not extend

Things currently implemented on assumptions the binary has since contradicted.
Listed first because building on them wastes work.

1. **The dispenser path.** `src/game.cpp` still has atoms falling straight down.
   **The real path is now observed** (Experiment 2, `docs/reversing-notes.md`):
   spawn at `(303,186)`, placed at the bottom of an outer vertical tube at
   `y = 187`, ascend at constant x, cross the top along `y ~ 0..3`, then descend
   into a play column. Tube columns seen: 34, 58, 107, 179, 197, 246, 270, 294 -
   the same x values as the tube artwork, plus 294. **Atoms move 4 px per frame**,
   measured with a frame-synchronised sampler - note the test tube moves 6 px per
   frame, so the two differ.
2. **The test tube holds one atom.** It holds **five** - both published
   descriptions of the game say so outright. `TESTUBE1/2/3` are tipping frames,
   not capacities (see below), and the varying capacity is far better explained
   by `FILLBALL`, which *permanently reduces the tube by one atom*, than by
   difficulty. Treat the 5/3/2-by-difficulty figure in `src/game.cpp` as
   unfounded.
3. **Scoring and pacing.** `kScorePerAtom`, `kChainBonus`, `kSpawnInterval`,
   `fallSpeed` are invented. Real values are in `1000:3a67`.
4. **Atom colour count and the special balls - now measured.** The engine's 8
   flat colours are wrong. There are **19 ball types**, read out of the live
   sprite tables at `DS:0x1da6` (balls) and `DS:0x1df6` (fades), 19 entries
   each at stride 4:

   | type | what |
   |---|---|
   | 1-7 | the ordinary colours: Redium, Greenium, Bluium, Cyanium, Purplium, Yellowium, Pinkium |
   | 8 | **Flashium** - the wildcard, with **no sprite of its own**: its table slot is *rewritten* ~4x/sec, cycling types 1..7 in order (measured, with a static control). The cell value stays 8, which is why it always clears with `FFADE` |
   | 9 | AntiMatter - destroys the surrounding atoms |
   | 10 | Bonus - travels fast, turns into Flashium when caught, grants a bonus drop |
   | 11 | Xenon - inert, will not react with any colour |
   | 12 | Multiplier - fills the test tube with **random** balls |
   | 13 | Evil Multiplier - fills the test tube with Xenons |
   | 14 | Convertor - turns the atoms it lands on into Xenons |
   | 15 | Blocker - fills the beaker column it lands in with Xenons |
   | 16 | Filler - permanently reduces tube capacity by one |
   | 17 | almost certainly the immovable atom `FILLBALL` parks at the bottom of the tube - near-black, no fade family, and play reports a "sticky black one that cannot be dumped" |
   | 18 | the crystal ("mischief crystal"), static sprite is `CRFADE1` |
   | 19 | `MYSTBALL`, unidentified |

   Only 1-10 and 18 have fade families, so only those are cleared by matching.
   Cell values are therefore **not** limited to 1..7 - the table runs to 19.
   Behaviours for 8-16 come from published descriptions whose ordering matches
   the measured type order exactly; 17 and 19 are unknown.

---

## Next

### 0. START HERE: switch to the DOSBox-X debugger

Static disassembly has hit its limit on this function. Five self-corrections
in one session, every one caused by a tool being wrong rather than the binary
being obscure - a scan threshold too high, a correlation window too narrow, a
regex that silently dropped negative displacements, two structures assumed to
share a base, a shape measured from three sample rows. See `docs/worklog.md`.

DOSBox-X is already installed and the game runs under it. Watching memory
answers directly what inference keeps getting wrong.

**What to watch.** The atom array is 12 records of 28 bytes, based at the
frame of `1000:9e53` minus `0x163`, reached from `1000:3a67` through the
Pascal static link at `[bp+4]`. Known fields:

| offset | field |
|---|---|
| +0x00 | x |
| +0x02 | y |
| +0x0b | colour / sprite index |
| +0x14, +0x16 | saved x, one per video page |
| +0x18, +0x1a | saved y, one per video page |

**What to do.**

1. Break in `3a67` (image offset `0x3a67`, file offset `+0x2200`) and read
   `[bp+4]` to get the live base. Everything else is an offset from it.
2. Set a memory write breakpoint on a record's `+0x00`. Whatever traps is the
   code that moves atoms - the thing a whole session of grepping did not find.
   **Not possible over the GDB stub as built** - see Experiment 3 below.
3. For speed: run, hold **B**, and watch the write rate change. That settles
   whether speed is a record field, a divisor on a shared frame counter, or
   something else, by observation rather than inference.

**Tooling: `jdmichaud/dosbox-mcp`** over `lokkju/dosbox-x-remotedebug`.
**Built, installed and verified end to end** - see `docs/debug-rig.md` for where
it lives, the config choices, and the footguns. The game runs headless and
reaches its splash screens; segment bookkeeping is confirmed by two independent
routes. 24 tools: memory read/write, registers, linear-address breakpoints,
stepping, key injection and **save states**.

**One conf setting is mandatory:** `[dos] dos idle api = false`. Without it the
game prints "This game requires complete control of your computer" and stops -
it probes `INT 2Fh AX=1680h` for a multitasker at `21ea:0249` and DOSBox-X
answers. Details in `docs/reversing-notes.md` under "Startup checks".

The save states are the point. Every wrong turn last session came from an
unrepeatable inference. A save state turns each question into a controlled
experiment: identical starting state, one variable changed, re-runnable.

**Debug the unpacked image, not the shipped one.** `assets-extracted/TUBES_UNP.EXE`
is what Ghidra analysed and what every address in these notes refers to. The DOS
drive presents it as `TUBES.EXE`; the shipped LZEXE-packed original is there as
`TUBESPKD.EXE` for reference only. Run the packed one and break-on-exec stops in
the decompressor stub, whose `CS` belongs to the packer - so the mapping below
would be measuring the wrong program.

**The segment bookkeeping is now settled, statically.** Ghidra's base segment is
an arbitrary `0x1000` and DGROUP is Ghidra segment `0x2785`, so DGROUP sits
`0x1785` paragraphs into the image - file offset `0x1785 * 16 + 0x2200 header =
0x19a50`. Reading the waypoint-target table there out of the file gives
`104, 122, 140, 158, 176, 194`, **byte-exact** against the measured values. Six
values matching by chance is not credible, so this confirms both the DGROUP
segment number and the file-offset formula without an emulator.

What remains at runtime is only the load segment `L`. Entry `CS:IP` in the
unpacked header is `0000:aaba` - image-relative segment 0 - so `CS` at the
entry breakpoint *is* `L`, and:

        live_segment = L + (ghidra_segment - 0x1000)
        DGROUP       = L + 0x1785

Confirm it two independent ways, and require them to agree - `bringup_tubes.py`
does exactly this:

- **A.** Break at entry, read `CS`, check `EIP - CS*16 == 0xaaba` (proves we
  stopped in the right program), then read back `DS:0x26` at `L + 0x1785`.
- **B.** Free-run, then scan conventional RAM for the 24 bytes the file holds at
  `DS:0x1a` - verified to occur exactly once in the image:

        8f 00 7d 00 6b 00 c5 00 b3 00 a1 00 68 00 7a 00 8c 00 9e 00 b0 00 c2 00

  Its address must land where A predicts.

A alone could be a wrong-but-self-consistent guess about the load address; B
alone finds an address without proving what it is.

**Careful - the column table is not stored ascending**, and the summary above
glossed over it. `docs/reversing-notes.md` has this right already: the six x
values live at `DS:0x1a`..`DS:0x24` as two *descending* triples,
`143, 125, 107, 197, 179, 161`, and columns 1..6 index them in the order
3, 2, 1, 6, 5, 4. The ascending `107, 125, 143, 161, 179, 197` is the mapping
*after* that permutation, not a run of bytes to look for. Confirmed against the
image: `DS:0x1a`=143, `0x1c`=125, `0x1e`=107, `0x20`=197, `0x22`=179,
`0x24`=161. Anything scanning for the ascending form finds nothing - read the
notes before inventing a signature.

**Experiment 3 cannot be run as written.** The GDB stub
supports **software execution breakpoints only**; `Z2`/`Z3`/`Z4` watchpoints
are declined outright (`gdbserver.cpp`, `handle_breakpoint`). There is no
memory-write breakpoint over the wire. DOSBox-X's internal debugger *does*
have one (`BPM`, and this build has `C_HEAVY_DEBUG=1`) - it is just not
exposed. `docs/debug-rig.md` sizes the three routes; the good one is a small
patch mapping `Z2` onto the existing `CBreakpoint::AddMemBreakpoint`.

Experiments 1, 2 and 4 need none of that and can run today.

**Experiment 0 - the input bit map. DONE, and it validated the `.SCR` oracle.**
The apparent contradiction between `SETUP.CFG`'s scancode order and the `.SCR`
bit assignments was not real: `KEYBOARD.DRV` maps slot *n* to bits
`1, 4, 8, 2, 16, 32`, not the identity, which makes both readings correct at
once. Measured live with six key injections, every bit matching the prediction
from the driver's disassembly:

| bit | control | key |
|---|---|---|
| `0x01` | up | Up |
| `0x02` | down | Down |
| `0x04` | left | Left |
| `0x08` | right | Right |
| `0x10` | button A | **Left Ctrl** |
| `0x20` | button B | **Left Alt** |

So the `.SCR` bit table - previously only inferred from run-length statistics -
is confirmed against the input handler, and **`DEMO.SCR` is safe to use as the
correctness oracle in §5**. Details in `docs/reversing-notes.md`; the runnable
experiment is `exp0_input_bits.py` in the tooling directory.

The live key mask is one byte at driver offset `+0x1f`, and the driver's base
comes from the **INT 9 vector** at linear `0x24` - do not hardcode it. Reading
that byte is the cheapest possible way to confirm any input-related theory from
here on.

**Experiment 1 - the live atom array. MOSTLY DONE.** `12 records x 28 bytes` is
confirmed, and so is `(303, 186)` as the spawn marker. Found by signature rather
than frame arithmetic: search RAM for `2f 01` plus a plausible y, and ten hits
land at an exact 28-byte stride spanning precisely twelve slots. The two slots
*not* on the marker matched the two atoms visible on screen in the same halt -
that correspondence is the proof. `exp1_atom_array.py` in the tooling directory.

**Two corrections it forced:**

- `+0x1e`/`+0x1f` are not atom fields (`0x1f` = 31 > 28); they are the test
  tube's. See the record table above.
- **`1000:3a67`'s first call is the difficulty-selection screen**, not the game
  loop - its parent frame holds `Tubes 101`, `Tubes 201`, `Tubes 301` and `Exit`.
  So breaking at `3a67` and reading the static link gives a frame containing no
  atoms, and `parent - 0x163` scores 3/12 there, i.e. noise.

**What is left:** the stable frame-relative offset. `parent - 0x163` is still
unverified. Re-arm the breakpoint *after* difficulty selection and read `DI` at
`3a73` during actual play, then compare against the base the signature search
finds in the same run. Two routes agreeing is the standard here.

Useful: `3a67`'s prologue is `enter 0x3c6, 0` and `mov di,[bp+4]` sits at `3a70`,
so breaking at **`3a73`** gives the static link directly in `DI` - no stack walk.

**Experiment 2 - watch atoms move.** Free-run and re-dump each frame. Is x
stepped by a constant, interpolated, or recomputed from elsewhere? Static
analysis says the fields are written whole rather than incremented, so this
should show what actually drives them.

**Experiment 3 - find the mover.** Set a write breakpoint on record 0's `+0x00`.
Whatever traps is the code a full session of grepping failed to find. This is
the single highest-value moment in the whole plan - **and it is gated on the
watchpoint work in Correction 2 above.** Do that patch before this experiment,
or fall back to step-and-diff.

**Experiment 4 - settle speed (controlled).** Save state with an atom mid-arc.
Then: load, run N frames untouched, record positions. Load again, run N frames
holding **B**, record positions. Same start, one variable. The difference *is*
the speed mechanism, measured rather than inferred. Repeat with a bonus atom
on screen for the second speed.

Also worth watching once attached: the beaker grid (three parallel arrays,
stride 6) to confirm 6 x 5 live, and the scoring counters, which are still
entirely invented in the port.

### 1. The dispenser and test tube mechanic

**How it actually works** (described by the user from play, and corroborated
by the binary):

- Atoms enter at the **bottom right**, travel **up** the right-hand side, arc
  over the **top** of the screen, and come back **down** - tracing the tube
  artwork rather than falling straight.
- The player slides the test tube along a horizontal rail and catches them.
  The tube **holds several atoms stacked**.
- **Button A** tips the tube, dumping **one** atom at a time into the beaker.
- **Button B** speeds an atom along, sucking it out of the tube faster.

The binary corroborates the entry point exactly. The 12 records of 28 bytes
initialise to **(303, 186)** - x=303 is off the right of the play area, which
ends at 245, and y=186 is the bottom grid row. That is the spawn point, not
the "parked" sentinel it was first read as. So the 12 records are the atoms in
transit along the path.

**Implemented so far:** the tube stacks, A dumps one at a time, B accelerates.
Capacity 5/3/2 by difficulty, inferred from the `TESTUBE1/2/3` sprite heights
of 65/42/27 at the 13px row pitch - which sprite goes with which difficulty is
not proven.

**The atom record is solved.** From the one record-indexed draw site:

        push word es:[di]        ; x      - record +0
        push word es:[di+2]      ; y      - record +2
        mov  al,  es:[di+0xb]    ; colour - record +0x0b
        call 1321:0905           ; Draw(x, y, sprite)
        ...
        mov  es:[di+0x14], dx    ; saved x, one slot per video page

So each of the 12 records is a free-moving sprite carrying its own position,
plus saved positions per page for dirty-rect erase (the page index lives at
`ds:0x2376`). 28 bytes: x, y, colour, and two saved pairs.

**There IS a waypoint table** - at `DS:0x26`, six words, immediately after
the column-x table:

        targets: 104, 122, 140, 158, 176, 194     (pitch 18)
        columns: 107, 125, 143, 161, 179, 197     (pitch 18)

The targets are the column positions minus 3, the same -3 the test tube is
drawn at. So the six waypoints are the six column stops.

A first scan of DGROUP reported "no waypoint table". That was wrong: the scan
required smooth runs of **8 or more** words and this table has **6**. The
filter excluded the answer. Worth remembering - a negative result from a
threshold search is only as good as the threshold.

### The atom movement state machine

From `1000:3a67` around `0x67bd`:

        cmp  BYTE es:[di+0x1e], 6      ; waypoint index
        mov  BYTE es:[di+0x04], 2      ; direction = right
        inc  BYTE es:[di+0x1e]         ; advance waypoint
        mov  ax, [di+0x24]             ; target = waypointTable[index]
        mov  es:[di+0x1f], ax
        ...
        cmp  al, 1                     ; direction 1 = left
        sub  WORD es:[di], 6           ;   x -= 6
        cmp  ax, es:[di+0x1f]          ;   reached target?
        mov  BYTE es:[di+0x04], 0      ;   yes: snap to target, stop
        cmp  al, 2                     ; direction 2 = right
        add  WORD es:[di], 6           ;   x += 6

So atoms step **6 pixels at a time** toward a target column, and y snaps
between two lanes: **187** at the bottom where they enter, and **68** at the
top, just above the test tube at 69.

### Record layout (28 bytes)

| offset | field |
|---|---|
| +0x00 | x |
| +0x02 | y |
| +0x04 | direction: 0 stopped, 1 left, 2 right |
| +0x0b | colour / sprite index |
| +0x14, +0x16 | saved x, one per video page (dirty-rect erase) |
| +0x18, +0x1a | saved y, one per video page |

**Corrected:** `+0x1e` and `+0x1f` are *not* atom fields - `0x1f` = 31 does not
fit in 28 bytes. They belong to the **test tube** struct and were merged in by
mistake. The atom record tops out at `+0x1b`, which fits exactly.

### Two structures - and the second one is the TEST TUBE, not an atom

Both reached through the Pascal static link from `9e53`'s frame:

| base | shape | what |
|---|---|---|
| `parent - 0x163` | 12 x 28 bytes | the atoms |
| `parent - 0x16a` | one struct | **the player's test tube** |

The single struct was read for most of a session as "the atom currently
travelling the arc". It is not. At `0x66f0` it only acts when its direction is
0, then calls the input driver (`ds:0x2352`, `ds:0x2356`) and branches on the
button bits. It is player-controlled.

Five things corroborate it, none of which fit a travelling atom:

- Its waypoint targets are 104, 122, 140, 158, 176, 194 - the six column x's
  **minus 3**, which is exactly the offset the test tube is drawn at.
- It moves 6 pixels per frame toward a target: a tube sliding smoothly between
  columns, not snapping.
- Button A (`0x10`) puts it into state 3.
- State 3 runs a 4-phase counter that indexes a **sprite pointer table**
  (`phase << 2` at `0x7b2f`) - an animation, and A is the dump action.
- Its y values are 68 and 187, and the tube hangs at 69.

### The test tube's state machine

| `+0x04` | meaning |
|---|---|
| 0 | parked at a column, accepting input |
| 1 | sliding left, `x -= 6` per frame |
| 2 | sliding right, `x += 6` per frame |
| 3 | tipping: 4-frame animation, one frame per 2 game frames |

Left and right also step the waypoint index at `+0x1e` (`dec`/`inc`), bounded
at 6, so the tube stops on column centres.

### `TESTUBE1/2/3` are tipping frames, not capacities

This follows directly, and **corrects an earlier inference**. The sprites are
22x65, 20x42 and 20x27 - a tube foreshortening as it tips over, not three
capacities for three difficulties. The capacity 5/3/2 currently in
`src/game.cpp` is therefore unfounded and should be treated as a placeholder.

### Atom speed: still not found, and static analysis is hitting its limit

Ruled out so far:

- **Not in the test-tube struct.** Travel is a flat 6 px/frame with the
  divider threshold and 4-phase limit both hardcoded, and the struct is the
  tube, not an atom.
- **Not in a sibling function.** All 18 of the `*28` atom-array accesses are
  in `3a67`; `86b8`, `8da5`, `8c38`, `9499`, `9111` and `96db` contain none.
- **The atom array's x/y are never incremented.** They are written whole by
  `mov` - the spawn at `0x6b94` sets `x = ax` and `y = 187`. So atoms are
  positioned from other state each frame rather than stepped.

That last point is the useful one: whatever drives atom position is computed,
not accumulated, so there may be no "speed field" in the record at all - the
speed could be a divisor applied to a shared frame counter.

**A lead from play: `GOLDBALL` travels the tube *very fast*.** So speed is not a
single global - it varies by ball type. Since the record's only type-ish field is
the colour/sprite index at `+0x0b`, the thing to look for is a **speed selected
by ball type** (a small lookup, or a branch on the type) rather than a per-record
speed variable. That also gives Experiment 4 a much better second condition than
holding **B**: compare a gold ball against an ordinary one, same save state.

**Recommended change of technique.** Reading one 9382-byte Pascal procedure
with nested frames has produced five self-corrections in a single session, and
every one came from a tool being wrong rather than the binary being obscure:
a scan threshold set too high, a correlation window too narrow, a regex that
silently excluded negative displacements, two structures assumed to share a
base. Static disassembly is past the point of diminishing returns here.

The better tool is the one already on this machine: **DOSBox-X's debugger**.
Set a memory breakpoint on the atom array and watch what writes it. That
identifies the code directly instead of inferring it, and it also settles
speed by observation - run the game, hold B, and watch the rate change.

**Do not** assume per-cell tile routing - the frame update contains no
arithmetic on the 13px row pitch outside the settled-grid draw.

#### Static furniture, from literal draw coordinates in `1000:3a67`

84 of the 126 sprite draws use literal coordinates:

| y | x positions | what |
|---|---|---|
| 13 | 58, 107, 197, 246 | upper tube arcs |
| 26 | 34, 58, 107, 125, 179, 197, 246, 270 | lower tube arcs |
| 134 | 103 | the beaker |
| 135 | 186 | unidentified |

All symmetric about screen centre 160 once the 16px sprite width is added.
The beaker at (103, 134) independently confirms the placement derived from the
grid geometry.

The test tube is 65 tall and its mouth meets the top of the beaker, so it
hangs at **y = 134 - 65 = 69**.

### 2. Presentation

Cheap and high-impact once the mechanic is settled.

- 66 fade sprites: 11 families x 6 frames. **The mapping is now measured** - see
  the 19-type sprite table in `docs/reversing-notes.md`. Indexed by ball type:
  types 1-7 the colours, 8 Flashium (`FFADE`), 9 AntiMatter, 10 Bonus,
  18 the crystal. Types 11-17 and 19 have **null** fade pointers, so they are
  never cleared by matching.
- `.SFX` through SDL audio, mixed alongside the OPL output. The match sounds are
  already mapped: eleven `.SFX` share the exact names of the eleven fade sprite
  families (`RFADE`, ... `FFADE`, `AFADE`, `GLDFADE`, `CRFADE`), so there is one
  per family. Reported from play: the sound is chosen by the colour the **stack**
  matched as, while each ball's fade animation follows its **own** type - so a
  mixed chain shows mixed animations under a single sound.
- HUD: `Chains` at top left and `Drops` at top right with two counters between.
  A "drop" is a **missed** ball - the `DROP` sound plays on a miss - so the
  measured `9 / 6 / 3` drop limits are the miss allowance per difficulty, which
  is what `Tubes 101 / 201 / 301` selects. Which name maps to which limit is not
  yet confirmed.
- Fonts are decoded but never drawn

### 3. Game rules

- **Scoring is solved**, from the in-game Instructions (`docs/reversing-notes.md`):
  vertical chain **250**, horizontal **500**, diagonal **1000** (two diagonals, so
  "4 chains"); chains = atoms - 2 (3 atoms = 1 chain, 4 = 2, 5 = 3); forming
  multiple chains at once applies a **chain bonus multiplier**; a Bonus atom adds
  **1000** to the Bonus Jackpot and awards it. `kScorePerAtom` and `kChainBonus`
  in `src/game.cpp` can be replaced with real values.
- **Atom speed is solved.** "Press Button B **or Down** to increase the speed of
  any atoms in the tube **directly above the test tube**." So the boost is
  positional - gated on the atom's column matching the tube's - and both B and
  Down trigger it. That also reinterprets the `.SCR` demo: its long runs of bit
  `0x02` are the player holding **Down to speed atoms**, not "dropping faster".
  `GOLDBALL` is additionally fast by type.
- **The test tube is a LIFO stack** - atoms leave from the top, so speeding a
  source tube is how the player controls which atom ends up on top. Capacity is a
  flat **5**; `FILLBALL` permanently adds an occupying atom to the bottom.
- **Lose condition:** dropping more atoms than the difficulty allows, in both
  modes.
- the remaining unknowns: wave objectives, and the difficulty-to-drop-limit
  pairing
- the difficulty progression - seeds `3, 30, 2, 0, 3, 8` plus globals 50 and
  25, stepping every 15 and every 20 levels, with level bands at
  30 / 60 / 75 / 90 / 95 / 101. Variables not yet named; trace them from
  `9e53` into `3a67` through the Pascal static link.
- **wave definitions.** A wave briefing carries an objective ("live through 30
  atoms"), a **disabled element** that still spawns but cannot be cleared, and its
  own **drop allowance** (11 on wave 6). So the measured 9/6/3 triple is the
  Endurance difficulty setting; Wave mode overrides it per wave. The
  disabled-element rule has no equivalent in the port yet.
- special atoms; Endurance vs Wave mode selection sits under a Game Mode menu,
  and saved games are filtered by mode
- save/load. `TUBES.SAV` is 960 bytes and very sparse. **Partly decoded and
  confirmed against the running game**: player name at `0x1e0` (Pascal
  ShortString), score u32 at `0x1ff`, wave number at `0x206`; `0x207` and `0x20d`
  match the wave's drop allowance and atom target from one sample. See
  `docs/reversing-notes.md`.

### 4. The other screens

All mapped, all mechanical. In player-visible order:

| Stage | Function |
|---|---|
| Software Creations splash | `21d5:007b` |
| Absolute Magic splash | `2178:00eb` |
| title / main menu | `1b2e:52bf` |
| blackboard cutscene / instructions | `1b2e:1651` |
| test-tube screen | `1b2e:2d63` |

### 5. Optional

Replay `DEMO.SCR` through the game loop as a correctness oracle. The input bit
layout already matches, so a recorded demo can drive the same update path -
useful once the mechanics are real.

---

## Portability

Running on other platforms is a goal in itself; PSP is the first candidate
because its homebrew scene is active and SDL is already available there, but
it is an example rather than the target. Keep these in mind while writing code
rather than retrofitting later. None of it justifies contorting the code now -
it justifies *not* painting into a corner.

- **SDL is the plan - keep it at the edge.** SDL *is* the portability layer;
  the point is to confine it to the platform boundary rather than thread it
  through the game. Today only `main.cpp` (window, input, loop) and `opl.cpp`
  (audio device) include it - 3 files of 16, and `screen.cpp` is not one of
  them, being a plain indexed framebuffer with a `toRgba()` at the end.
  This already pays off twice: `tubes-tests` links no SDL at all, and
  `--dump-regs` proved the sequencer correct with no audio device attached.
  If a target's SDL is missing or awkward, `main.cpp` and `opl.cpp` are the
  only files to rewrite and nothing reversed is touched.
- **Endianness is not a problem.** PSP is MIPS little-endian, same as x86, so
  the format decoders port unchanged.
- **The OPL core may be.** Nuked-OPL3 is cycle-accurate and correspondingly
  expensive for a 333 MHz MIPS chip. If it proves too slow, the register
  stream is the interface - swap the core, keep the sequencer. That separation
  already exists via `RegisterSink` and is worth defending.
- **Prefer integers and floats over doubles.** The PSP FPU is single-precision;
  doubles are emulated. `MusicPlayer` currently uses `double` for the tick
  accumulator, which is fine on desktop and easy to change later.
- **Watch memory.** 32 MB on the original PSP. Loading the whole `.RES` is
  fine (524 KB), but decoding all 73 backgrounds at once would not be.
- **320x200 is a gift.** The PSP screen is 480x272, so the framebuffer fits
  with room for letterboxing at 1x, and the existing integer-scaling path
  already handles the rest.

---

## How things get proven here

Restating because it has repeatedly caught real errors - see `CLAUDE.md` for
the full list.

- Prefer an oracle over an opinion: exact sizes, byte-identical streams,
  offsets landing precisely at EOF.
- A size check is not a correctness check.
- Render it, or listen to it.
- Read the context around a grep hit before believing it. This binary has
  produced at least four coincidences that looked like findings.
- Separate proven from guessed, in writing.


---

## Before publishing to GitHub

The repository has never contained game data and `.gitignore` is aggressive
about keeping it that way, so publishing is mostly a matter of paperwork:

- **Choose a licence.** There is none yet. Note that `third_party/nuked-opl3`
  is **LGPL 2.1**, which constrains the options for the whole distribution -
  decide deliberately rather than dropping in an MIT file out of habit.
- `README.md` already leads with "you need your own copy" and explains why.
  Keep that first; it is the thing that makes the project defensible.
- Re-read `.gitignore` before the first push, and check `git log --stat` for
  anything game-derived that slipped in early. `git ls-files` should show only
  source, docs, scripts and the vendored emulator.
- `assets-extracted/` is ignored wholesale, including every rendered PNG, WAV,
  MIDI and DRO produced during analysis. None of it should ever be committed.
- Consider whether the Ghidra project should be mentioned in the README as
  *deliberately* outside the repo, since it is derived from copyrighted data.
