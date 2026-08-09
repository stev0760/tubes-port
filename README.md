# tubes-port

An SDL / C++ reimplementation of **Tubes**, a 1994 DOS puzzle game by Software
Creations and Absolute Magic.

> Collect atoms in test tube and match at least 3 like colours in beaker any
> way.
>
> — the game's own Simple Instructions

Atoms fall through a network of glass tubes. You slide a test tube along the
bottom of the network to catch them, then tip them into the beaker below, where
three or more of a colour in a row clear. Miss too many and the run ends.

## You need your own copy of the game

This repository contains **engine code only**. It ships no game data, and it
never will.

Tubes is still copyrighted. The artwork, sound, music and wave data live in the
original `TUBES.RES`, and those files are not ours to redistribute. The engine
therefore does what ScummVM, OpenTTD and the Doom source ports do: it reads
assets at runtime from a copy of the original that you supply.

    tubes-port --gamedir /path/to/your/TUBES

Both releases work. The 25-wave shareware disc and the 75-wave registered
edition differ in their wave count, their menus and their end screens, and the
port asks which one you have on first run.

## Status

Playable start to finish, in both Endurance and Wave mode.

Every format in the archive is decoded and every screen of the original is
ported: both splashes, the opening cutscene, the title and its menu, the wave
briefing, the game, the stats blackboard, high score entry and viewing,
Instructions, Credits, save and load, the demo, the wave-75 ending, and the
shareware ordering and sign-off screens. Attract mode cycles on its own.

What that is measured against, rather than asserted:

| Check | Result |
|---|---|
| Unit checks | 1021, 0 failures |
| Opening cutscene vs. the original | **0** pixels of 64,000, all five pages, nothing masked |
| Software Creations splash | **0** pixels of 64,000 |
| High score viewer, both pages | **0** pixels of 64,000, unmasked |
| Title and menu | menu text 0.00%, whole screen 0.25% |
| Play field, eight paused captures | 0.02% to 0.22% of structural pixels |
| All ten songs | OPL register stream matches the reference byte for byte |
| `TUBES.SAV` and `TUBES.HSC` | re-encode byte-exact |

The play field's floor is three pixels at (59, 10..12), where the original
leaves a foreground pixel erased for a reason not yet found.

Known gaps, all presentation rather than rules: the wave briefing draws the
projector slide as a measured rectangle rather than from its own routine and
omits the professor, and the stats blackboard's background is a stand-in
because nothing decompiled so far writes the image it blits. `PLAN.md` tracks
both.

## Building and running

Needs SDL2, CMake and a C++17 compiler.

    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ./build/tubes-tests
    ./build/tubes-port --gamedir /path/to/your/TUBES

Left and Right slide the test tube, Down speeds up the atom above it, Ctrl or
Space tips the tube into the beaker, Esc quits. Keys are rebindable in Game
Options.

The flags a player might want:

| Flag | Meaning |
|---|---|
| `--gamedir DIR` | directory holding your `TUBES.RES` (default `.`) |
| `--scale N` | integer scale factor (default: largest that fits) |
| `--no-splash` | skip straight to the title screen |
| `--no-music` | start silent |
| `--fade-steps N` | shorten the half-second screen fade; `0` cuts instead |
| `--shareware`, `--registered` | override the remembered edition |

`--help` lists the rest. Most of them exist to park the engine on one screen so
it can be captured and diffed, which is how the numbers in the table above are
produced: `--screenshot FILE` renders a single frame to a BMP and exits, and
works headless under `SDL_VIDEODRIVER=dummy`.

The engine draws into a 320x200 indexed framebuffer, exactly as the original
did, then scales by an integer factor with nearest-neighbour filtering and
letterboxes the remainder. Non-integer scaling would destroy the pixel grid of
art authored for a 256-colour Mode X screen, so it is deliberately not offered.

## How this was built

Full decompilation of segmented 16-bit real-mode x86 is a slog, and the
tempting shortcut is to reverse only the *numbers* — scoring tables, wave
curves, spawn rates — and then write the gameplay fresh against them, checking
the feel side by side against DOSBox.

**That shortcut was tried, and it is what this project now exists to avoid.**
It is lossy in ways that are invisible from the inside. A scoring rule fitted
to two observed awards was simply wrong, while the real rule sat in the game's
own Instructions. The wildcard atom was wrong in two separate ways until a
player described what the game actually does. An "ambiguity in the original"
got written up that was really a bug in our own matcher. Each was caught by a
human noticing, not by the method — and a rule that never happens to appear on
screen cannot be sampled at all, so you never learn it is missing.

