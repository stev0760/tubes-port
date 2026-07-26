# tubes-port

An SDL / C++ reimplementation of **Tubes**, a 1993 DOS puzzle game by
Software Creations and Absolute Magic.

## You need your own copy of the game

This repository contains **engine code only**. It ships no game data, and it
never will.

Tubes is still copyrighted. The artwork, sound, music, and level data live in
the original `TUBES.RES`, and those files are not ours to redistribute. The
engine therefore does what OpenTTD, ScummVM, and the Doom source ports do: it
reads assets at runtime from a copy of the original game that you supply.

    tubes-port --gamedir /path/to/your/TUBES

No original files or assets extracted from them are ever committed here — see
`.gitignore`, which is deliberately aggressive about this.

## Status

Renders. Every asset format bar music is decoded, and the engine draws a
composited 320x200 scene from the original data.

- [x] Decompress `TUBES.EXE` (LZEXE v0.91) into an analyzable binary
- [x] Map the `TUBES.RES` container format and its LZSS compression
- [x] Decode sprites, images, palettes, fonts, sound effects, demo recording
- [x] Engine: resource loading and Mode X-faithful rendering
- [x] Playfield logic, matching and input
- [ ] `.MUS` FM/Adlib music
- [ ] Recover real gameplay constants (grid size, scoring, wave curves)
- [ ] Beaker and test tube presentation, sound effects, wave/endurance modes

## Building and running

Needs SDL2, CMake and a C++17 compiler.

    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ./build/tubes-port --gamedir /path/to/your/TUBES

Options:

| Flag | Meaning |
|---|---|
| `--gamedir DIR` | directory holding your `TUBES.RES` (default `.`) |
| `--scale N` | integer scale factor (default: largest that fits) |
| `--screenshot FILE` | render one frame to a BMP and exit |

`--screenshot` works headless under `SDL_VIDEODRIVER=dummy`, which is how the
renderer gets verified without a display.

### Rendering approach

The engine draws into a 320x200 indexed framebuffer, exactly as the original
did, then scales by an integer factor with nearest-neighbour filtering and
letterboxes the remainder. Non-integer scaling would destroy the pixel grid
of art authored for a 256-colour Mode X screen, so it is deliberately not
offered.

## Getting started

`TUBES.EXE` is LZEXE-compressed, so a disassembler shows only a decompressor
stub and 43KB of entropy until it is unpacked:

    tools/unpack.sh /path/to/TUBES.EXE

This produces `assets-extracted/TUBES_UNP.EXE` (99,728 bytes, 2,150
relocations, entry `0000:AABA`) which can be loaded into Ghidra as 16-bit
real-mode x86.

Requires a C compiler and `git` — the script builds
[mywave82/unlzexe](https://github.com/mywave82/unlzexe) on first run.

## Approach

Full decompilation of segmented 16-bit real-mode x86 into working C++ is a
long slog, and most of it would be spent re-deriving a Columns-style puzzle
game whose rules are already documented in the manual. So the plan is
targeted rather than exhaustive:

1. Reverse the `.RES` container properly — the assets are the part that
   genuinely cannot be recreated.
2. Reverse only the *numbers*: scoring tables, wave progression, fall-speed
   curves, chain multipliers, spawn distribution.
3. Write the gameplay fresh against those constants, checking feel
   side-by-side against DOSBox.

The original sound drivers in `DRIVERS.RES` are discarded outright; SDL
replaces them.

See `docs/reversing-notes.md` for findings so far.

## Legal

This project is for interoperability and preservation. It reimplements the
game engine; it does not copy or redistribute the original game. You must own
Tubes to play it.
