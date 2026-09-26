# tubes-port

A reimplementation of **Tubes**, a 1994 DOS puzzle game by Software Creations
and Absolute Magic, in C++17 and SDL2. It reads the original's data files and
plays the same game.

Atoms travel a network of glass tubes. Slide the test tube along the bottom to
catch them, tip them into the beaker, and clear three or more of a colour in a
row. Miss too many and the run ends.

## Playing it

The port contains none of the original game. It plays the data files from your
own copy of Tubes, so you need one first.

### Getting the game

**Any copy works**, registered or shareware. Tubes has been out of print for
decades. The shareware release was made to be copied and passed around freely,
and the Internet Archive hosts it:
<https://archive.org/details/msdos_TUBES_shareware>. Download `TUBES.zip` and
unzip it anywhere. The folder you get is your Tubes folder: it is the one with
`TUBES.RES` in it.

### Windows

1. Download the `windows-x86_64.zip` from the
   [Releases page](https://github.com/stev0760/tubes-port/releases/latest)
   and unzip it.
2. Copy `tubes-port.exe` into your Tubes folder, next to `TUBES.RES`.
3. Double-click `tubes-port.exe`.

Or leave the .exe where it is and drag your Tubes folder onto it. Either way it
remembers where the game is, so after the first start a shortcut to it on the
desktop works too.

If Windows says **"Windows protected your PC"**, click **More info**, then
**Run anyway**. It says that about any program downloaded from the internet
that has not been through a paid code-signing process, and this one has not.

A console window opens beside the game. Messages go there, and closing it
closes the game. If the game cannot be found, a window says where it looked and
what to do.

### Linux

Download the `linux-x86_64.tar.gz` from the same page. It needs SDL2
(`sudo apt install libsdl2-2.0-0` on Debian and Ubuntu). Extract it, then run it
from your Tubes folder, or tell it where that is:

    ./tubes-port /path/to/your/tubes

### What it reads and writes

`TUBES.RES` carries the sprites, sounds, songs, fonts and wave data.
`DRIVERS.RES` beside it adds FM music, which needs the `FMMUSIC.DRV` inside it.
Those two files are all the port opens. It never touches the game's own
executable, so the LZEXE packing on it makes no difference.

Each run asks which edition to play, the 25-wave shareware release or the
75-wave registered one, starting on your last answer. `TUBES.RES` is identical
in both, so one copy of the data plays either and switching costs a keypress.
`--shareware` and `--registered` answer from the command line.

Saving writes into that same folder in the formats the 1994 game uses, so both
programs read each other's files: `TUBES.SAV` and `TUBES.HSC` for the registered
edition, `TUBESSW.SAV` and `TUBESSW.HSC` for the shareware one, which keeps the
two editions' progress apart.

**Controls.** Left and Right slide the test tube, Down speeds up the atom above
it, Ctrl or Space tips the tube into the beaker. F1 help, F2 save, F5 pause, Esc
quit. Keys are rebindable in Game Options, and a gamepad works if one is
plugged in.

| Flag | |
|---|---|
| `--gamedir DIR` | where your `TUBES.RES` lives; a bare `DIR` works too (default: look for it) |
| `--scale N` | integer scale factor (default: largest that fits) |
| `--no-splash` / `--no-music` | skip the boot splashes / start silent |
| `--fade-steps N` | shorten the half-second screen fade; `0` cuts it |
| `--version` | print the version and exit |

`--help` lists the rest, most of which park the engine on one screen so it can
be captured and compared against the original.

## Building

SDL2, CMake 3.16 or newer, and a C++17 compiler.

    # Debian/Ubuntu: sudo apt install build-essential cmake libsdl2-dev
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ./build/tubes-tests          # 1086 checks; runs without game data

On Windows, with Visual Studio and [vcpkg](https://vcpkg.io), from a Developer
PowerShell:

    vcpkg install sdl2:x64-windows-static
    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
          -DVCPKG_TARGET_TRIPLET=x64-windows-static -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
    cmake --build build --config Release
    .\build\Release\tubes-tests.exe

That links SDL2 and the C runtime statically, so `tubes-port.exe` runs on its
own. It is the configuration CI builds and the release ships.

## How it works

Every gameplay rule in `src/` was decompiled out of the original executable and
transliterated, down to the order of statements in a frame. The atom router, the
beaker's three planes, the 75-wave table and its 25 objective routines, the
scoring ramp, the tipping animation: each one is a reading of 16-bit Pascal
compiled in 1994, and `docs/reversing-notes.md` records the address it came from
and how it was proven.

Three checks run against the original:

- **The demo.** `DEMO.SCR` is the original's own recording, one input byte per
  frame, and it is deterministic. Replaying it through the ported logic ends on
  byte 2,367 with a score of 13,000 — the same byte and the same score the
  original ends on.
- **The screens.** Frames are compared pixel for pixel against captures of the
  game running under DOSBox-X. The cutscene, the first splash and both high
  score pages match on all 64,000 pixels. The play field runs 0.02% to 0.22%.
- **The music.** The sequencer's OPL register writes are diffed against an
  independent Python decoder. All ten songs match byte for byte.

The engine draws into a 320x200 indexed framebuffer and scales it by whole
numbers with nearest-neighbour filtering, which keeps the pixel grid the art was
drawn on.

## Status

**0.11.0**, playable start to finish in Endurance and Wave mode, on both
editions. Every screen of the original is ported and every format in the archive
decoded. `PLAN.md` lists what is left, and `src/version.h` explains what the
version numbers promise.

## Working on it

`tools/` holds a decoder per format, the generators that turn the game's own
text into `src/*.cpp` tables, and `screen_sweep.sh`, which captures all 74
screens for comparison. `docs/worklog.md` is the running log, and `CLAUDE.md`
describes how the repository is worked in.

The shipped `TUBES.EXE` is LZEXE-compressed, so unpack it before loading it into
a disassembler:

    tools/unpack.sh /path/to/TUBES.EXE     # -> assets-extracted/TUBES_UNP.EXE

## Licence

This project's code is **MIT**, in [`LICENSE`](LICENSE). The vendored
`third_party/nuked-opl3` is LGPL-2.1-or-later and `third_party/moderndos-8x16`
is MIT or CC0-1.0, both keeping their own terms.

Tubes itself remains under its own copyright. This repository holds engine code,
never game data, and you need your own copy of the game to play it. The one
thing here that came out of the binary is the game's own screen text — the
Instructions, the Credits, the cutscene — generated into `src/`, which the
shareware release's `TUBES.DOC` also carries in plain text.
