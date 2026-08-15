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

## The prime directive: transfer the logic, do not re-invent it

**The goal is a faithful translation of the original Turbo Pascal into C++/SDL:
the same game, playing virtually identically, on source that is readable and
extensible.** Not a game inspired by Tubes. `PLAN.md` opens with this.

**Every gameplay rule in `src/` must be derived from the original's code**, by
decompiling the Turbo Pascal and transliterating it to C++/SDL. The Pascal
source is almost certainly lost, so this binary is the only remaining record of
how Tubes works — the aim is a near-exact reconstruction of it, not a game that
behaves similarly.

**Black-box recreation is forbidden as a source of rules.** Watching the game
run and writing code that reproduces what you saw is not porting. It has
already been tried, in the session that measured the drops model, and it was
lossy in ways that were invisible from the inside:

- a scoring rule was fitted to two observed awards and was **wrong**, while the
  real rule sat in the game's own Instructions;
- Flashium's wildcard was **wrong in two different ways** until a player
  described what the game actually does;
- an "unresolved ambiguity in the original" was written up that was really a
  **bug in our own matcher**.

Each was caught by a human noticing, not by the method. Observation yields
samples; the binary yields the function. A rule that has never been exercised
on screen cannot be sampled at all, and you will not know it is missing.

**So the order of authority is:**

1. the **decompiled code** — the only thing that settles a rule;
2. the game's own text (Instructions, briefings, Credits) — good corroboration,
   and it has now **three times** held an answer that was being derived the
   hard way, most recently the Credits stating outright that Tubes "was written
   in Borland Pascal v7, and uses a planar 320x200x256" - the two assumptions
   this file's first paragraph has carried since day one. **Read the
   Instructions and the Credits early.** They cost nothing and this project
   left them until last;
3. **live measurement** — for *locating* and *validating*, never for deriving.

Measurement keeps a large role, just not that one. Measured addresses say
*where* to look in the disassembly (see the DS-offset table in
`docs/reversing-notes.md`), and the deterministic `DEMO.SCR` trace is the
**oracle**: ported logic must reproduce a captured state sequence. That is the
same standard already applied to music, where the sequencer is verified by
diffing register streams rather than by listening.

If a rule cannot yet be decompiled, leave it **explicitly marked as a
placeholder** — as `src/game.cpp` does at the top — rather than shipping a
plausible invention that later reads as settled.

## Hard rule: no game data in this repository

The game is still copyrighted. Assets are read at runtime from the user's own
copy; the engine takes `--gamedir`. `.gitignore` is deliberately aggressive
about `*.RES`, `*.EXE`, `assets-extracted/`, and rendered output.

**Always check `git diff --cached --name-only` before committing.** It has
already caught one near-miss (`__pycache__`). Never `git add -A` and trust it
blindly.

The rule has a second half that is easy to miss: **do not write to the player's
game directory either.** `TUBES.HSC` and `TUBES.SAV` are the player's files,
the port writes both, and a scripted run has already created one by accident -
see "Build, test, run". Reproduce captured data in a test from the constants
instead; that is how the `TUBES.HSC` sentinel residue is pinned without the
capture being in the repo.

## Commit discipline: small, atomic, and as you go

**The repository is published**, at `github.com/stev0760/tubes-port`, `main`
tracking `origin/main`. That is new as of 2026-08-15 and it changes two of the
assumptions this file used to carry.

A working tree that has drifted from the last commit is still unbacked work,
and a session that ends without committing still loses its reasoning even if
the code survives - so commit as you go, exactly as before. What changed is
that **a commit is not backed up until it is pushed**, and that **history is no
longer free to rewrite**. The identity rewrite that was cheap on 2026-08-08
would now break every clone and every hash anyone has quoted. Rewrite nothing
that has been pushed.

Anything committed from here also lands in public. `git config --local` is the
noreply address, so authorship is fine; what needs the same care as ever is the
no-game-data rule, which a push makes irreversible in a way a local commit
never did.

- **Commit each landing, not each session.** One finding, one fix, or one
  transliterated routine is a commit. If a change needs "and also" to describe
  it, it is probably two commits - the catch-time specials and the score-ramp
  clock they exposed were separable and should have been split.
- **Commit before starting the next thing**, not after. Picking up new work on
  a dirty tree is how a bisectable history stops being bisectable.
- **The message carries the derivation.** State the address the rule came out
  of, what the code actually says, and what the port had wrong - the commit log
  is the second copy of `docs/reversing-notes.md` and has been read as one.
- **Leave the tree clean.** Untracked scratch is not free: it hides the one
  file that should have been added. Either commit it or add it to
  `.gitignore`; do not let it accumulate.
