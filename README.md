# tubes-port

An SDL / C++ reimplementation of **Tubes**, a 1994 DOS puzzle game by Software
Creations and Absolute Magic — translated out of the original binary rather than
rewritten from memory of how it played.

> Collect atoms in test tube and match at least 3 like colours in beaker any
> way.
>
> — the game's own Simple Instructions

Atoms travel a network of glass tubes. You slide a test tube along the bottom to
catch them, then tip them into the beaker, where three or more of a colour in a
row clear. Miss too many and the run ends.

## Running it with your own copy of the game

This repository is **engine code only**. It ships no game data and never will:
Tubes is still copyrighted, and its artwork, sound, music and wave data are not
ours to redistribute. The engine does what ScummVM and the Doom source ports do
— it reads the assets at runtime from a copy you supply.

    ./build/tubes-port --gamedir /path/to/your/tubes

Point `--gamedir` at the folder holding these (it defaults to `.`):

| File | |
|---|---|
| `TUBES.RES` | **required** — every sprite, sound, song, font and the wave data |
| `DRIVERS.RES` | optional — holds `FMMUSIC.DRV`, whose tables the FM sequencer needs. Without it, sound effects but no music |

**You do not need `TUBES.EXE`, and its compression does not matter.** The
original executable is LZEXE-packed, but this port *is* the executable. It never
reads the original, so there is nothing to unpack and no DOSBox in the loop.
Unzip your copy anywhere and point at the folder.

**Both editions work.** The 25-wave shareware and 75-wave registered releases
differ in wave count, menus and end screens. `TUBES.RES` is byte-identical
between them, so the port cannot detect which you have and asks on startup.
`--shareware` and `--registered` skip the question.

Playing normally **writes `TUBES.HSC` and `TUBES.SAV` into that folder**, in the
original's own byte-exact formats, so your saves and scores stay readable by the
1994 game. Copy them aside first if the folder holds saves you care about.

**Controls.** Left/Right slide the test tube, Down speeds the atom above it,
Ctrl or Space tips the tube into the beaker, Esc quits. F1 help, F2 save, F5
pause. Keys are rebindable in Game Options. A gamepad is detected automatically.

**Flags.** `--help` lists the rest, most of which park the engine on one screen
for capture.

| Flag | |
|---|---|
| `--gamedir DIR` | where your `TUBES.RES` lives (default `.`) |
| `--scale N` | integer scale factor (default: largest that fits) |
| `--no-splash` / `--no-music` | skip the boot splashes / start silent |
| `--fade-steps N` | shorten the half-second screen fade; `0` cuts instead |
| `--version` | print the version and exit |

## Building

Needs **SDL2**, **CMake 3.16+** and a **C++17** compiler. Nothing else — the
OPL3 emulator is vendored.

    # Debian/Ubuntu: sudo apt install build-essential cmake libsdl2-dev
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ./build/tubes-tests          # 1061 checks; needs no game data

`tubes-tests` links no SDL and reads nothing from disk, which is why CI runs it
and why the rendering code stays free of SDL.

## How it works

**Every gameplay rule in `src/` is read out of the original's code**, not
inferred from watching it run. That shortcut was tried early and abandoned. A
scoring rule fitted to two observed awards was simply wrong while the real rule
sat in the game's own Instructions, and the wildcard atom was wrong in two
separate ways until a player described what the game actually does. Observation
yields samples. The binary yields the function. A rule that never happens
to appear on screen cannot be sampled at all.

The order of authority is fixed. **The decompiled code** settles a rule, the
**game's own text** corroborates it, and **live measurement** locates and
validates but never derives. Rules that cannot yet be decompiled are marked in
the source as placeholders rather than shipped as plausible inventions.

