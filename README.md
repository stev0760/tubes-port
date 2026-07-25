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

Early. Currently at the reverse-engineering stage; no engine code yet.

- [x] Decompress `TUBES.EXE` (LZEXE v0.91) into an analyzable binary
- [ ] Map the `TUBES.RES` container format
- [ ] Extract sprites, sound, and level data
- [ ] Recover gameplay constants (scoring, wave curves, spawn RNG)
- [ ] Engine: rendering, input, audio
- [ ] Gameplay

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