- Tests and the pixel diff run **before** the commit, and the numbers go in the
  message. "111 checks, 0.02% to 0.22%" is how a later session knows whether
  its own regression was already there.

## Layout

| Path | Contents |
|---|---|
| `src/` | the engine (C++17, SDL2) |
| `src/screens.cpp` | every screen the port draws - NO SDL, and in `tubes-tests` |
| `src/uistate.h` | the frame loop's own state, grouped - NO SDL, and in `tubes-tests` |
| `src/main.cpp`, `boot.cpp`, `present.cpp`, `opl.cpp` | the SDL edge, and the only files that may include it |
| `src/instructions.cpp`, `credits.cpp`, `cutscene.cpp` | GENERATED - see "extract, do not transcribe" |
| `tools/` | Python decoders, one per format, plus `unpack.sh` |
| `ghidra_scripts/` | Java `GhidraScript` files for headless analysis |
| `third_party/` | vendored deps, unmodified — currently Nuked-OPL3 (LGPL 2.1) |
| `docs/reversing-notes.md` | every format, with what is proven vs guessed |
| `docs/debug-rig.md` | the live DOSBox-X debugging setup and its limits |
| `docs/worklog.md` | chronological record of the work |
| `PLAN.md` | roadmap, current status, and what is known-wrong |

The original game files live in the parent directory, `..`. The Ghidra
project is at `../ghidra-project` — outside this repo on purpose, since it is
derived from copyrighted data. So is the debugging rig and everything it
captures, at `~/Dev/tubes-tooling/`.

`src/` splits platform-agnostic logic from the SDL edge, and the split is what
makes a port to another platform tractable at all. **Four files include SDL and
no others may: `main.cpp`, `boot.cpp`, `present.cpp` and `opl.cpp`.** `board`,
`game`, `wave`, `menu`, `session`, `hiscore`, `save`, `res`, `gfx`, `mus`,
`font`, `screen`, `screens` and `uistate` are portable and must stay that way.

The edge is small and each file has one job. `present.cpp` puts a finished
frame on the window and owns the display options. `boot.cpp` holds the screens
that run their own blocking loop - both splashes, the cutscene, the edition
prompt, the sign-off. `main.cpp` is the CLI, the setup and the frame loop.

`screens.cpp` is the one to know about: it is every screen the port draws, it
has no SDL in it, and it is compiled into `tubes-tests` as well as
`tubes-port`. Until it was split out of `main.cpp` none of the port's rendering
could be tested at all, because `tubes-tests` links no SDL and `main.cpp` was
one translation unit with `SDL_Init` in it. Keep it that way: a screen that
needs an SDL call is a screen whose caller should do that call and pass the
result in. `drawRebindScreen` is the worked example - it takes the six finished
key names rather than the `Bindings` they come from, because only
`SDL_GetScancodeName` knows what a key is called.

## Build, test, run

    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ./build/tubes-tests                      # 1021 checks, and rising
    ./build/tubes-port --gamedir ..

Music is verified by diffing register streams, not by listening:
`--dump-regs NAME` prints what the sequencer writes to the chip, and it must
match `tools/mus_decode.py` exactly. All 10 songs currently do. That check is
independent of the OPL emulator, so a synthesis bug can never be mistaken for
a sequencer bug. `--render-mus NAME OUT.wav` renders offline.

`--screenshot FILE` renders one frame to a BMP and exits; it works headless
under `SDL_VIDEODRIVER=dummy`. `--auto N` runs N frames of a scripted player
first, so a headless capture shows a populated beaker. Together these are how
rendering gets verified without a display. `--title N`, `--hiscores [0|1]`,
`--wave N`, `--f2`, `--rebind`, `--instructions [N]` and `--credits` each open
a specific screen for capture, and `--dump-save FILE` decodes a `TUBES.SAV` and
re-encodes it, so the C++ reader can be diffed against `tools/sav_decode.py`
rather than trusted.

**A capture flag must never write to the game directory.** `--auto-advance`
walks a whole session, so it once qualified for a high score and saved a real
`TUBES.HSC` into the player's own game files - which then silently changed what
every later capture compared against.

That rule is no longer a guard to remember. **Every write to a file the player
owns goes through `tubes::PlayerFiles`** (`src/playerfiles.h`), which a scripted
run is handed blocked and an interactive run writing; the writers do not ask
which they hold. A new write path that opens its own `ofstream` is the bug the
class exists to make visible. The flag that classifies the run is `scripted` in
`main.cpp` - it was called `harness`, which named the DOSBox rig rather than
anything in this program.

## Toolchain gotchas

- **Ghidra 12.2 will not run `.py` scripts.** Jython ships as an uninstalled
  extension and PyGhidra wants an interactive venv install. Write Java
  `GhidraScript` files; they compile on the fly with no setup.
