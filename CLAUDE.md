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
| `third_party/` | vendored deps, unmodified — currently Nuked-OPL3 (LGPL 2.1) |
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

Music is verified by diffing register streams, not by listening:
`--dump-regs NAME` prints what the sequencer writes to the chip, and it must
match `tools/mus_decode.py` exactly. All 10 songs currently do. That check is
independent of the OPL emulator, so a synthesis bug can never be mistaken for
a sequencer bug. `--render-mus NAME OUT.wav` renders offline.

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

Gameplay constants are partly recovered. **Measured:** 16x13 cells,
playfield x range 74..245, drop limits 9/6/3, and the difficulty seed and
progression rules in `1000:9e53`. **Still guessed:** grid columns and rows
(7x10), spawn rate, fall speeds, scoring.

`1000:9e53` is the game *session* — it loads the play-area art, seeds
difficulty, then runs the frame loop. `1000:3a67` is a **nested Pascal
procedure** inside it, sharing its locals, which is why it appears to take no
arguments. Decompile the two together.

Presentation is incomplete: `BEAKER.CSP` is not drawn, so atoms appear to
float, and the test tube's vertical placement is approximate.

## Open work

Every format the game loads is now decoded, `.MUS` included. What remains is
engine work:

- **Beaker and test tube presentation.** `BEAKER.CSP` is not drawn and the
  tube sits at the top of the screen, so atoms appear to float.
- Sound effects through SDL_mixer; wave and endurance modes.

`.SPR` (2), `.ANM` (1) and `.BIN` (1) have never been examined. Nothing
appears to need them yet.

## Reversing method that has actually worked

Ranked by how often it produced the answer:

1. **Find a second consumer of the same data.** `.MUS` fell apart in an hour
   once `GMMUSIC.DRV` was read alongside `FMMUSIC.DRV` — two drivers eating
   one byte stream pin down every field between them. A whole session of
   statistical probing before that produced nothing.
2. **Read the code, don't stare at the bytes.** LZSS, the container, and the
   `.MUS` event grammar all came off the disassembly directly. Every
   histogram-and-stride guess was wrong.
3. **Look for external standards in the decoded output.** GM program and drum
   numbers, 768-byte VGA palettes, equal-tempered frequencies — these can't
   be artifacts of a wrong decode, so they confirm independently.
