# Roadmap

Cross-session tracking. `CLAUDE.md` is how to work in this repo,
`docs/reversing-notes.md` is what the formats are, `docs/worklog.md` is what
happened when. **This file is what is left to do.**

Update it when something lands or when a plan turns out to be wrong. A stale
plan is worse than none.

---

## The goal

**Faithfully translate the original Turbo Pascal to C++/SDL, so the game plays
virtually identically - but on source that is readable and extensible.**

Not a game inspired by Tubes, and not a reimplementation that behaves similarly.
The target is the same game: same rules, same speeds, same pixels, arrived at by
reading the original's code and writing the equivalent in C++. The Pascal source
is almost certainly lost, so this binary is the only surviving record of how
Tubes works, and the port is meant to become the readable version of it.

What "faithful" buys is a base worth extending. Once the behaviour matches, the
code is a normal C++/SDL codebase - portable, testable, modifiable - rather than
16-bit real-mode Pascal that only runs under emulation.

Two consequences that shape every decision here:

* **Rules come from the decompiled code, never from watching the game.** See
  the prime directive in `CLAUDE.md`. Observation gives samples; the binary
  gives the function.
* **"Close enough" is not the bar.** The pixel-diff harness exists so that
  "virtually identical" is a measured number rather than an impression.

---

## Where this stands

Percentages are judgement calls, so the breakdown matters more than the number.

### Reversing: ~85%

| Area | State |
|---|---|
| Every asset format | **done** - container, LZSS, `.CSP`, `.GFX`/`.PAL`, fonts, `.SFX`, `.SCR`, `.MUS` |
| Program map, every interface stage | **done** |
| Playfield geometry and cell-to-pixel mapping | **done**, measured |
| Ball table and the 19 type numbers | **done**, read out of the entry program's own initialiser |
| Fade encoding (`type + 19*frame`, one table) | **done** |
| Beaker structure (three `array[1..5,1..6]` planes) | **done**, decompiled |
| Atom router `1000:0f80` | **done** - states, fixed-point, both arc tables |
| Network topology (feed / lane / destination per column) | **done**, measured |
| Drops, scoring, tube capacity, save format, menus, waves | **done** |
| Render order and the dirty-rect model | **done** |

Still unread, and most of it is inside `1000:3a67`'s 9,382 bytes:

- the **match-and-clear routine** - scoring came from the Instructions, not code
- the **specials' behaviours**: AntiMatter's blast, Convertor, Blocker, Filler,
  Multiplier, EvilMultiplier, Crystal
- the **wave definition** data structure - objectives and modifiers
- **spawn/dispense** logic, and what else the difficulty routine sets
- corner-sprite selection, descent velocity
- `.SPR`, `.BIN`, `.ANM`

### The engine: ~40%

The core play loop is now largely faithful; almost everything *around* it is
absent. That is why the number went **down** from the 45% claimed earlier -
that figure was set before the surrounding scope was properly counted.

Working, and transliterated rather than invented:

- the atom router, network topology and fixed frame step
- the tube network rendering, per-atom tube overlay, beaker and test tube
  layering
- atom type numbering, drops model, scoring by chain orientation with the ramp
- Flashium's wildcard, inert specials, cascades and gravity
- music: all ten songs, correct at the register level

Absent entirely:

| Missing | Size |
|---|---|
| HUD - Chains, score, Drops, the small ball counters | small, high visibility |
| Sound effects | small |
| The three-plane beaker, and therefore **any clear animation** | medium, blocks the specials |
| Specials' behaviours | medium |
| Wave structure: briefings, objectives, modifiers | large |
| Menus, difficulty select, high scores, save/load | large |
| Blackboard stats and cutscenes | medium |
| Demo playback (`.SCR` replay through the same loop) | small, and it is the regression oracle |

Pixel accuracy against the original currently reads **4.6%** of structural
pixels differing, on a paused frame with the backdrop excluded.

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

1. ~~**The dispenser path.**~~ **DONE.** `game.cpp` now transliterates the
   router `1000:0f80`: fixed-point motion at 1/128 px, states 3/5/6/7, the
   `DS:0x18` column table, and the two hand-tuned arc offset tables
   (`{9,6,4,2,1}` rising, `{9,6,3,2,1}` horizontally). The network topology -
   which feed tube serves which column along which lane - is in `game.cpp`.