- Ghidra needs **absolute paths** for both `-scriptPath` and the project
  directory. A leading `./` is rejected outright.
- Set `__stdcall16far` on game functions before decompiling. Pascal is
  callee-cleans; Ghidra's inferred cdecl signatures come out wrong.
- **A function taking an open array decompiles to garbage.** Turbo Pascal
  copies conformant arrays onto the stack at entry, and Ghidra renders the copy
  loops as a wall of `puVar`. `1b2e:0f46` is the example. Read the listing -
  its one timing literal was visible in a single grep.
- Ghidra creates no xrefs for DS-relative globals in 16-bit segmented code.
  Use `ghidra_scripts/FindScalarRefs.java` to chase them.
- Turbo Pascal 7.0 runs headless under DOSBox; `TPC.EXE` is the scriptable
  compiler, `TURBO.EXE` is the IDE and is not.

## Verification standards

This project has been unusually rigorous about proof, and it has repeatedly
paid off. Keep it up.

- **Diff the COLOURS, not just the count.** The cutscene's last 144 pixels sat
  open for two sessions, written up as "the original still holds the
  difference" and blamed on unread page bookkeeping. One `Counter` over the
  differing pixels said `(0,0,0) x 144` - the original was BLACK there and the
  port was showing something, the opposite way round from the write-up - and
  the cause fell out in minutes. A pixel count says where; the values say what.
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
- **A position in a frame is part of the transliteration.** The clear timer was
  decremented at the end of the beaker update instead of after the
  wave-complete test, one statement early, and every clear animation in the
  game was judged a frame short. The order of statements is derived too.
- Prefer an oracle over an opinion: exact decompressed sizes, byte-identical
  RTL, directory offsets landing precisely at EOF, a re-encoded `TUBES.SAV`
  differing in 0 of 960 bytes.

## The one place this port does NOT transliterate

Control bindings. The original asks an input driver for one byte a frame - Up,
Down, Left, Right, A, B - and **that byte is game logic and does not change**:
`DEMO.SCR` stores exactly one per frame, which is why a recorded demo replays
through the same code path as live play.

Everything BELOW it is DOS plumbing. `KEYBOARD.DRV`, `JOYSTK1/2.DRV` and
`MOUSE.DRV` exist because 1994 had no abstraction over an XT keyboard, a
gameport and a serial mouse; SDL is that abstraction, so porting a driver
chooser would be transliterating the *absence* of SDL. `src/input.h` carries
the full reasoning. `SETUP.CFG` is deliberately not written - it is the DOS
install's hardware config and `SETUP.EXE` owns it.

This is the only such departure, and it was agreed with the player before it
was written. Do not treat it as a precedent for gameplay.

## Things known to be provisional

Far less than there used to be. `1000:3a67`, `1000:0f80` and `1000:9e53` are
all transliterated, so the geometry, the atom state machine, the dispenser, the
drops pool, the scoring ramp, the wave table and the whole progression are read
rather than fitted. `PLAN.md` carries the list; do not duplicate it here.

What is worth knowing before touching gameplay:

- **the playfield array is 6 x 5**, off the loop bounds in `1000:3a67`. The old
  7x10 came from the manual and was wrong in both dimensions.
- **atom type numbers**: 1..7 the ordinary colours in the order Red, Green,
  Blue, Cyan, Purple, Yellow, Pink, then 8 Flashium, 9 AntiMatter, 10 Bonus,
  11 Xenon, 12..16 the letter balls, 19 the hidden-atom sprite. A settled
  beaker cell holds `type + 19 * fadeFrame`, so one table draws both.
- **the test tube holds 5**, stated by the in-game Instructions, not 5/3/2 by
  difficulty as the sprite heights suggested.
- the **frame rate is 16.11 Hz**, not the 18.2 the project assumed for months
  - and it is now derived rather than measured: `21ea:0690` stores
  `145 div fps` as the timer period, the session asks for a flat 16, and
  `145 / (145 div 16)` is 16.11. The odd constant is truncation, not a choice.
- **`23e7:0024` is `Delay(n)` and its unit is the vertical retrace**, so every
  hold in the game is n/70 s.

The open gameplay questions are in `PLAN.md`'s "Known wrong" and
"Player-reported differences" sections, which are kept current.

**The screen cross-fade is found**, after three failed searches, and how it
was found is the lesson: the player said it behaved like a *mandatory* effect,
which meant a graphics unit rather than the game. It is - `23e7:0097` fades in
and `23e7:00ce` fades out, and every screen calls them. A routine that
everything calls is not one any single screen names, so searching the game
segments for a fade call could never have worked.

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

## Open work

