# CLAUDE.md

Working notes for this repository. Read `docs/reversing-notes.md` for format
details and `docs/worklog.md` for what happened when.

## What this is

An SDL/C++ reimplementation of **Tubes**, a 1994 DOS puzzle game by Software
Creations and Absolute Magic. The original is Borland Pascal 7.0 targeting
Mode X (unchained VGA 320x200x256).

Two goals, in tension occasionally — the first one wins:

1. **Learn how decompilation works.** The original binary is the point, not
   an obstacle. Do not "just rewrite it" to save time; that was proposed
   early on and rejected for good reason.
2. Produce a playable port.

## Hard rule: no game data in this repository

The game is still copyrighted. Assets are read at runtime from the user's own
copy; the engine takes `--gamedir`. `.gitignore` is deliberately aggressive
about `*.RES`, `*.EXE`, `assets-extracted/`, and rendered output.

**Always check `git diff --cached --name-only` before committing.** It has
already caught one near-miss (`__pycache__`). Never `git add -A` and trust it
blindly.

## Layout

| Path | Contents |
|---|---|
| `src/` | the engine (C++17, SDL2) |
| `tools/` | Python decoders, one per format, plus `unpack.sh` |
| `ghidra_scripts/` | Java `GhidraScript` files for headless analysis |
| `docs/reversing-notes.md` | every format, with what is proven vs guessed |
| `docs/worklog.md` | chronological record of the work |

The original game files live in the parent directory, `..`. The Ghidra
project is at `../ghidra-project` — outside this repo on purpose, since it is
derived from copyrighted data.

## Build, test, run

    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ./build/tubes-tests                      # 19 checks on the matching rules
    ./build/tubes-port --gamedir ..

`--screenshot FILE` renders one frame to a BMP and exits; it works headless
under `SDL_VIDEODRIVER=dummy`. `--auto N` runs N frames of a scripted player
first, so a headless capture shows a populated beaker. Together these are how
rendering gets verified without a display.

## Toolchain gotchas

- **Ghidra 12.2 will not run `.py` scripts.** Jython ships as an uninstalled
  extension and PyGhidra wants an interactive venv install. Write Java
  `GhidraScript` files; they compile on the fly with no setup.
- Ghidra needs **absolute paths** for both `-scriptPath` and the project
  directory. A leading `./` is rejected outright.
- Set `__stdcall16far` on game functions before decompiling. Pascal is
  callee-cleans; Ghidra's inferred cdecl signatures come out wrong.
- Ghidra creates no xrefs for DS-relative globals in 16-bit segmented code.
  Use `ghidra_scripts/FindScalarRefs.java` to chase them.
- Turbo Pascal 7.0 runs headless under DOSBox; `TPC.EXE` is the scriptable
  compiler, `TURBO.EXE` is the IDE and is not.

## Verification standards

This project has been unusually rigorous about proof, and it has repeatedly
paid off. Keep it up.

- **A size check is not a correctness check.** Every `.CSP` decoded to the
  right pixel count while having completely wrong geometry, because C++
  integer division truncates toward zero where Python floors. It rendered as
  a flat sliver. Only looking at the output caught it.
- **Render it, or listen to it.** The `.GFX` planar-vs-chunky mistake passed
  every header validation and was invisible until drawn. The user's listening
  test on the converted WAVs retroactively validated the LZSS decoder for
  *every* format, since a subtly wrong ring buffer would still produce
  correct-length output.
- **Cross-check new decoders against the Python tools.** `csp_decode.py INFO`
  prints dimensions, origin and pixel count; comparing against it located the
  floor-division bug immediately.
- **Separate proven from guessed, in writing.** `docs/reversing-notes.md`
  marks which constants are measured and which are invented. Do not let those
  blur.
- Prefer an oracle over an opinion: exact decompressed sizes, byte-identical
  RTL, directory offsets landing precisely at EOF.

## Things known to be provisional

Gameplay constants are hand-tuned, not recovered — the grid is a guess at
7x10, as are spawn rate, fall speeds and scoring. Real values should come
from the playfield renderer at `1000:9e53` and the main loop at `1000:3a67`.
Measured-real values: 16x13 cells, playfield x range 74..245, drop limits
9/6/3.

Presentation is incomplete: `BEAKER.CSP` is not drawn, so atoms appear to
float, and the test tube's vertical placement is approximate.

## Open work

`.MUS` is the only unsolved format. It is OPL2 (proven by disassembling the
register writes in `FMMUSIC.DRV`) but the event stream is variable-length and
the instrument-table length cannot be inferred from the data. Progressing it
means disassembling the sequencer in `FMMUSIC.DRV`, not more statistical
probing — that avenue is exhausted and documented.

`.SPR` (2), `.ANM` (1) and `.BIN` (1) have never been examined.