2. ~~**The test tube holds one atom.**~~ **DONE.** Capacity is a flat 5, stated
   by the in-game Instructions. The 5/3/2-by-difficulty guess is retired.
3. ~~**Scoring and pacing.**~~ **MOSTLY DONE.** Scoring is by chain
   orientation, 250/500/1000, from the Instructions, and the score ramps.
   `kSpawnIntervalFrames` is **still an invented placeholder** and is the one
   item here that remains genuinely wrong.
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
   | 18 | the **Mischief Crystal** - starts in the beaker as contamination and **teleports** between cells; removed with **AntiMatter**, never by matching. `CRFADE` is its *teleport* animation (forward out, reverse back), which is why its static sprite is `CRFADE1` |
   | 19 | `MYSTBALL`, still unidentified |

   A fade family is an **effect animation**, not a "can be cleared" marker: the
   seven colours are match-clears, `FFADE` the wildcard's, `GLDFADE` the Bonus,
   `AFADE` the **AntiMatter blast** applied to everything caught in it, and
   `CRFADE` the crystal's **teleport**. Types 11-17 and 19 need no effect of
   their own.
   Cell values are therefore **not** limited to 1..7 - the table runs to 19.
   Behaviours for 8-16 come from published descriptions whose ordering matches
   the measured type order exactly; 17 and 19 are unknown.

---

## Next

### 0. START HERE: transliterate `1000:3a67`, measured by the pixel diff

The project is in its **transliteration phase**. `CLAUDE.md` carries the prime
directive: every gameplay rule in `src/` must come from decompiled Pascal, not
from watching the game. Black-box recreation was tried for a session and was
lossy in ways invisible from the inside - a scoring rule fitted to two points
that was simply wrong, a wildcard rule wrong until a player described it, and
an "ambiguity in the original" that was really a bug in our own matcher.

**The measure of progress is now a number.** `~/Dev/tubes-tooling/` holds a
pixel-diff harness (see `docs/debug-rig.md`):

    python3 capture_frame.py frames 2      # original, PAUSED, + its state
    python3 diff_frame.py frames/frame00.state

It captures the original with the game's own **Pause** key - halting via GDB
blocks the screendump - puts the port into that exact state with
`tubes-port --render-state`, and diffs. Current reading: **4.6% of structural
pixels**. Drive that down.

Two calibrations are already in it and must not be removed: the backdrop is
excluded (random, animated), and greys are compared with a tolerance of 6
because DOSBox expands the DAC with `v<<2` and the port with `v*255/63`.
Without the tolerance the harness reports 34% and sends you hunting a palette
bug that does not exist.

**In priority order:**

1. **Sweep the test tube x offset** against the harness. Our wall sits at
   x=166 where the original's is at 172, but shifting by that 6 px made the
   diff *worse* (4.6% -> 8.5%) because the sprite has more than one wall.
   Sweep, take the minimum; do not deduce from one edge.
2. **The missing vertical pieces in the arcs** - a handful of red columns in
   the diff image.
3. **The corner sprites.** `game.cpp` draws no tube over an atom rounding a
   bend, because which of `TUBEVL`/`TUBEVR`/`TUBEHR` the original picks per
   corner is not decoded. A round ball on the bend is the current placeholder.
4. **Descent velocity.** State 7's speed is inferred, not found in code.
5. **Spawn interval** - `kSpawnIntervalFrames` is an outright placeholder.
6. **The beaker representation.** A cell holds `type + 19*fadeFrame` with a
   parallel animating plane and a third overlay plane (`MARKER`). The port
   models a single plane of types, so it cannot show a clear animation at all.
   This is the deepest remaining gap and it invalidates nothing above it.

**What is already transliterated and should not be re-derived:** the atom
router `1000:0f80` (fixed-point, states 3/5/6/7, the two arc offset tables),
the `DS:0x18` column table, the network topology (feed x, lane y, dest x per
column), the ball table and type numbering, the drops model, and scoring by
chain orientation.

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
ends at 245. **Measured:** that is the initial value of a *never-used slot*, not
a parked state and not a recycle target - over 382 frames of play, **no** record
ever transitioned into or out of it. The twelve records are a pool of slots,
reused by overwriting x/y directly; the large positional jumps in the per-frame
data are allocation, not motion. A lost atom's record simply keeps its final
position until the slot is reallocated.

