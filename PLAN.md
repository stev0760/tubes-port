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

1. **The dispenser model.** `src/game.cpp` has atoms falling straight down to
   be caught by a tube. The tube-piece sprites (`TUBEH`, `TUBEV`, `TUBEVL`,
   `TUBEVR`) are 16x13 - the same cell size as atoms - so they are *tiles on a
   grid* and the dispenser is a routing network. Both halves of the current
   model are wrong.
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

The last real unknown, and everything else sits on it. In `1000:3a67`:

- how atoms enter the tube network and how routing is represented
- how the test tube stacks, and what capacity maps to which difficulty
- what the `TUBEVL`/`TUBEVR` variants mean (left/right routing?)

Anchor: the cell-to-pixel mapping is known, so tube-tile draws are
recognisable by their coordinates.

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