Verification is by oracle, not opinion. The original's `DEMO.SCR` recording is
deterministic, so the ported logic must reproduce it frame for frame — it ends
on the same input byte the original does, 2,367, with the same final score.
Screens are diffed pixel-for-pixel against captures of the original under
DOSBox-X. Music is checked by comparing OPL register streams against an
independent Python decoder, never by listening.

| Check | Result |
|---|---|
| Opening cutscene, all five pages | **0** pixels of 64,000, nothing masked |
| Splash screen, high score viewer | **0** pixels of 64,000 |
| Play field, eight paused captures | 0.02%–0.22% of structural pixels |
| All ten songs | OPL register stream matches byte for byte |
| `TUBES.SAV`, `TUBES.HSC` | re-encode byte-exact |

The engine draws into a 320x200 indexed framebuffer exactly as the original did,
then scales by an integer factor with nearest-neighbour filtering. Non-integer
scaling would wreck the pixel grid of art authored for Mode X, so it is not
offered.

**One deliberate departure**, and it sits below the game logic rather than in
it: the original asks an input driver for one byte a frame (Up, Down, Left,
Right, A, B), and that byte *is* game logic and does not change. Underneath,
`KEYBOARD.DRV` and friends exist only because 1994 had no abstraction over an XT
keyboard and a gameport. SDL is that abstraction, so porting a driver chooser
would be transliterating the *absence* of SDL. `src/input.h` has the reasoning.

## Status

**0.9.0.** Playable start to finish in both Endurance and Wave mode. Every
format in the archive is decoded and every screen of the original is ported,
from the two boot splashes to the wave-75 ending, with attract mode cycling on
its own.

1.0.0 is a claim rather than a milestone: every rule read rather than fitted,
and a full Tubes 101 playthrough completed clean. `src/version.h` says what the
three numbers promise here. `PLAN.md` is what is left. Known gaps are
presentation, not rules: the briefing omits the professor and draws its
projector slide as a measured rectangle, the stats background is a stand-in, and
three pixels at (59, 10..12) differ for a reason not yet found.

## Reversing the binary yourself

Here the compression does matter. `TUBES.EXE` is LZEXE-packed, so a disassembler
shows a decompressor stub and 43KB of entropy until it is unpacked:

    tools/unpack.sh /path/to/TUBES.EXE     # -> assets-extracted/TUBES_UNP.EXE

The result loads into Ghidra as 16-bit real-mode x86. The script needs a C
compiler and `git`, and builds
[mywave82/unlzexe](https://github.com/mywave82/unlzexe) on first run.

The Ghidra project lives **outside this repository** on purpose, since it is
derived from copyrighted data, as is everything the debugging rig captures.
`docs/reversing-notes.md` is the format and findings reference and marks which
constants are measured and which inferred. `docs/worklog.md` is the chronological
record and `docs/debug-rig.md` covers the DOSBox-X rig.

`src/` is the engine, `tools/` the Python decoders, `ghidra_scripts/` the
headless analysis scripts. Only `main.cpp`, `boot.cpp`, `present.cpp` and
`opl.cpp` may include SDL. Everything else is platform-agnostic and must stay
that way, because that split is what makes a port to another platform tractable.

## Licence

This project's own code is **MIT** — see [`LICENSE`](LICENSE). Vendored code
keeps its terms: `third_party/nuked-opl3` is LGPL-2.1-or-later (weak copyleft,
and it does not relicense this project), `third_party/moderndos-8x16` dual MIT
or CC0-1.0.

## Legal

For interoperability and preservation. This reimplements the engine. It does not
copy or redistribute the game, and no original file or extracted asset has ever
been committed here. You need to own Tubes to play it.

The game's own text (Instructions, Credits, the cutscene's story) *is*
generated into `src/` from the binary. A knowing exception, taken because Tubes
shipped as shareware and the shareware package's own `TUBES.DOC` carries the
same story in plain text, meant to travel with the game. The **assets** rule has
no exception: no `.RES`, no `.EXE`, no extracted sprites, no rendered output.