**Also measured, and correcting the line above:** a missed atom is **lost**, not
deposited in the beaker. That is a "drop", and it is what the drop allowance
counts. The beaker fills *only* by catching atoms in the test tube and tipping
them in with Button A - nothing reaches it without passing through the tube.

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
| +0x0b | colour / sprite index - **confirmed live** at array base `0x241a4` (the earlier doubt was a 6-byte base error, not a layout error) |
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
| `parent - 0x16a` | one struct | **the player's test tube** - now **located live at `0x245d0`**, but *not* where this implies: it sits `0x432` bytes **above** the atom array, not 7 bytes below. The field layout is confirmed; the parent-relative relationship is wrong |

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

### The test tube's state machine - **confirmed live**

Struct at **`0x245d0`**; x `+0x00`, state `+0x04`, waypoint index `+0x1e`,
target x `+0x1f`, all bytes. Driving each input and reading `+0x04`:

| `+0x04` | meaning | observed |
|---|---|---|
| 0 | parked at a column, accepting input | idle |
| 1 | sliding left, `x -= 6` per frame | hold Left |
| 2 | sliding right, `x += 6` per frame | hold Right |
| 3 | tipping: 4-frame animation | press Button A |

x was seen stepping 104, 110, 116, ... 194 - the 6 px/frame rail speed - and the
targets are exactly 104, 122, 140, 158, 176, 194 with the index tracking 1..6.
The **live score** is a `u32` at `0x245e7`.

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
- `.SFX` through SDL audio, mixed alongside the OPL output. **All 24 are now
  attributed**: 11 match/clear sounds (one per fade family), 6 gameplay events
  (`DROP` = a miss, `HITATOM`, `HITGLASS`, `SLIDE`, `SWITCH`, `SELECT`), 4
  cutscene (`BUBBLE` - the beaker foaming in the intro - `CLAP`, `NOOOO`,
  `WHATTHE`) and 3 splash (`WOOSH`, `LIGHTN`, `ABSMAGIC`). The match sounds are
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
  atoms") and often a **modifier** - a disabled element that still spawns but
  cannot be cleared, atoms hidden until they leave a tube, beaker atoms morphing
  on a timer, a pre-filled beaker. None of these exist in the port.
- **drops are a persistent pool, not part of the wave definition** - now proven,
  with each mechanism measured on its own. The live counter is a u8 at
  **`0x245bc`**, confirmed against the HUD (memory read 8 while the display read
  `8 Drops`). A miss decrements it; a **Bonus atom increments it**; and
  **clearing a wave leaves it untouched** - it read 8 on both sides of the wave
  52/53 boundary, and wave 53's briefing then announced "8 drops allocated".
  That last point is the confirming test the notes had queued, delivered by play
  rather than by editing the save: the briefing echoes the *current pool*, so the
  fifty screenshots read 11 only because the sweep always loaded a save holding
  11 at `TUBES.SAV 0x207`. The Bonus atom is therefore an extra life - the only
  known way to replenish a resource that otherwise only decreases.
  **A new game seeds the counter from the difficulty** - Tubes 101/201/301 give
  9/6/3, confirmed by playing all three - which finally reconciles the binary's
  measured `9/6/3` with the 11 seen in saves: they were never competing claims,
  one is the seed and the other is a run that collected Bonuses. There is **no
  cap** (a wave 6 save started at 12), so the port should treat drops as a plain
  byte counter seeded once per game.
- special atoms; Endurance vs Wave mode selection sits under a Game Mode menu,
  and saved games are filtered by mode
- save/load. `TUBES.SAV` is 960 bytes and **structurally decoded**: two `0x1e0`
  banks, one per game mode, each holding five `0x50` slots plus a trailer.
  Per-slot fields, confirmed against the running game: player name at `+0x00`
  (Pascal ShortString), score u32 at `+0x1f`, wave at `+0x26`, **drops remaining
  at `+0x27`**, atom target at `+0x2d`. Bank 0 is Endurance, bank 1 is Wave mode,
  which is why Endurance listed five `(UNAVAILABLE)` entries for a save Wave Mode
  showed at once. The stride also explains the two previously unaccounted bytes:
  `0x1bb` and `0x39b` are the same `+0x2b` field of each bank's trailer.
  There is no checksum, so editing a slot's wave byte warps to any wave - but
  note that is `bank + slot*0x50 + 0x26`, and the familiar `0x206` is merely
  bank 1 slot 0. See `docs/reversing-notes.md`.

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