So every gameplay rule in `src/` is derived from the original's code, and the
order of authority is fixed:

1. **the decompiled code** — the only thing that settles a rule;
2. **the game's own text** — Instructions, briefings, Credits. Good
   corroboration, and three times now it has held an answer that was being
   derived the hard way. The Credits state outright that Tubes "was written in
   Borland Pascal v7, and uses a planar 320x200x256";
3. **live measurement** — for *locating* and *validating*, never for deriving.

Measurement keeps a large role, just not that one. Measured addresses say where
to look in the disassembly, and the original's own `DEMO.SCR` recording is the
oracle: it is deterministic, so ported logic has to reproduce a captured state
sequence frame for frame. The same standard applies to music, where the
sequencer is checked by diffing OPL register streams against an independent
Python decoder rather than by listening.

Rules that cannot yet be decompiled are marked in the source as placeholders,
rather than shipped as plausible inventions that later read as settled.

There is exactly one deliberate departure, and it is below the game logic
rather than in it. The original asks an input driver for one byte per frame —
Up, Down, Left, Right, A, B — and *that byte is game logic and does not
change*; `DEMO.SCR` stores one per frame, which is why a recorded demo replays
through the same code path as live play. Everything under it is DOS plumbing.
`KEYBOARD.DRV`, `JOYSTK1.DRV` and `MOUSE.DRV` exist because 1994 had no
abstraction over an XT keyboard, a gameport and a serial mouse. SDL is that
abstraction, so porting a driver chooser would be transliterating the *absence*
of SDL. `src/input.h` carries the full reasoning.

## Getting started on the binary

`TUBES.EXE` is LZEXE-compressed, so a disassembler shows a decompressor stub
and 43KB of entropy until it is unpacked:

    tools/unpack.sh /path/to/TUBES.EXE

This produces `assets-extracted/TUBES_UNP.EXE` (99,728 bytes, 2,150
relocations, entry `0000:AABA`), which loads into Ghidra as 16-bit real-mode
x86. It needs a C compiler and `git` — the script builds
[mywave82/unlzexe](https://github.com/mywave82/unlzexe) on first run.

The Ghidra project itself is kept **outside this repository**, on purpose: it
is derived from copyrighted data, and so is everything the debugging rig
captures.

`docs/reversing-notes.md` is the format and findings reference, and it marks
which constants are measured and which are inferred. `docs/worklog.md` is the
chronological record, `docs/debug-rig.md` covers the live DOSBox-X setup, and
`PLAN.md` is what is left to do.

## Layout

| Path | Contents |
|---|---|
| `src/` | the engine (C++17, SDL2) |
| `src/main.cpp`, `boot.cpp`, `present.cpp`, `opl.cpp` | the SDL edge — the only files that may include it |
| `src/instructions.cpp`, `credits.cpp`, `cutscene.cpp` | generated from the disassembly, not transcribed |
| `tools/` | Python decoders, one per format |
| `ghidra_scripts/` | Java `GhidraScript` files for headless analysis |
| `third_party/` | vendored dependencies, unmodified |
| `docs/` | reversing notes, worklog, debug rig |

Everything outside those four SDL files is platform-agnostic and must stay that
way. Ports to other platforms are an eventual goal, and that split is what
makes one tractable.

## Licence

This project's own code is **MIT** — see [`LICENSE`](LICENSE).

Vendored code keeps its own terms. `third_party/nuked-opl3` is
LGPL-2.1-or-later, which is weak copyleft and does not relicense this project.
`third_party/moderndos-8x16` is dual MIT or CC0-1.0.

## Legal

This project is for interoperability and preservation. It reimplements the game
engine. It does not copy or redistribute the original game, and no original
file or asset extracted from one has ever been committed here. You need to own
Tubes to play it.

The game's own text (the Instructions, the Credits and the cutscene's story)
*is* generated into `src/` from the binary. That is a knowing exception, taken
because Tubes shipped as shareware and the shareware package's own `TUBES.DOC`
carries the same story in plain text, meant to travel with the game. The
**assets** rule has no exception: no `.RES`, no `.EXE`, no extracted sprites,
no rendered output.
