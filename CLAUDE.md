# CLAUDE.md

Working notes for this repository. **`PLAN.md` is what is left to do** - read
it first when picking work up. `docs/reversing-notes.md` has the format
details, `docs/worklog.md` what happened when.

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
| `docs/debug-rig.md` | the live DOSBox-X debugging setup and its limits |
| `docs/worklog.md` | chronological record of the work |
| `PLAN.md` | roadmap, current status, and what is known-wrong |

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
progression rules in `1000:9e53`. **Also measured:** the
playfield array is 6 x 5, read off the loop bounds in `1000:3a67` (the old
7x10 came from the manual and was wrong in both dimensions).
**Also measured:** the full
cell-to-pixel mapping - x = {107,125,143,161,179,197} (pitch 18, not 16),
y = row * 13 + 121.

**Also measured, from attract mode** (`DEMO.SCR` replays through the normal
game loop, so the demo is a full play session with no human pacing it, and it
is deterministic - verified by diffing two cold-boot runs):

- the **atom type field is `+0x0b`**, at 96.2% over 79 settle events. The
  earlier "refutation" of it used an array base six bytes early.
- **atom type numbers**: 1..7 the ordinary colours in the order Red, Green,
  Blue, Cyan, Purple, Yellow, Pink, then 8 Flashium, 9 AntiMatter, 10 Bonus,
  11 Xenon, 12..16 the letter balls. A settled beaker cell holds this byte.
- **drops are one pool that counts down**: seeded 9/6/3 by difficulty,
  -1 per miss, +1 per Bonus caught, untouched by clearing a wave.
- **the test tube holds 5**, stated by the in-game Instructions, not 5/3/2 by
  difficulty as the sprite heights suggested.
- **scoring is in units of 250**, and the score *ramps* toward its award in
  roughly sixths rather than snapping.

**Still guessed:** spawn rate, fall speeds, the special atoms' spawn rates, and
how run length versus orientation splits the award (the two measured awards -
a vertical 3 paying 250 and a diagonal 4 paying 1000 - differ in both, so they
are confounded; a horizontal 4 or a run of 5 would separate them).

When chasing a DS-relative global, establish which segment DS actually holds
first. Two separate wrong turns came from this: `SS:SP` in the EXE header
points past the image and is not DGROUP (which is Ghidra segment `2785`), and
one unit sets `DS = CS` so its `ds:0x1e` is unrelated to the game's.

`ghidra_scripts/MapProgram.java` dumps the call graph plus the strings each
function references; that is what identified every interface stage (splashes,
menu, blackboard cutscene, game session). Re-run it rather than guessing at
what a function does.

`1000:9e53` is the game *session* — it loads the play-area art, seeds
difficulty, then runs the frame loop. `1000:3a67` is a **nested Pascal
procedure** inside it, sharing its locals, which is why it appears to take no
arguments. Decompile the two together.

Presentation is incomplete: `BEAKER.CSP` is not drawn, so atoms appear to
float, and the test tube's vertical placement is approximate.

## Open work

**Read `PLAN.md` first - it opens with the next step.** In short: static
disassembly of `1000:3a67` has hit its limit, and the technique has switched to
watching the game run under a debugger. `PLAN.md` has the exact addresses and
what to watch.

The rig is built and lives **outside this repo**, at `~/Dev/tubes-tooling/` -
`docs/debug-rig.md` covers it. Two things to know before planning against it.
It runs `assets-extracted/TUBES_UNP.EXE`, the unpacked image Ghidra analysed,
presented to DOS as `TUBES.EXE` - debugging the shipped packed binary would
break at LZEXE's stub instead of the game. And the GDB stub has **no memory
watchpoints**, only execution breakpoints, so "break on a write to the atom
array" needs a small patch first.

The segment mapping is settled: DGROUP is Ghidra `0x2785` = `L + 0x1785`, and
`L` is `CS` at the entry breakpoint. Proven against the file, not guessed.

Ports to other platforms are an eventual goal, so keep SDL at the platform
edge - it is the portability layer, not something to avoid. Only `main.cpp`
and `opl.cpp` include it today; `res`, `gfx`, `mus`, `board`, `game` and even
`screen` are platform-agnostic and should stay that way.

## When a search comes back empty, suspect the search

Five wrong conclusions in one session, every one from a tool rather than the
binary (all in `docs/worklog.md`):

- a DGROUP scan requiring runs of 8+ words missed a 6-word table
- a correlation window of 25 instructions "disproved" a correct reading
- a regex matching only positive displacements hid every stack local, and
  reported 9 mutations where there were 38
- two structures were assumed to share a base pointer, repeatedly
- a sprite was measured by "widest gap at three sample rows" instead of
  occupancy over its full extent, and written off twice

A negative result is only as good as the filter that produced it. Before
reporting "there is no X", check that the search could have found X. The
tell each time was an implausible number - zero mutations in 1392 bytes of
code is not a finding, it is a bug.

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
