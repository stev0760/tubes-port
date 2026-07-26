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

1. **The dispenser path.** `src/game.cpp` still has atoms falling straight
   down. Really they enter bottom-right, travel up, arc over the top and come
   down along the tube artwork. The stacking tube and its controls are now
   implemented; the *path* is not, and is approximated by a vertical fall.
   Do not model it as per-cell tile routing - the frame update contains no
   13px-pitch arithmetic outside the settled-grid draw.
2. **The test tube holds one atom.** `TESTUBE1/2/3` are 22x65, 20x42 and 20x27
   - about 5, 3 and 2 cells at the 13px row pitch. Three capacities, matching
   the 9/6/3 drop limits. The tube stacks several atoms.
3. **Scoring and pacing.** `kScorePerAtom`, `kChainBonus`, `kSpawnInterval`,
   `fallSpeed` are invented. Real values are in `1000:3a67`.
4. **Atom colour count.** The engine uses 8 flat colours. The original has
   `ANTIBALL`, `GOLDBALL` and `XENBALL` loaded by `entry` and unused here, and
   grid cells cycle 1..7 with 0 empty - so 8 is probably not the whole story.

---

## Next

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
| +0x1e | waypoint index, 1..6 |
| +0x1f | target x |

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

### Speed: still not found, and now known not to be here

Neither candidate survives. Travel is a flat 6 px/frame with the divider
threshold and the 4-phase limit both hardcoded, and this struct is the tube
rather than an atom - so the bonus atom's speed cannot live here at all.

Atom speed must be in the 12-record array at `parent - 0x163`, which has not
been examined for it yet. That is where to look next.

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

- 66 fade sprites: 11 families x 6 frames, the atom-clear animation
- `.SFX` through SDL audio, mixed alongside the OPL output
- HUD: the reference screenshot shows chains, score and drops across the top
- Fonts are decoded but never drawn

### 3. Game rules

- scoring tables and chain multipliers
- the difficulty progression - seeds `3, 30, 2, 0, 3, 8` plus globals 50 and
  25, stepping every 15 and every 20 levels, with level bands at
  30 / 60 / 75 / 90 / 95 / 101. Variables not yet named; trace them from
  `9e53` into `3a67` through the Pascal static link.
- special atoms, wave and endurance modes
- save/load (`TUBES.SAV` is 960 bytes)

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