**Read `PLAN.md` first - it opens with the next step.** **Every menu item now
works**, and so does the whole boot sequence: both splashes, the title, play,
load, save (F2), Game Options with control rebinding, High Scores,
Instructions, View Demo and Credits, with attract mode cycling on its own.

**Every screen of the original is now ported**, and the player's polish list in
`PLAN.md` section 4.5 is **done** - the classroom animations, the joke slide,
the screen fade and the truncated sounds, all four. Its fifth item, ports to
other platforms, was never a polish task: see `Portability`.

So nothing of the original program is outstanding. What is left is the list at
the top of `PLAN.md` - the player's full playthrough on Tubes 101, the GLDFADE
question, a Credits slide of the port's own - and then **publishing**, whose
checklist is the last section of `PLAN.md`. `LICENSE`, `README.md` and the
`harness` flag are all done, so what is left there is the identity rewrite and
the repository check - paperwork rather than code.

The rig is built and lives **outside this repo**, at `~/Dev/tubes-tooling/` -
`docs/debug-rig.md` covers it. Three things to know before planning against it.
It runs `assets-extracted/TUBES_UNP.EXE`, the unpacked image Ghidra analysed,
presented to DOS as `TUBES.EXE` - debugging the shipped packed binary would
break at LZEXE's stub instead of the game. The GDB stub has **no memory
watchpoints**, only execution breakpoints, so "break on a write to the atom
array" needs a small patch first. And `DOSBoxInstance.start()` runs
`pkill -9 -f dosbox-x`: a capture script has no business doing that to a
machine that may have a game running on it, so subclass it and neuter
`_kill_existing`, as `grab_hiscores.py` does.

**A static screen diffs at ZERO.** `diff_frame.py` compares only grey
structural pixels and masks the atoms, because a gameplay frame has a random
backdrop and things moving during the capture. A menu-side screen has none of
that, so `diff_hiscores.py` compares all 64,000 pixels with no mask - and both
high-score pages come back at 0. Reach for the exact comparison on any static
screen; it has already caught a bug that had nothing to do with rendering.

The segment mapping is settled: DGROUP is Ghidra `0x2785` = `L + 0x1785`, and
`L` is `CS` at the entry breakpoint. Proven against the file, not guessed.

Ports to other platforms are an eventual goal, so keep SDL at the platform
edge - it is the portability layer, not something to avoid. See `Layout` for
which files may include it.

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

**And suspect the KIND of search, not just its width.** The high score viewer
was written up as "unfound, not absent" after `FindScalarRefs` on the two bank
addresses returned only the loader and the entry screen, with a plausible
reason attached: a routine taking the bank as a parameter is invisible to a
scalar scan. The reasoning was sound and the conclusion was wrong -
`1b2e:61b6` names both banks with plain literals. What was missing was a
*string* search:

    awk '/^@FUNC/{f=$0} /@STR/{print f" || "$0}' map.txt | grep -i "high scores"

One line over a `MapProgram` dump already on disk. A screen the player can
describe in detail cannot be code that does not exist, and this file had
quoted one of its own strings for months.

## Reversing method that has actually worked

Ranked by how often it produced the answer:

1. **Find a second consumer of the same data.** `.MUS` fell apart in an hour
   once `GMMUSIC.DRV` was read alongside `FMMUSIC.DRV` — two drivers eating
   one byte stream pin down every field between them. A whole session of
   statistical probing before that produced nothing.
2. **Read the code, don't stare at the bytes.** LZSS, the container, and the
   `.MUS` event grammar all came off the disassembly directly. Every
   histogram-and-stride guess was wrong.
3. **Extract data mechanically; never transcribe it.** The Instructions are
   152 strings and the Credits 36, and typing them would have been 188 chances
   to mistype a line of the game's own documentation and never notice.
   `tools/gen_instructions.py` reads the disassembly and emits the tables,
   because every text call is a fixed push sequence. It did both screens, and
   `tools/gen_cutscene.py` then did a third - and that one earned its keep
   twice over, because the cutscene's 26-slot frame list is built by COPYING
   pointers and no amount of squinting would have got it right. Anything that
   is a list in the binary should arrive in `src/` the same way.
4. **Look for external standards in the decoded output.** GM program and drum
   numbers, 768-byte VGA palettes, equal-tempered frequencies — these can't
   be artifacts of a wrong decode, so they confirm independently.
5. **When two readings of the same bytes disagree, capture the screen.** The
   save file's menu arms read `+0x28` for Endurance and `+0x26` for Wave, which
   looked like an off-by-two until the list was captured from the original:
   both were right, because Endurance has no waves and lists chains instead.
   Five minutes with the rig against an afternoon of argument - and this is
   measurement in the role the prime directive allows, arbitrating a reading
   rather than producing one.
