---
name: tubes-rig
description: Drive the Tubes debugging rig - run the original under DOSBox-X, capture paused frames with their state, pixel-diff them against the port, and decompile functions with Ghidra headless. Use whenever measuring the original's behaviour, comparing the port's rendering, or pulling code out of TUBES_UNP.EXE.
---

# The Tubes rig

Everything lives **outside the repo** at `~/Dev/tubes-tooling/` because it holds
copyrighted game data. Never copy game data or decompiler output into
`tubes-port/`.

## Run an isolated instance (does not disturb a game in progress)

`DOSBoxInstance.start()` calls `pkill -9 -f dosbox-x` and kills every instance.
Launch with `subprocess.Popen` instead, against a config with its own ports:

    tubes-sweep.conf   gdbserver 2160, qmpserver 4445, drive sweepdrive/
    tubes.conf         gdbserver 2159, qmpserver 4444, drive gamedrive/

Both stubs are **single-client**. One script at a time per port.

## Navigate to gameplay

    esc              skip splashes
    ret              title -> main menu
    ret              Start Game -> Game Mode
    ret              Endurance -> Difficulty
    ret              Tubes 101 -> briefing
    ret              briefing -> play

Main menu order is Start / Continue / Options / High Scores / Instructions /
View Demo / Credits / Exit, and it **wraps**. `View Demo` is 5 Downs.

**ESC out of a submenu lands on "Exit Tubes?" with YES highlighted** - one RET
from ending the session. Back out with Down then Enter.

## Freeze a frame: the game's PAUSE key, never GDB halt

Halting via GDB makes QMP `screendump` **time out** - the emulator's main loop
is blocked. Pause freezes the game loop while the emulator keeps running, so
the framebuffer holds still *and* the screendump is answered.

    q.key_press("pause")

Attract mode cannot be paused: any keypress exits it. Start a real game.

## Capture and diff

    python3 capture_frame.py frames 2      # PNG + .state per frame, paused
    python3 diff_frame.py frames/frame00.state

Reports the percentage of differing **structural** pixels and writes a diff
image: red = original has it and we do not, green = we draw it and it does not,
yellow = both, different shade.

Two calibrations are built in and must not be removed:

* the **backdrop is excluded** - random `GAMEBG1..10` with `STAR1..4` animated
  over it, so it never matches and swamps everything;
* **greys compare with a tolerance of 6** - DOSBox expands the 6-bit DAC with
  `v<<2`, the port with `v*255/63`, so every grey lands one unit apart. Without
  it the harness reads 34% instead of 4.6%.

Do not fix an offset from a single edge in the diff. Shifting the test tube by
its measured 6 px made the diff worse; sweep and take the minimum.

## Decompile

Ghidra 12.2 **will not run `.py`** - write Java `GhidraScript`. Absolute paths
only.

    /opt/ghidra/support/analyzeHeadless \
      /home/steve/Games/GAMES/tubes/ghidra-project tubes \
      -process TUBES_UNP.EXE -noanalysis \
      -scriptPath /home/steve/Games/GAMES/tubes/tubes-port/ghidra_scripts \
      -postScript DecompileFuncs.java 1000:3a67 1000:9e53

Output is prefixed `INFO  DecompileFuncs.java> `; strip it before parsing.
`group_layouts.py` groups draw calls by brace depth - line-range grouping is
invalid and has already produced a wrong conclusion.

## Addresses (this conf, L = 0x0824)

    DS = 0x1fa9   DGROUP linear 0x1fa90      SS = 0x2294
    atom array  0x241a4   12 x 28
    beaker planes 0x242d8 / 0x242f6 / 0x24314   (overlay / animating / cells)
    drops       0x245bc   tube struct 0x245d0   score 0x245e7

**Gameplay state is on the STACK, not DGROUP** - the grid is `[BP-0x1e]` in
`1000:3a67`'s frame. Searching DGROUP offsets for it finds nothing.

## Footguns

* `pgrep -f`/`pkill -f` match your own command line. Kill by PID.
* A negative result is only as good as the filter. This project has logged five
  wrong conclusions from tools rather than the binary - and one from a search
  that came back *full* (two "frame counters" that were artefacts of testing
  ~28,000 offsets for monotonicity).
* Anything learned from play goes into `docs/reversing-notes.md` **immediately**,
  or it is lost at the next context compaction. That has already happened once.
