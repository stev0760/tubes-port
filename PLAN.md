# Roadmap

Cross-session tracking. `CLAUDE.md` is how to work in this repo,
`docs/reversing-notes.md` is what the formats are, `docs/worklog.md` is what
happened when. **This file is what is left to do.**

Update it when something lands or when a plan turns out to be wrong. A stale
plan is worse than none.

---

## The goal

**Faithfully translate the original Turbo Pascal to C++/SDL, so the game plays
virtually identically - but on source that is readable and extensible.**

Not a game inspired by Tubes, and not a reimplementation that behaves similarly.
The target is the same game: same rules, same speeds, same pixels, arrived at by
reading the original's code and writing the equivalent in C++. The Pascal source
is almost certainly lost, so this binary is the only surviving record of how
Tubes works, and the port is meant to become the readable version of it.

What "faithful" buys is a base worth extending. Once the behaviour matches, the
code is a normal C++/SDL codebase - portable, testable, modifiable - rather than
16-bit real-mode Pascal that only runs under emulation.

Two consequences that shape every decision here:

* **Rules come from the decompiled code, never from watching the game.** See
  the prime directive in `CLAUDE.md`. Observation gives samples; the binary
  gives the function.
* **"Close enough" is not the bar.** The pixel-diff harness exists so that
  "virtually identical" is a measured number rather than an impression.

---

## The next step, in one place

**Every screen of the original is ported, every asset format is decoded, and
nothing in the program is unread.** `1000:9499`, the wave-75 ending, was the
last one and it is done - so what is left is the enhancements the player has
asked for, and then publishing:

1. ~~a **Graphics Options screen**~~ - **DONE**, see section 5. Fullscreen,
   window size, 4:3, vsync and scanlines, on the port's own screen beside the
   rebinding one, all render-side.
2. **A FULL PLAYTHROUGH on Tubes 101** - the player's, and it is the item that
   matters most, because it is the only thing that exercises all 25 objective
   templates in sequence. It has already paid for itself: wave 8 asked for
   marked atoms and could not be passed, and neither could the other **eight**
   mode-6 waves, because the objective counter was decremented by nothing. See
   the worklog. Every live mode now has a test that carries a wave to COMPLETE
   rather than merely to counter-zero, which is the assertion that would have
   caught it.
3. **GLDFADE**, the remaining enhancement in section 5. **Open, and not on the
   Bonus atom** - the player's call, and it is the right one: converting the
   Bonus on catch is the original's rule and bending it to make a gold fade
   happen is a worse trade than leaving the animation unshown. Where else it
   could live is undecided. This is a "think about it" item, not a queued one.
4. **A new Credits slide.** The port's own, added to `1b2e:411b`'s four pages.
   `src/credits.cpp` is GENERATED - by `tools/gen_instructions.py`, which is
   parameterised and emits both screens - so the addition has to survive a
   regeneration. That means a fifth page **appended by the port**, not an edit
   to the four that are extracted: the same shape as `kOptionsPagePort` beside
   `kMenuPages`, and for the same reason.

   ~~Noted while checking this, and left for the lint pass: **`src/credits.cpp`'s
   header comment is the Instructions' header**, verbatim.~~ **FIXED.** It now
   names `1b2e:411b`, its 4 pages and 36 strings, and its own regenerate command
   line - as does `src/instructions.cpp`, which had none.
5. **Shareware edition support**, via a `--shareware` switch - and it turned
   out to be far LARGER than this list assumed, twice over. It was written up
   as "nothing to detect and nothing missing" on the strength of the `.RES`
   being byte-identical; then the player ran the shareware build. It has an
   extra menu item (**Preview Registered**, with its own wave list starting on
   the Mischief Crystal wave), no Bonus or AntiMatter in normal play, an
   **Ordering Info slide deck**, a registration deck on the way out, and it
   dumps `TUBESEND.BIN` to the DOS screen as it quits. Its Instructions text is
   even re-wrapped differently. **So this is a second program to decompile, not
   a flag over the first**, and it is now the largest remaining item in this
   file. Savegames and high scores must be kept separate between the modes -
   the one part of it that can damage a player's own files. See "What shareware
   mode actually is" below, which carries the full plan and the order of work.

   The payoff is worth the size: it makes the port playable by someone who only
   ever had the shareware disc, which is a fitting thing for an abandonware
   preservation project to be able to do.
6. **Publishing**, which is the last section of this file and is gated on a
   comment and documentation pass, a repository check and a final code review.
   **The player has a specific method in mind for the review and lint pass, so
   do not start one unprompted.** The licence is decided: **MIT** for this
   project's own code, with `third_party/nuked-opl3` keeping its LGPL-2.1
   notices.

**The three animations that used to head this list are done**, and the two
leads it carried were both pointing at the wrong routine:

* the **projector screen roll-down** is `1b2e:0510`, not `1b2e:0a11`'s
  `DS:0x210e` animation, and nothing gates it - what varies is which screens
  call `1b2e:0510` at all;
* **Professor Lanny's mouth** is `TALK1..5.GFX` driven by `1b2e:0cd1`, a
  SECOND key wait every screen runs before `1b2e:0e37`, not a fourth arm of
  `1b2e:0656`. `DS:0x20c8` is never written by anything, so `0656`'s clap arm
  is unreachable in this build, and `DS:0x20e3` is set by `1000:9499`;
* the **joke slide** is `1b2e:084e` and `FLASH.GFX`, which this project's own
  notes had quoted as "a one-in-twenty easter egg" without asking what it was.

`POINTERT.GFX` turned out to belong to the joke slide rather than to the wave,
so nothing in the classroom's resource table is unattributed any more.

The three-pixel floor at (59, 10..12) on the play field is still unexplained
and has been for months. It is not worth a session on its own.

## Where this stands

Percentages are judgement calls, so the breakdown matters more than the number.

### Reversing: ~95%

| Area | State |
|---|---|
| Every asset format | **done** - container, LZSS, `.CSP`, `.GFX`/`.PAL`, fonts, `.SFX`, `.SCR`, `.MUS` |
| Program map, every interface stage | **done** |
| Playfield geometry and cell-to-pixel mapping | **done**, measured |
| Ball table and the 19 type numbers | **done**, read out of the entry program's own initialiser |
| Fade encoding (`type + 19*frame`, one table) | **done** |
| Beaker structure (three `array[1..5,1..6]` planes) | **done**, decompiled |
| Atom router `1000:0f80` | **done** - states, fixed-point, both arc tables |
| Network topology (feed / lane / destination per column) | **done**, measured |
| Drops, tube capacity, save format, menus | **done** |
| Render order and the dirty-rect model | **done** |
| The frame render, end to end | **done** - draw order, the six atom slots, the GAMEFG stamp |
| `.CSP` placement offsets | **done** - the `(128, -2)` base |
| Spawn: period, type distribution, column choice | **done**, decompiled |
| Difficulty seeds and their per-wave stepping | **done** |
| The test tube's record, state machine and five slots | **done** - and it is a stack |
| The beaker update `1000:22a6` | **done** - three planes, four matchers, fade, gravity |
| The tipping animation `1000:463a`, and records 7..12 | **done** - four phases, state 8 and state 9 |
| Text rendering and the HUD | **done** - the colour walk, the shadow, both fonts |
| Sound: the `.SFX` header from the driver side, and which sound plays when | **done** |
| Beaker-side specials `1000:2790` | **done** - AntiMatter, Blocker, Convertor |
| Catch-time specials `1000:180c` | **done** - Bonus, Multiplier, EvilMultiplier, Filler |
| Scoring and the chain bonus multiplier | **done**, from code rather than the manual |
| Wave definitions: the 75-wave table, 25 objective templates, the modifiers, the progression | **done** - `1000:86b8` is the table, `1000:a616` steps it |
| The four wave-setup routines | **done** - `1000:0000` marked, `0236` crystals, `035e` pre-fill, `4bf6` the morph |
| The Mischief Crystal, end to end | **done** - the 10-byte record, the teleport `1000:0560`, the AntiMatter removal `041c`, the gravity follow `04ca` |

Also done since: **every asset format** (`.SPR` and `.ANM` fell in one
session), both splashes, the opening cutscene with its two-track animation
player, and the screen fade that was open from the first session.

**Nothing in the program or the archive is unread.** The three that were:

- ~~`1000:9499`, the wave-75 ending~~ - **done**, and it is what set
  `DS:0x20e3`. Verified from play: a real wave-75 clear runs the ending, the
  high score entry screen and the return to the title. `--ending [N]` opens
  either page, `--hs-entry` the entry screen, and `--wave 75 --make-save FILE`
  writes a save to reach it by playing
- ~~the video page bookkeeping~~ - **done**. Four 16000-byte pages, page 3 the
  clean backdrop everything erases from, `2321:024d` is
  `CopyRect(src, dst, x, y, w, h)`, `1b2e:1188` a three-line restore-from-3,
  `1b2e:0f46`'s second toggle is what pins a lone track to the shown page, and
  `DS:0x2058` was `BLACKBRD.GFX` all along. It was NOT the cause of the
  cutscene's 144 pixels - see the notes
- ~~`.BIN`~~ - **done**, and it was the last one. `TUBESEND.BIN` is a raw DOS
  text-mode screen dump (80 x 23 character/attribute pairs) and it is the
  **shareware exit screen**. Nothing reads it: "TUBESEND" appears exactly once
  in the whole game directory, in the archive's own directory entry. It is the
  shareware build's sign-off carried in a shared archive. `tools/bin_decode.py`
  renders it; see the notes for what it settles

### The engine: ~90%

**Every screen of the original program is ported.** The number is no longer
about missing screens - it is about the polish inside them, and about how much
of what is on screen has been measured against the original rather than merely
read.

Working, and transliterated rather than invented:

- the atom router, network topology and fixed frame step - including that the
  descent **accelerates below y = 50** to a flat 9 px a frame, so the
  difficulty speed only governs the top of a play column
- the catch as a **window** at y 60..70, refused while the tube is tipping
- **Flashium's colour cycle**: type 8 has no sprite, and the original rewrites
  its ball-table slot every fourth frame from the seven ordinary colours
- **the whole frame render**: the six per-column atom slots interleaved with
  the three furniture passes, the GAMEFG stamp that clips an atom to the pipe
  it is inside, `.CSP` placement offsets, and the beaker and test tube layering
- the spawn: period, column choice with retries, and the full type distribution
- the test tube's 6 px/frame slide and the Down/B speed boost
- **the beaker as the original's three planes**: cells holding
  `type + 19*fadeFrame`, the marked plane that drives the clear animation, and
  the objective plane with its `MARKER` overlay
- the four match scans with their real bounds, the per-seed awards, the chain
  bonus multiplier and the six-frame score ramp
- Flashium's wildcard - including that it ADOPTS the colour it completes
- gravity at one row per frame, which is what makes the beaker settle
- the beaker-side specials: AntiMatter's 3x3 blast, the Blocker filling its
  column, the Convertor converting by type board-wide, and the four consumables
  going inert if they settle
- **the catch-side specials**: the Bonus becoming Flashium, granting a drop and
  paying a 1000 award that *grows by 1000 every time*; the Multiplier topping
  the tube up to five with 1..8 rolls; the Evil Multiplier doing the same with
  Xenon; and the Filler parking an untippable type 17 in the bottom slot
- **the tube as a stack**, tipping the atom caught last, and refusing a 17
- **the tipping animation**: four phases on a 2-frame divider, `TESTUBE1/2/3`
  chosen by the phase, the contents bunching and pouring on per-slot literals
- **the in-tube slide** and **records 7..12**, so a tipped atom visibly leaves
  the tube, falls 9 px a frame and lands on the first free row - or is lost,
  at the cost of a drop, if its column is full
- **the HUD**: Chains, the centred score and Drops, with the score pop-ups -
  and under it a transliterated text renderer, so a glyph is a vertical colour
  walk off one palette index with a shadow, exactly as `2000:35ec` draws it
- **wave mode's rules**: the 75-wave table from `1000:86b8`, all 25 objective
  routines, the six counters and the progression at `1000:a616`, the scoring
  hook `1000:192f` firing once per SEED, the 720-frame Task Display clock and
  the wave-complete test - in `src/wave.{h,cpp}`, with `--wave N` to play one
- the **wave setup**: the marked atoms with their covering and Xenon variants
  (`1000:0000`), the pre-filled beaker (`1000:035e`), and the beaker morph
  (`1000:4bf6`) - which is a colour ROTATION, so it preserves every chain
- **the Mischief Crystal, whole**: placed with staggered clocks (`1000:0236`),
  teleporting through `CRFADE` played out and back in (`1000:0560`), its record
  following its cell down the gravity pass (`1000:04ca`), and removable only by
  AntiMatter (`1000:041c`)
- the **hidden-atom modifier**, `-0x189` - `MYSTBALL` substituted at the six
  network draw sites and nowhere else, so the concealment ends on the catch
- the **Task Display**, `1000:2a4a` and `1000:2894` - the count over a full-size
  ball in the count modes, three half-size ones in the shape of the required
  chain in the chain modes, and both cycling on the Flashium tick when the wave
  names no colour
- music: all ten songs, correct at the register level
- **sound effects**: the eleven fade families plus DROP, HITGLASS and HITATOM,
  each fired from the site the original calls `PlaySound` at, through ONE voice
  because that is all `SBSOUND.DRV` has

The surrounding screens, and where each stands:

| Screen | State |
|---|---|
| The briefing `1000:86b8` | **done and measured** - title band **0.00%**, body **0.09%**, whole slide **0.14%**. Open: the projector slide is a measured rectangle rather than `1000:bcf1`, and the professor is absent |
| The title screen and menu `1b2e:52bf` / `1b2e:4d80` | **done and measured** - menu text **0.00%** on pages 1 and 3, whole screen **0.25%**. All seven pages, the transitions, the two-key protocol, the turning stars and the 25-leg letterform walk |
| Demo playback (`.SCR` replay through the same loop) | **done** - matches end to end, terminates on the original's own last byte |
| The Software Creations splash `21d5:007b` | **done** - 0 pixels of 64,000 against the reference render |
| The Absolute Magic splash `2178:00eb` | **done** - the built backdrop, the logo zoom, five strikes with the white flash, the writing |
| The opening cutscene `1b2e:1651` | **done and measured** - **0 pixels of 64,000** on all five pages, whole screen, nothing masked, 42 captures. Nothing open |
| Instructions `1b2e:2d63` and Credits `1b2e:411b` | **done** - all 21 slides and 4 pages, EXTRACTED not transcribed; a captured slide diffs at **8 pixels of 64,000** |
| High scores, viewer and entry | **done** - both viewer pages at **0 of 64,000**, unmasked; `TUBES.HSC` round-trips byte-exact |
| Save / load, the menu and F2 | **done** - `TUBES.SAV` re-encodes byte-exact; the slot list diffs at 252 px, all star-rotation phase |
| The stats blackboard `1000:8da5` and Continue `1000:8c38` | **done**. Open: the background is a stand-in - `2321:068d` blits a held image from `DS:0x2058` and nothing decompiled writes it |
| Attract mode | **done** - title, cutscene, demo, back to the title, with ESC in the cutscene skipping the demo as `1000:b287` does |

Pixel accuracy against the original reads **0.02% to 0.22%** of structural
pixels differing on the play field, over eight paused captures with the
backdrop excluded - down from 4.42%. The floor is three pixels at (59, 10..12),
where the original leaves a GAMEFG pixel erased for a reason not yet found.

### The frame rate was wrong for the whole project until now

`kOriginalFps` was **18.2 Hz**, the PC BIOS tick, assumed early and never
measured. It is **16.11 Hz**: `21ea:06ba` divides 145 by a per-frame period in
`DS:0x0d40`, which reads 9 in the session and 6 on the title, menu and
briefing. A least-squares fit of the original's `DEMO.SCR` byte index against
wall clock gives 16.18 Hz independently. Nothing in the simulation moved - the
frame *sequence* is identical - only the real-time speed.

---

## Done

| Area | State |
|---|---|
| `TUBES.EXE` unpacking (LZEXE v0.91) | done |
| `.RES` container + Okumura LZSS | done, 239/239 payloads exact |
| `.CSP` compiled sprites | done, 108/108 |
| `.GFX` raster images + `.PAL` | done, 73/73 |
| `.816`/`.88` fonts | done, 5 |
| `.SFX` digital audio | done, 24/24, confirmed by ear |
| `.SCR` demo recording | done |
| `.MUS` FM music | done, all 10; register stream matches the reference byte-for-byte |
| Renderer: 320x200 indexed, integer scaling | done |
| Music playback: sequencer + Nuked-OPL3 via SDL audio | done |
| Program map: every interface stage identified | done |
| Playfield geometry: 6 x 5 grid, pitch 18 x 13, origin (107, 134) | done |
| Beaker rendering | done |

**Every format in the archive is now decoded**, `.BIN` included - it was the
last, and it turned out to be a text-mode screen the shipped executable never
opens.

---

## Known wrong - rebuild, do not extend

Things currently implemented on assumptions the binary has since contradicted.
Listed first because building on them wastes work.

1. ~~**The dispenser path.**~~ **DONE.** `game.cpp` now transliterates the
   router `1000:0f80`: fixed-point motion at 1/128 px, states 3/5/6/7, the
   `DS:0x18` column table, and the two hand-tuned arc offset tables
   (`{9,6,4,2,1}` rising, `{9,6,3,2,1}` horizontally). The network topology -
   which feed tube serves which column along which lane - is in `game.cpp`.
2. ~~**The test tube holds one atom.**~~ **DONE.** Capacity is a flat 5, stated
   by the in-game Instructions. The 5/3/2-by-difficulty guess is retired.
3. ~~**Scoring and pacing.**~~ **DONE.** Scoring is now from code, not the
   Instructions: 250/500/1000 by orientation, added once per SEED position so a
   run of four pays twice, multiplied by the number of distinct runs formed at
   once, and ramped over six frames. `kSpawnIntervalFrames` is no longer
   invented either - the difficulty block seeds it at 70/60/50 frames.
   One conflict is open: an earlier live measurement had a diagonal four paying
   1000 where this pays 2000. The measurement is from the black-box session and
   the code is the authority, but it deserves a check once the HUD exists.
4. **Atom colour count and the special balls - now measured.** The engine's 8
   flat colours are wrong. There are **19 ball types**, read out of the live
   sprite tables at `DS:0x1da6` (balls) and `DS:0x1df6` (fades), 19 entries
   each at stride 4:

   | type | what |
   |---|---|
   | 1-7 | the ordinary colours: Redium, Greenium, Bluium, Cyanium, Purplium, Yellowium, Pinkium |
   | 8 | **Flashium** - the wildcard, with **no sprite of its own**: its table slot is *rewritten* ~4x/sec, cycling types 1..7 in order (measured, with a static control). The cell value stays 8, which is why it always clears with `FFADE` |
   | 9 | AntiMatter - destroys the surrounding atoms |
   | 10 | Bonus - travels fast, turns into Flashium when caught, grants a bonus drop |
   | 11 | Xenon - inert, will not react with any colour |
   | 12 | Multiplier - tops the test tube up to five with `Random(8)+1`, so Flashium is in the fill (`1000:08d2`) |
   | 13 | Evil Multiplier - fills the test tube with Xenons |
   | 14 | Convertor - turns the atoms it lands on into Xenons |
   | 15 | Blocker - fills the beaker column it lands in with Xenons |
   | 16 | Filler - parks a type 17 in the tube's bottom slot and discards itself (`1000:0b55`) |
   | 17 | `FILLBALL`, **settled**: `1000:472a` refuses to tip one, so the Filler's slot is dead for the rest of the session. No capacity variable exists |
   | 18 | the **Mischief Crystal** - starts in the beaker as contamination and **teleports** between cells; removed with **AntiMatter**, never by matching. `CRFADE` is its *teleport* animation (forward out, reverse back), which is why its static sprite is `CRFADE1` |
   | 19 | `MYSTBALL` - **not a ball at all**: a rendering state, substituted for the real sprite while a hidden-atom wave is running (`-0x189`, six sites in `1000:3a67`) |

   **`GLDFADE` is an unused animation** - see `docs/reversing-notes.md`. The
   Bonus becomes a Flashium in the catch (`1000:07f7`), the pre-fill and the
   morph only produce 1..7, and `1000:2790` turns a settled one into Xenon, so
   no type-10 cell can ever be cleared and the six `GLDFADE` sprites are never
   drawn. The SOUND of that name is used, at the catch - and it is the only
   one of the eleven fade sounds whose internal name field is empty.

   A fade family is an **effect animation**, not a "can be cleared" marker: the
   seven colours are match-clears, `FFADE` the wildcard's, `GLDFADE` the Bonus,
   `AFADE` the **AntiMatter blast** applied to everything caught in it, and
   `CRFADE` the crystal's **teleport**. Types 11-17 and 19 need no effect of
   their own.
   Cell values are therefore **not** limited to 1..7 - the table runs to 19.
   Behaviours for 8-16 come from published descriptions whose ordering matches
   the measured type order exactly; 17 is still unknown. **19 is solved** and is
   not a ball - see the row above.

---

## Next

### KNOWN ISSUE: the exit screen flickers, cause not found

**Open, reproducible, and given up on for now** after six attempts. The port's
own bug - it is in `runExitScreen` in `main.cpp`, not in anything decompiled -
and it is cosmetic: it affects only the shareware sign-off screen and nothing
in the game.

**The symptom, as the player describes it and it is the best evidence there
is:** about **twice**, shortly after the screen appears, **the first quarter of
the text elements invert colours for a split second**. It then settles and
stays correct. Reproduces windowed and fullscreen. It is a REGION, it is
TRANSIENT, and it happens during the hold rather than during the fade.

**What is ruled out**, each tried and each failing to fix it:

1. *Presenting too rarely* - the loop originally presented only on the cursor
   blink. Presenting every retrace changed nothing.
2. *Swap chain convergence* - priming with four back-to-back presents so every
   buffer holds the final frame changed nothing. (That loop was removed; it was
   uploading a megabyte four times with no delay and may have been making it
   worse.)
3. *Unthrottled presenting* - **a real bug, found and fixed, and not this.**
   `SDL_RenderSetVSync(1)` returns 0 and sets the PRESENTVSYNC flag on this
   Wayland/OpenGL target **without actually syncing** - measured, 60 presents in
   459 ms. The loop believed the return value and skipped its delay. Fixed by
   throttling unconditionally; the flicker survived.
4. *Mismatched texture access* - the texture was `STREAMING` updated with
   `SDL_UpdateTexture`, which is the pairing neither mode is for, and at
   640x400 RGBA it is a megabyte racing the draw. Changed to `STATIC` with
   uploads only when content changes. The flicker survived that too.

**What has NOT been tried**, in the order worth trying:

* **run it under X11** - `SDL_VIDEODRIVER=x11 ./build/tubes-port --exit-screen`.
  If it does not happen there, it is the Wayland backend and everything above
  was looking in the wrong layer. This is the cheapest decisive test and should
  be first;
* **the software renderer**, `SDL_RENDER_DRIVER=software`, for the same reason;
* `SDL_LockTexture` / `SDL_UnlockTexture` with a `STREAMING` texture - the other
  correct pairing, and the one not yet used;
* **actually capturing it.** Every diagnosis so far has been reasoning plus the
  player's description. A recording would settle it in one look, and could not
  be made: the session is Wayland, `grim` and `wf-recorder` are not installed,
  and `ffmpeg -f x11grab` on `:1` captures black.

**The method note, which is the part worth keeping.** Four causes were inferred
before giving up, and *every one of them was reasoned from a true statement
about the code* - no `PRESENTVSYNC` flag at creation, streaming textures not
guaranteeing persistence between frames, swap chains needing to converge,
`UpdateTexture` being the wrong call for `STREAMING`. All four are true. None
was the cause. Reading harder produced a new wrong answer each time, and the
one thing that materially advanced the diagnosis was the player describing the
artifact precisely - *region, inverted, twice, during the hold not the fade* -
which falsified three of the four in one sentence.

So: **this is a case for a capture, not for another theory.** Do not open it
again without a recording or an X11 comparison.

### Player-reported differences, still open

Observed by the player against the original, so **real** - but not yet
decompiled, and deliberately not guessed at. Presentation only; none of them
changes a rule.

1. ~~**Screens cross-fade.**~~ **DONE - and it was where the player said it
   would be.** `23e7:0097` fades in, `23e7:00ce` fades out, both in the
   GRAPHICS unit, and every screen in the game calls them - which is why three
   scans of the game segments found nothing. 41 DAC uploads one retrace apart
   (`[DS:0x0ce6] = 40` steps, `23e7:003d` waits for the retrace), so 0.586 s,
   matching the half second the player timed. `docs/reversing-notes.md` has
   the ramp arithmetic. Ported with `--fade-steps N` to shorten or disable it.
   Still open, and marked as inferred rather than read: `[DS:0x22de]`, a
   no-argument music-driver call that appears once in each screen function
   immediately before the palette fade-out and nowhere else - the music half
   of the same transition. It is NOT `StopMusic`, which is `[DS:0x22da]`.
2. **The Wave Complete banner ends when its music does. DONE** - the something
   in `1000:5e0b`'s `repeat until KeyPressed or <something>` is
   `[DS:0x22ce] = $ff`, the driver's "the song has been round once" query, and
   the sequencer already tracked it as `looped_` (`cs:0x32`). Ported along with
   the two `Delay($28)` holds either side of the wait, which are what stop the
   keypress that ended the wave from dismissing the banner it caused - a player
   reported wave 1's banner never appearing at all.
3. **`SELECT.SFX` plays on SELECTING a menu item**, not on moving between them.
   Implemented from the player's account; the call sites in `1b2e:4d80` are
   near calls whose targets Ghidra renders with the 0x10000 bias, so the scan
   for a play vector cannot see them. It may be used elsewhere too.

### 0. START HERE: the menu items that are reachable and inert

**The next step is section 4B below.** The session loop is closed (4A, done)
and so are High Scores and Save/Load, screens and all - F2 saves and Continue
Saved Game loads a real DOS save.

**The menu is still not finished**, and it is what stands between here and a
player sitting down with the whole program. Four of the eight items go
nowhere: **Game Options** (both toggles AND Redefine Input Device - the player
has flagged key/gamepad binding as important), **Instructions**, **View Demo**
and **Credits**. 4B lists each with its address and what it needs.

`1000:3a67` itself is **done** - this section's original task - and the method
below is what did it. Keep it.

The project is in its **transliteration phase**. `CLAUDE.md` carries the prime
directive: every gameplay rule in `src/` must come from decompiled Pascal, not
from watching the game. Black-box recreation was tried for a session and was
lossy in ways invisible from the inside - a scoring rule fitted to two points
that was simply wrong, a wildcard rule wrong until a player described it, and
an "ambiguity in the original" that was really a bug in our own matcher.

**The measure of progress is now a number.** `~/Dev/tubes-tooling/` holds a
pixel-diff harness (see `docs/debug-rig.md`):

    python3 capture_frame.py frames 2      # original, PAUSED, + its state
    python3 diff_frame.py frames/frame00.state

It captures the original with the game's own **Pause** key - halting via GDB
blocks the screendump - puts the port into that exact state with
`tubes-port --render-state`, and diffs. Current reading: **4.6% of structural
pixels**. Drive that down.

Two calibrations are already in it and must not be removed: the backdrop is
excluded (random, animated), and greys are compared with a tolerance of 6
because DOSBox expands the DAC with `v<<2` and the port with `v*255/63`.
Without the tolerance the harness reports 34% and sends you hunting a palette
bug that does not exist.

**Items 1 to 5 of the old list are done**, and four of the five were not what
they looked like. The test tube needed no sweep and the arcs were not missing
pieces: both were the `.CSP` placement offset, which `screen.h` had documented
as "meaningless as a placement offset" and dropped. The corner sprites needed
no selection rule, because nothing is painted over an atom - `2321:0874`
re-stamps GAMEFG. Descent velocity has no assignment at all. Only the spawn
interval was what it claimed to be. See `docs/reversing-notes.md`.

**In priority order:**

1. **DONE: the demo replay matches, end to end.** `--demo-trace` replays
   `DEMO.SCR` headless; `--demo-csv FILE` writes the per-frame state the rig
   diffs against; `--play-demo` runs it through the live loop.

   The port now reproduces the original's whole attract-mode session and stops on
   **the same byte the original does**:

   | event | original | port |
   |---|---|---|
   | a miss, score 1,000 | byte 693 | byte 693 |
   | a Bonus caught, +1 drop | byte 2,188 | byte 2,184 |
   | a miss | byte 2,276 | byte 2,274 |
   | a miss | byte 2,320 | byte 2,319 |
   | a miss, drops now 0 | byte 2,352 | byte 2,353 |
   | the drops byte underflows: game over | byte 2,367 | **byte 2,367** |

   Final score 13,000 on both sides, 71 spawns, agreement on which frame each
   byte is consumed at with a median difference of -0.2 frames over 752 byte
   counts, and every spawn within 4.7 frames.

   Six rules came out of it, all from decompiled code, all in
   `docs/reversing-notes.md`:

   * the demo runs at **Tubes 301** (`1000:a483` on `DS:0x1d4f`, set to 2 at
     `1000:b272`);
   * the first dispense is on **frame zero** - `1000:3be0` seeds the countdown
     with 1;
   * the Down/B boost is **inside** the tube's `state = 0` guard and **before**
     the Left/Right handler moves the stop (`1000:4534`);
   * a `.SCR` is **one byte per IDLE frame**, because `1000:44f0` skips the input
     read while the tube is busy and the demo reader is that read;
   * the catch tests the tube's **actual x** (`rec.x = tube.x + 3`), not its stop;
   * the **endurance ramp** `1000:235c` - five frames off the dispense interval
     per ten runs, with the velocity climbing 0x20 - and it is a **loop, once per
     RUN**, `1000:240a`/`240d`.

   **Watch for "once per event".** Two rules here count once per SEED rather than
   per event - the score award and the ramp - and assuming otherwise has now been
   wrong twice. When a count drives something, check whether the original wraps
   it in a loop.

   **The oracle is now a regression test worth running.** Any change to the
   rules should leave `--demo-trace` ending on byte 2,367 with score 13,000. If
   it does not, the change is wrong or a new rule has been found.

   Rig instruments, all built: `exp23_demo_index.py` (the demo reader's own
   stream index at **`0x24c2e`**, plus atoms, tube, beaker, drops and score),
   `exp19_seed_count.py` (`RandSeed` through the LCG orbit for an exact `Random`
   call count), `exp21_tube_track.py`, `exp22_input_vectors.py`. Anchor every run
   on `1000:6008` (linear `0xE248`) and set breakpoints only while HALTED.
   **Fit slope AND intercept** when turning the guest's wall clock into frames -
   fitting through the origin manufactures a phantom drift.

2. **Wave structure - now DECOMPILED, and next to transliterate.** The whole
   objective system came apart in one pass; `docs/reversing-notes.md` has it
   under "Wave mode, decompiled". In short:

   * `1000:86b8` is the briefing screen and **its body is the wave table** - a
     75-arm dispatch on the wave number, one arm per wave, calling one of **25
     objective routines** between `1000:62f1` and `1000:8581`.
   * The parameters are **six counters seeded as literals** at `1000:a4cd` -
     `3, 30, 2, 0, 3, 8`, which is exactly the list `PLAN.md` used to carry as
     "difficulty seeds, variables not yet named" - stepped by the progression
     at `1000:a616`: the dispense interval tightens one frame a wave with a
     12-frame refund every fifteenth, and every twentieth wave adds a chain to
     both chain targets, ten to the atom target and one marked atom.
   * `1000:192f` is the scoring hook, and modes 4/5/6 bypass it - mode 4 counts
     atoms *dispensed*, at `1000:4b31`.
   * The reading reproduces **all nine** briefings the old level-warp sweep
     sampled, including the wave 10 / wave 15 coincidence, and every count in
     them.

   What is left is the port work, which is large and mostly *around* the rules:
   briefings, the stats blackboard, the Continue screen, the Task Display HUD,
   and the four wave-setup helpers (`1000:0000`, `1000:0236`, `1000:035e`, and
   the morph body at `1000:4bf6`) that are named but not yet read.

**What is already transliterated and should not be re-derived:** the atom
router `1000:0f80` (fixed-point, states 3/5/6/7, the two arc offset tables),
the four DGROUP geometry tables, the network topology, the ball table and type
numbering, the drops model, scoring by chain orientation, the whole per-frame
draw order with its six atom slots and the GAMEFG stamp, `.CSP` placement
offsets, the spawn (period, column, type distribution), the difficulty seeds,
the test tube's slide and speed boost, the tipping animation and its three
sprites, records 7..12 and the fall into the beaker, and the text renderer
with the HUD it draws.

### 1. The dispenser and test tube mechanic

**How it actually works** (described by the user from play, and corroborated
by the binary):

- Atoms enter at the **bottom right**, travel **up** the right-hand side, arc
  over the **top** of the screen, and come back **down** - tracing the tube
  artwork rather than falling straight.
- The player slides the test tube along a horizontal rail and catches them.
  The tube **holds several atoms stacked**.
- **Button A** tips the tube, dumping **one** atom at a time into the beaker.
- **Button B** speeds an atom along, sucking it out of the tube faster.

The binary corroborates the entry point exactly. The 12 records of 28 bytes
initialise to **(303, 186)** - x=303 is off the right of the play area, which
ends at 245. **Measured:** that is the initial value of a *never-used slot*, not
a parked state and not a recycle target - over 382 frames of play, **no** record
ever transitioned into or out of it. The twelve records are a pool of slots,
reused by overwriting x/y directly; the large positional jumps in the per-frame
data are allocation, not motion. A lost atom's record simply keeps its final
position until the slot is reallocated.

**Also measured, and correcting the line above:** a missed atom is **lost**, not
deposited in the beaker. That is a "drop", and it is what the drop allowance
counts. The beaker fills *only* by catching atoms in the test tube and tipping
them in with Button A - nothing reaches it without passing through the tube.

**Implemented so far:** the tube stacks, A dumps one at a time, B accelerates.
Capacity 5/3/2 by difficulty, inferred from the `TESTUBE1/2/3` sprite heights
of 65/42/27 at the 13px row pitch - which sprite goes with which difficulty is
not proven.

**The atom record is solved.** From the one record-indexed draw site:

        push word es:[di]        ; x      - record +0
        push word es:[di+2]      ; y      - record +2
        mov  al,  es:[di+0xb]    ; colour - record +0x0b
        call 1321:0905           ; Draw(x, y, sprite)
        ...
        mov  es:[di+0x14], dx    ; saved x, one slot per video page

So each of the 12 records is a free-moving sprite carrying its own position,
plus saved positions per page for dirty-rect erase (the page index lives at
`ds:0x2376`). 28 bytes: x, y, colour, and two saved pairs.

**There IS a waypoint table** - at `DS:0x26`, six words, immediately after
the column-x table:

        targets: 104, 122, 140, 158, 176, 194     (pitch 18)
        columns: 107, 125, 143, 161, 179, 197     (pitch 18)

The targets are the column positions minus 3, the same -3 the test tube is
drawn at. So the six waypoints are the six column stops.

A first scan of DGROUP reported "no waypoint table". That was wrong: the scan
required smooth runs of **8 or more** words and this table has **6**. The
filter excluded the answer. Worth remembering - a negative result from a
threshold search is only as good as the threshold.

### The atom movement state machine

From `1000:3a67` around `0x67bd`:

        cmp  BYTE es:[di+0x1e], 6      ; waypoint index
        mov  BYTE es:[di+0x04], 2      ; direction = right
        inc  BYTE es:[di+0x1e]         ; advance waypoint
        mov  ax, [di+0x24]             ; target = waypointTable[index]
        mov  es:[di+0x1f], ax
        ...
        cmp  al, 1                     ; direction 1 = left
        sub  WORD es:[di], 6           ;   x -= 6
        cmp  ax, es:[di+0x1f]          ;   reached target?
        mov  BYTE es:[di+0x04], 0      ;   yes: snap to target, stop
        cmp  al, 2                     ; direction 2 = right
        add  WORD es:[di], 6           ;   x += 6

So atoms step **6 pixels at a time** toward a target column, and y snaps
between two lanes: **187** at the bottom where they enter, and **68** at the
top, just above the test tube at 69.

### Record layout (28 bytes)

| offset | field |
|---|---|
| +0x00 | x |
| +0x02 | y |
| +0x04 | direction: 0 stopped, 1 left, 2 right |
| +0x0b | colour / sprite index - **confirmed live** at array base `0x241a4` (the earlier doubt was a 6-byte base error, not a layout error) |
| +0x14, +0x16 | saved x, one per video page (dirty-rect erase) |
| +0x18, +0x1a | saved y, one per video page |

**Corrected:** `+0x1e` and `+0x1f` are *not* atom fields - `0x1f` = 31 does not
fit in 28 bytes. They belong to the **test tube** struct and were merged in by
mistake. The atom record tops out at `+0x1b`, which fits exactly.

### Two structures - and the second one is the TEST TUBE, not an atom

Both reached through the Pascal static link from `9e53`'s frame:

| base | shape | what |
|---|---|---|
| `parent - 0x163` | 12 x 28 bytes | the atoms |
| `parent - 0x16a` | one struct | **the player's test tube** - now **located live at `0x245d0`**, but *not* where this implies: it sits `0x432` bytes **above** the atom array, not 7 bytes below. The field layout is confirmed; the parent-relative relationship is wrong |

The single struct was read for most of a session as "the atom currently
travelling the arc". It is not. At `0x66f0` it only acts when its direction is
0, then calls the input driver (`ds:0x2352`, `ds:0x2356`) and branches on the
button bits. It is player-controlled.

Five things corroborate it, none of which fit a travelling atom:

- Its waypoint targets are 104, 122, 140, 158, 176, 194 - the six column x's
  **minus 3**, which is exactly the offset the test tube is drawn at.
- It moves 6 pixels per frame toward a target: a tube sliding smoothly between
  columns, not snapping.
- Button A (`0x10`) puts it into state 3.
- State 3 runs a 4-phase counter that indexes a **sprite pointer table**
  (`phase << 2` at `0x7b2f`) - an animation, and A is the dump action.
- Its y values are 68 and 187, and the tube hangs at 69.

### The test tube's state machine - **confirmed live**

Struct at **`0x245d0`**; x `+0x00`, state `+0x04`, waypoint index `+0x1e`,
target x `+0x1f`, all bytes. Driving each input and reading `+0x04`:

| `+0x04` | meaning | observed |
|---|---|---|
| 0 | parked at a column, accepting input | idle |
| 1 | sliding left, `x -= 6` per frame | hold Left |
| 2 | sliding right, `x += 6` per frame | hold Right |
| 3 | tipping: 4-frame animation | press Button A |

x was seen stepping 104, 110, 116, ... 194 - the 6 px/frame rail speed - and the
targets are exactly 104, 122, 140, 158, 176, 194 with the index tracking 1..6.
The **live score** is a `u32` at `0x245e7`.

Left and right also step the waypoint index at `+0x1e` (`dec`/`inc`), bounded
at 6, so the tube stops on column centres.

### `TESTUBE1/2/3` are tipping frames, not capacities

This follows directly, and **corrects an earlier inference**. The sprites are
22x65, 20x42 and 20x27 - a tube foreshortening as it tips over, not three
capacities for three difficulties. The capacity 5/3/2 currently in
`src/game.cpp` is therefore unfounded and should be treated as a placeholder.

### Atom speed - SOLVED, and none of the reasoning below was right

The velocity field is `+0x09` of the atom record and has exactly three writers
in the whole procedure: the spawn (the difficulty's 2/3/4 px/frame), the Down/B
boost (`0x480`, nine px/frame, applied to one atom and never reset), and the
tipping animation. It is a per-record field after all, written whole at spawn -
which is why the "the x/y are never incremented, so speed must be a divisor on
a shared counter" reasoning below went nowhere. Kept as a record of the wrong
turn; skip to `docs/reversing-notes.md` for the answer.

Ruled out so far:

- **Not in the test-tube struct.** Travel is a flat 6 px/frame with the
  divider threshold and 4-phase limit both hardcoded, and the struct is the
  tube, not an atom.
- **Not in a sibling function.** All 18 of the `*28` atom-array accesses are
  in `3a67`; `86b8`, `8da5`, `8c38`, `9499`, `9111` and `96db` contain none.
- **The atom array's x/y are never incremented.** They are written whole by
  `mov` - the spawn at `0x6b94` sets `x = ax` and `y = 187`. So atoms are
  positioned from other state each frame rather than stepped.

That last point is the useful one: whatever drives atom position is computed,
not accumulated, so there may be no "speed field" in the record at all - the
speed could be a divisor applied to a shared frame counter.

**A lead from play: `GOLDBALL` travels the tube *very fast*.** **SOLVED.** The
last thing `1000:0f80` does to every atom, every frame, is reload its velocity:
`if type = 10 then 0x480 else sessionBase`. So the Bonus atom is fast by type,
and the Down/B boost written before the router lasts exactly one frame - the
player has to hold it.

**Recommended change of technique.** Reading one 9382-byte Pascal procedure
with nested frames has produced five self-corrections in a single session, and
every one came from a tool being wrong rather than the binary being obscure:
a scan threshold set too high, a correlation window too narrow, a regex that
silently excluded negative displacements, two structures assumed to share a
base. Static disassembly is past the point of diminishing returns here.

The better tool is the one already on this machine: **DOSBox-X's debugger**.
Set a memory breakpoint on the atom array and watch what writes it. That
identifies the code directly instead of inferring it, and it also settles
speed by observation - run the game, hold B, and watch the rate change.

**Do not** assume per-cell tile routing - the frame update contains no
arithmetic on the 13px row pitch outside the settled-grid draw.

#### Static furniture, from literal draw coordinates in `1000:3a67`

84 of the 126 sprite draws use literal coordinates:

| y | x positions | what |
|---|---|---|
| 13 | 58, 107, 197, 246 | upper tube arcs |
| 26 | 34, 58, 107, 125, 179, 197, 246, 270 | lower tube arcs |
| 134 | 103 | the beaker |
| 135 | 186 | unidentified |

All symmetric about screen centre 160 once the 16px sprite width is added.
The beaker at (103, 134) independently confirms the placement derived from the
grid geometry.

The test tube is 65 tall and its mouth meets the top of the beaker, so it
hangs at **y = 134 - 65 = 69**.

### 2. Presentation

Cheap and high-impact once the mechanic is settled.

- 66 fade sprites: 11 families x 6 frames. **The mapping is now measured** - see
  the 19-type sprite table in `docs/reversing-notes.md`. Indexed by ball type:
  types 1-7 the colours, 8 Flashium (`FFADE`), 9 AntiMatter, 10 Bonus,
  18 the crystal. Types 11-17 and 19 have **null** fade pointers, so they are
  never cleared by matching.
- `.SFX` through SDL audio, mixed alongside the OPL output. **All 24 are now
  attributed**: 11 match/clear sounds (one per fade family), 6 gameplay events
  (`DROP` = a miss, `HITATOM`, `HITGLASS`, `SLIDE`, `SWITCH`, `SELECT`), 4
  cutscene (`BUBBLE` - the beaker foaming in the intro - `CLAP`, `NOOOO`,
  `WHATTHE`) and 3 splash (`WOOSH`, `LIGHTN`, `ABSMAGIC`). The match sounds are
  already mapped: eleven `.SFX` share the exact names of the eleven fade sprite
  families (`RFADE`, ... `FFADE`, `AFADE`, `GLDFADE`, `CRFADE`), so there is one
  per family. Reported from play: the sound is chosen by the colour the **stack**
  matched as, while each ball's fade animation follows its **own** type - so a
  mixed chain shows mixed animations under a single sound.
- HUD: `Chains` at top left and `Drops` at top right with two counters between.
  A "drop" is a **missed** ball - the `DROP` sound plays on a miss - so the
  measured `9 / 6 / 3` drop limits are the miss allowance per difficulty, which
  is what `Tubes 101 / 201 / 301` selects. Which name maps to which limit is not
  yet confirmed.
- Fonts are decoded but never drawn

### 3. Game rules

- **Scoring is solved**, from the in-game Instructions (`docs/reversing-notes.md`):
  vertical chain **250**, horizontal **500**, diagonal **1000** (two diagonals, so
  "4 chains"); chains = atoms - 2 (3 atoms = 1 chain, 4 = 2, 5 = 3); forming
  multiple chains at once applies a **chain bonus multiplier**; a Bonus atom adds
  **1000** to the Bonus Jackpot and awards it. `kScorePerAtom` and `kChainBonus`
  in `src/game.cpp` can be replaced with real values.
- **Atom speed is solved.** "Press Button B **or Down** to increase the speed of
  any atoms in the tube **directly above the test tube**." So the boost is
  positional - gated on the atom's column matching the tube's - and both B and
  Down trigger it. That also reinterprets the `.SCR` demo: its long runs of bit
  `0x02` are the player holding **Down to speed atoms**, not "dropping faster".
  `GOLDBALL` is additionally fast by type.
- **The test tube is a LIFO stack** - atoms leave from the top, so speeding a
  source tube is how the player controls which atom ends up on top. Capacity is a
  flat **5**; `FILLBALL` permanently adds an occupying atom to the bottom.
- **Lose condition:** dropping more atoms than the difficulty allows, in both
  modes.
- the remaining unknowns: wave objectives, and the difficulty-to-drop-limit
  pairing
- the difficulty progression - seeds `3, 30, 2, 0, 3, 8` plus globals 50 and
  25, stepping every 15 and every 20 levels, with level bands at
  30 / 60 / 75 / 90 / 95 / 101. Variables not yet named; trace them from
  `9e53` into `3a67` through the Pascal static link. **The endurance half of
  this is now done** - `1000:235c` takes five frames off the dispense interval
  and adds 0x20 to the velocity every ten MATCHES; see
  `docs/reversing-notes.md`. What is still unread is the WAVE progression at
  `1000:a616` / `a630` / `a646`, which moves the same variables per level.
- **wave definitions.** A wave briefing carries an objective ("live through 30
  atoms") and often a **modifier** - a disabled element that still spawns but
  cannot be cleared, atoms hidden until they leave a tube, beaker atoms morphing
  on a timer, a pre-filled beaker. None of these exist in the port.
- **drops are a persistent pool, not part of the wave definition** - now proven,
  with each mechanism measured on its own. The live counter is a u8 at
  **`0x245bc`**, confirmed against the HUD (memory read 8 while the display read
  `8 Drops`). A miss decrements it; a **Bonus atom increments it**; and
  **clearing a wave leaves it untouched** - it read 8 on both sides of the wave
  52/53 boundary, and wave 53's briefing then announced "8 drops allocated".
  That last point is the confirming test the notes had queued, delivered by play
  rather than by editing the save: the briefing echoes the *current pool*, so the
  fifty screenshots read 11 only because the sweep always loaded a save holding
  11 at `TUBES.SAV 0x207`. The Bonus atom is therefore an extra life - the only
  known way to replenish a resource that otherwise only decreases.
  **A new game seeds the counter from the difficulty** - Tubes 101/201/301 give
  9/6/3, confirmed by playing all three - which finally reconciles the binary's
  measured `9/6/3` with the 11 seen in saves: they were never competing claims,
  one is the seed and the other is a run that collected Bonuses. There is **no
  cap** (a wave 6 save started at 12), so the port should treat drops as a plain
  byte counter seeded once per game.
- special atoms; Endurance vs Wave mode selection sits under a Game Mode menu,
  and saved games are filtered by mode
- save/load. `TUBES.SAV` is 960 bytes and **structurally decoded**: two `0x1e0`
  banks, one per game mode, each holding five `0x50` slots plus a trailer.
  Per-slot fields, confirmed against the running game: player name at `+0x00`
  (Pascal ShortString), score u32 at `+0x1f`, wave at `+0x26`, **drops remaining
  at `+0x27`**, atom target at `+0x2d`. Bank 0 is Endurance, bank 1 is Wave mode,
  which is why Endurance listed five `(UNAVAILABLE)` entries for a save Wave Mode
  showed at once. The stride also explains the two previously unaccounted bytes:
  `0x1bb` and `0x39b` are the same `+0x2b` field of each bank's trailer.
  There is no checksum, so editing a slot's wave byte warps to any wave - but
  note that is `bank + slot*0x50 + 0x26`, and the familiar `0x206` is merely
  bank 1 slot 0. See `docs/reversing-notes.md`.

### 4. The other screens - and the order to do them in

The title screen and menu are **done**. What remains, ordered by how much it
closes the loop for a player rather than by size:

**A. Close the session loop. DONE** - `src/session.{h,cpp}`, commit `99b1c48`.
The wave loop, the three end-of-session banners, the stats screen
`1000:8da5`, the Continue screen `1000:8c38`, and `1000:2dd0`'s in-game keys
(ESC abort, F5 pause, F3/F4 music and sound). A game that ends now returns to
the title. Two things in it are **marked as stand-ins, not derived**:

- the stats screen's background - `2321:068d` blits a held image to `(0, 12)`,
  not `GAMEBG` at the origin, and nothing decompiled writes `DS:0x2058`;
- ~~the Continue countdown's tick length~~ **SETTLED**: `23e7:0024` is
  `Delay(n)` in vertical retraces, so `param * 7` iterations of `Delay(10)` is
  `param` seconds exactly - one count a second, which is what the port had
  assumed. The stats background still wants a capture of the original;
  `--screenshot-after N` plus `--auto-advance` is how the port side is
  captured.

Still open from it: `1000:2dd0`'s **F1 Help** body. Its screen is not
decompiled and the key is inert - the only key in the game that does nothing.
F2 is done, both ways in: during play, and from the abort banner's own offer.

**B. The menu items that currently do nothing.** Each is reachable and inert,
which is worse than absent - it looks broken. **The menu is NOT finished**; four
of the eight items still go nowhere.

DONE:

- **Credits** - `1b2e:411b`, four pages, sharing the Instructions screen and
  its key rules. Its own third page is where the game tells you it was written
  in Borland Pascal 7 and uses planar 320x200x256 - two things this project
  assumed from the first session and never had confirmed.
- **Instructions** - `1b2e:2d63`, all 21 slides. The slide data is EXTRACTED,
  not transcribed: `tools/gen_instructions.py` reads the disassembly and emits
  `src/instructions.cpp`, because 152 strings typed by hand would be 152
  chances to mistype a line of the game's own documentation. A captured slide
  diffs against the original at **8 pixels of 64,000**, all of them the
  professor's pointer mid-wave.
- **View Demo / attract mode** - `1000:b264` and `1000:b287` do the same three
  stores, so the menu item and the 720-frame timeout are one path: mode 0, new
  game, difficulty 2, replaying `DEMO.SCR`. Both work, and the loop turns over
  on its own. TWO PIECES ARE MISSING and are marked at the call site rather
  than faked: the timeout runs the blackboard cutscene `1b2e:1651` FIRST and
  skips the demo if it returns 2, and `1000:a690` calls `1000:9338` at the end
  of an attract session - a routine that runs in NO other mode and is unread.
- **Game Options** - `src/input.{h,cpp}`. The two toggles are the game's own
  flags (`DS:0x215f`, `DS:0x215e`) and now persist. Redefine Input Device is
  **deliberately re-implemented rather than transliterated**: the original
  picks a DRIVER because DOS gave it no abstraction, and SDL is that
  abstraction, so the port rebinds the six controls instead - keyboard and
  gamepad at once, which the original could not do. `SETUP.CFG` is NOT written;
  it is the DOS install's hardware config and belongs to `SETUP.EXE`. See
  input.h for the reasoning.
- **High scores** - `src/hiscore.{h,cpp}`, the entry screen `1000:96db` and the
  viewer `1b2e:61b6`. `TUBES.HSC` round-trips byte-exact against a real file,
  and both viewer pages diff against the original at **0 pixels of 64,000**,
  unmasked. The entry screen has no capture yet; it needs a qualifying score.
- **Save / load**, both halves - `src/save.{h,cpp}`, Continue Saved Game, and
  F2. `TUBES.SAV` is fully decoded and re-encodes byte-exact; the slot list
  diffs at 252 of 64,000 pixels (all star-rotation phase) and the F2 screen
  carries 99% of the original's ink at the same coordinates.

**Every menu item now does something.** What is left of the original program
is section C's two splashes and section D's blackboard cutscene.

Also inert: **F1 help** in-game, whose body is in `1b2e:2d63` alongside the
slideshow.

**C. The two splashes,** `21d5:007b` and `2178:00eb` - Software Creations and
Absolute Magic. `1b2e:11b0` runs both, and a skip in the first cancels the
second: `if k <> 1 and k <> 2 then AbsoluteMagic`, where 1 is Enter/Space and
2 is ESC.

**Software Creations is DONE.** `SOFT.PAL`, `SOFT.GFX` and the 23-frame
`SOFT.ANM`, at the original's own pacing - fade in, `Delay(10)`, the animation
at three retraces a frame, seven holds of ten retraces, fade out. Its capture
diffs against the Python reference at **0 pixels of 64,000**. `--splash N`
captures the Nth animation frame, `--no-splash` skips both.

**One bug the player caught, worth keeping in view.** The "Absolute Magic"
lettering was corrupted, because `AMWRITE.GFX` carries the `0xE5` prefix and
this project had that prefix written up as "marks chunky storage". It does
not: it is a header byte both blitters skip, and the LAYOUT is decided by
which one draws the file. `AMWRITE.GFX` goes through `2321:0948`, the Mode X
routine, so it is planar. `176 * 25 + 5` fits the file either way, so no size
check could have found it - only looking at it. `decodeGfx` now takes a
`GfxLayout`.

**Absolute Magic is DONE too.** `2178:00eb`, whole: the built backdrop
(`CLOUD.GFX` at the top and the same band rotated 180 degrees at the bottom,
which is what `2178:0000` writing it backwards from offset 63,999 produces),
`AMTHEME.MUS`, the six-frame logo zoom at `SetFrameRate(9)`, five lightning
strikes at their five literal positions at `SetFrameRate(4)` with the white
flash, and the writing. The white flash was a guess when it was written up an
hour earlier and is now read: the splash `FillChar`s a palette with 63 and
uploads it either side of the page flip. `--splash2 N` captures any step.

**On skipping - one deliberate departure, agreed with the player.** The
original reads the keyboard only in the tail loop of each splash, through a
BUFFERED read (`ClearKeyBuffer` immediately after the wait is the tell), so a
press during the animation counted but not until the next poll - which left
ESC feeling dead for up to a second. **ESC now cuts in at once**: every fade
step, animation frame and hold polls, and a press ends the screen there. The
fade-OUT still runs to completion, because the DAC has to reach black or the
next screen starts up lit. Nothing else changes - the pacing, the order and
every literal are the original's.

This is the second departure in the whole port, after control bindings, and
like that one it was asked for explicitly rather than assumed.

**D. The slideshow and the cutscene, LAST.** These are two different things and
were previously conflated in this file:

| | Function | Size | What it is |
|---|---|---|---|
| Instructions slideshow | `1b2e:2d63` | 4,510 | prev-slide / next-slide **slides**, `TESTUBE1.CSP`, `TESTUBES.CSP` |
| Blackboard cutscene | `1b2e:1651` | 2,323 | the teacher sequence, `WRITE0..9.GFX`, `EXPLOD1..16.GFX` |

**The cutscene's TEXT, layout and animation player are all decoded** - see
`docs/reversing-notes.md`, "The opening cutscene". Dr. Lanny B. Brilliant is
named there, so are the eight elements, and each element has its own atom
drawn beside its name.

`1b2e:0f46` is now read too, and it is not what its name suggested: it is a
**two-track** animation player, running two sequences at once, each with its
own frame list, position, size, frame count and sound cue. Its whole parameter
map is in the notes. Two frame counts are magic - 25 on track A and 16 on
track B mean "play once and stop" rather than loop.

**All five call sites are read too**, and the notes carry them as a table:
five pages of 2, 18, 4, 12 and 10 seconds, Lanny writing throughout, an atom
cycling the seven colours beside the element names, and the explosion looping
four frames and then running all sixteen. The fifth call has both tracks idle -
it is a plain hold that still polls for a key.

**The cutscene is DONE.** The slot map came out mechanically -
`tools/gen_cutscene.py`, the third screen through that generator after the
Instructions and the Credits - and the sizes prove it: slots 0..9 are the
28x41 `WRITE1..5` and slots 10..25 the 28x66 `WRITE6..9`, which is exactly
what the two page groups pass.

`1000:b224` runs it ONCE, after the splashes and immediately before the title
screen is first shown; the main loop's `JMP 1000:b236` returns to the title
call, not to this. Its music is `CLASS.MUS`. `--cutscene N` captures any page.

**Captured and diffed against the original.** `grab_cutscene.py` sweeps the
original from boot with a screendump every ~1.4 s - it cannot be paused into,
since it acts only on Enter/Space and ESC - and `diff_cutscene.py` matches
each of the port's five pages to its best capture: **0 pixels of 59,184 on
all five**, with only the two animation rectangles and the cycling atom
masked. `sweep_cutscene_ticks.py` then removes the masks by rendering the port
at every tick of a page: **0 pixels of 64,000, whole screen, on every page**.

The captures also confirm the durations independently - 2, 18, 4, 12 and 10
seconds, measured off the page boundaries in a run captured from its start.

**Enter turns the page; ESC leaves.** `1b2e:112a` onward: Enter or Space sets
the page's countdown to 1, so that PAGE ends and the next begins, and ESC does
the same but also sets the return code to 2, which is what leaves the
cutscene. The port had every key ending the whole thing, which made Enter a
skip button rather than the page-turner the original gives you.

**The last 144 pixels are closed, and the cause was not the pages.** The
residue on the fourth page was written up as "the original holds the union and
the port rebuilds", and the fix was expected to come out of the page
bookkeeping. Reading the pixels' COLOUR settled it in minutes instead: all 144
are colour 0 in the original and the base pose's greys in the port, i.e. the
original had blacked something out and the port let it show through.
`1b2e:0f46` blits every frame OPAQUELY (`2000:389d`), so a 28x66 frame's
transparent bottom rows land as black over the base pose. The frame lists were
loading with index 0 transparent and the figures lived in an overlay that
cannot tell "wrote black" from "wrote nothing". Both fixed; **all five pages
now diff at 0 with nothing masked**.

The page bookkeeping was read as well and is in the notes - it is all real and
none of it was needed.

They are last on purpose. Both are large, neither gates play, and the cutscene
in particular is an animation system (`WRITE0..9` is a *writing* animation)
rather than a screen - so it is the one piece most likely to need machinery
nothing else needs.

### 4.5 FINAL POLISH - the player's list, recorded before it is worked

Five items, given in one go so none of them gets lost. None is a rule; all are
things a player notices and the port currently does not do.

**1. Every screen with an animation should have it. DONE**, and both leads
recorded here pointed at the wrong routine - which is the useful part.

* the **projector screen roll-down** is **`1b2e:0510`**, and it has nothing to
  do with `DS:0x210e`. `1b2e:0a11`'s six-frame animation moves the SLIDE; the
  screen behind it is rolled down by the routine that builds the classroom
  from nothing, 15 frames of `Delay(3)` over a word table at `DS:0xb9c` that
  overshoots its resting height by five and comes back. Nothing gates it.
  What varies is who calls `1b2e:0510`: the Instructions and the Credits
  always, the briefing only on `wave = 1` and not after a Continue, and the
  stats / Continue / ending screens never.
* **Professor Lanny's mouth** is `TALK1..5.GFX` drawn by **`1b2e:0cd1`** - not
  a fourth arm of `1b2e:0656` but a SECOND key wait, which every screen runs
  BEFORE `1b2e:0e37`:

      k := 1b2e:0cd1(bursts);                  { he talks }
      if k = 3 then k := 1b2e:0e37(seconds);   { it timed out - now he waves }

  so he speaks first and gestures only when the talk has run out. The
  parameter counts bursts of `Random(4)+4` mouths, not frames.

  Of the two flags this list asked about: **`DS:0x20c8` is written by nothing**
  except the start-up clear, so `1b2e:0656`'s clap arm is unreachable in the
  shipped build - which is why `CLAP1..3.GFX` are never seen either;
  **`DS:0x20e3` is set by `1000:9499`**, the wave-75 ending, and cleared again
  on the way out. So the jump arm belongs to the one screen nobody has reached.

**2. The joke slide. DONE.** `1b2e:084e` and `FLASH.GFX`, which is 172 x 132 -
the slide rectangle exactly - and draws Lanny holding his coat open over an
"AM" T-shirt. `POINTERT.GFX` is the startled face that goes with it. Gated on
`Random(100) < 5` per slide AND on `DS:0x210f`, so at most once per program
run. `--joke` forces it. This file's own notes had quoted the routine as "a
one-in-twenty easter egg, not ported" without asking what the egg was.

**3. The screen transition fade. DONE.** The player's lead was right in every
particular: it is a mandatory effect, it lives in a graphics unit rather than
in the game, and it is about half a second. `23e7:0097` / `23e7:00ce`, 41 DAC
uploads a retrace apart. See the fade section in `docs/reversing-notes.md`.
Ported, with `--fade-steps N` (default 40, the original's; 0 cuts) since the
player sanctioned speeding it up.

**4. Sound is clipped or truncated. DONE**, and it was BOTH candidates.

`SBSOUND.DRV` has one voice, so the original genuinely cuts a sound off when
the next one starts - faithful, but a truncated `DROP` reads as a defect to
anyone who has not read the driver. And the port was worse than the original:
`Game::pendingSound_` was a single slot, so a second event in the same frame
replaced the first BEFORE it was ever played. The original at least starts it.

Both lifted, as a DEPARTURE agreed with the player - modern SDL mixes as many
streams as you like. `SfxPool` (sfx.h) is four voices and `takeSound` is a
four-deep queue the frame loop drains. Nothing about WHICH sound plays when
changes: every one still fires from the site the original calls `PlaySound`
at. They simply finish.

**5. Ports to other platforms** stay an eventual goal - see `Portability`.

### 5. After the port is faithful: enhancements

Explicitly **not** now, and explicitly **not** in place of the original
behaviour - the point of the port is the original. But once it is faithful,
optional extras are wanted, and the architecture should not preclude them:

- **higher frame rates**, by interpolating between the 16.11 Hz simulation
  frames rather than by running the simulation faster. The fixed step is load
  bearing - every speed is a whole number of pixels per frame - so anything
  here has to be a *render-side* interpolation with the simulation untouched.
- ~~**a Graphics Options screen**~~ - **DONE.** The player's request, and it
  went in on the rebinding screen's terms: a screen the port OWNS rather than
  transliterates, for the reason `src/input.h` gives for the input half. The
  original chooses a DRIVER because DOS gave it no abstraction, SDL *is* that
  abstraction; the display is the same story one layer over, since Mode X was
  the only mode the original had and `SETUP.EXE` owned whatever choice existed.

  Five rows, all render-side: **Display** (windowed / fullscreen desktop),
  **Window Size** (Fit, or 1x..6x pinned), **Pixels** (square, or the 4:3 a
  1994 monitor showed), **Vertical Sync**, and **Scanlines**. Every change
  applies and saves the instant it is made. `--graphics` opens the screen for
  capture.

  What it cost, stated plainly because it is the one thing here that is not
  free: **page 6 is now the only menu page the port does not render
  pixel-identically.** A fifth row at the same 26-pixel pitch starts the block
  13 pixels higher. `kMenuPages` still holds the image's own four-item page and
  always will - the port's version is `kOptionsPagePort`, a separate object,
  and `menuPage()` is what every layout and navigation path reads.

  Three things made it cheap and one is a rule that held:

  * `Settings` (`src/input.h`) already persisted through `SDL_GetPrefPath`, so
    the display options ride the same file. It is NOT `SETUP.CFG`, which is the
    DOS install's hardware config and belongs to `SETUP.EXE`;
  * the menu page already existed, so the row went beside `Redefine Input
    Device` rather than onto a key nobody would find;
  * `screen.cpp` is a plain indexed framebuffer and only `main.cpp` and
    `opl.cpp` include SDL, so all of it lives at the platform edge and touches
    nothing that was reverse engineered. The scale and letterbox arithmetic is
    in `input.cpp`, with no SDL in it, which is why the tests can check it;
  * **and the rule: none of it touches the SIMULATION.** The fixed 16.11 Hz
    step is load bearing - every speed in the game is a whole number of pixels
    per frame - so a graphics option changes how a frame is PRESENTED and never
    how one is computed. Same constraint as the frame-rate item above.

  Two honest limitations. The 4:3 stretch spreads 200 source rows over
  `240 * scale`, so rows come out three and four pixels tall in a repeating
  pattern at 3x - inherent to nearest-neighbour on a 320x200 image, and the
  reason square is the default. And scanlines are drawn over the presented
  image rather than into the framebuffer, so `--screenshot` never shows them,
  which is deliberate: a capture must stay comparable to the original.

- small quality-of-life tweaks, each behind a switch that defaults to off.
- **give `GLDFADE` its animation back.** The player's idea, and it is the best
  candidate on this list because the work is already done: six compiled
  sprites that decode and render, a sound already wired to the right event,
  and a slot in the fade table the original filled in and then closed off four
  separate ways (see `docs/reversing-notes.md`, "GLDFADE: the sound is used,
  the ANIMATION cannot be"). Something in 1994 was going to make a gold fade
  happen in the beaker and the Bonus was changed to convert on catch instead.

  What it would take is a rule that puts a type-10 cell in the glass and lets
  it clear - the obvious one being "a Bonus that is tipped rather than caught"
  - which means changing the catch, so it is squarely an ENHANCEMENT and not a
  restoration: nobody can say what the 1994 rule was, only that there was
  meant to be one. Behind a switch, default off, like everything here.

  It is also the only item on this list that would show the player something
  the original never shows anybody, which is a nice thing for a preservation
  project to be able to offer once it has finished being faithful. And it is a
  bonus feature for the Bonus atom.

Keeping `kFrameHz` a real measured constant rather than a fudge factor is what
makes this possible later, which is a second reason the 16.11 Hz correction was
worth making.

---

## Portability

Running on other platforms is a goal in itself; PSP is the first candidate
because its homebrew scene is active and SDL is already available there, but
it is an example rather than the target. Keep these in mind while writing code
rather than retrofitting later. None of it justifies contorting the code now -
it justifies *not* painting into a corner.

- **SDL is the plan - keep it at the edge.** SDL *is* the portability layer;
  the point is to confine it to the platform boundary rather than thread it
  through the game. Today only `main.cpp` (window, input, loop) and `opl.cpp`
  (audio device) include it - 3 files of 16, and `screen.cpp` is not one of
  them, being a plain indexed framebuffer with a `toRgba()` at the end.
  This already pays off twice: `tubes-tests` links no SDL at all, and
  `--dump-regs` proved the sequencer correct with no audio device attached.
  If a target's SDL is missing or awkward, `main.cpp` and `opl.cpp` are the
  only files to rewrite and nothing reversed is touched.
- **Endianness is not a problem.** PSP is MIPS little-endian, same as x86, so
  the format decoders port unchanged.
- **The OPL core may be.** Nuked-OPL3 is cycle-accurate and correspondingly
  expensive for a 333 MHz MIPS chip. If it proves too slow, the register
  stream is the interface - swap the core, keep the sequencer. That separation
  already exists via `RegisterSink` and is worth defending.
- **Prefer integers and floats over doubles.** The PSP FPU is single-precision;
  doubles are emulated. `MusicPlayer` currently uses `double` for the tick
  accumulator, which is fine on desktop and easy to change later.
- **Watch memory.** 32 MB on the original PSP. Loading the whole `.RES` is
  fine (524 KB), but decoding all 73 backgrounds at once would not be.
- **320x200 is a gift.** The PSP screen is 480x272, so the framebuffer fits
  with room for letterboxing at 1x, and the existing integer-scaling path
  already handles the rest.

---

## How things get proven here

Restating because it has repeatedly caught real errors - see `CLAUDE.md` for
the full list.

- Prefer an oracle over an opinion: exact sizes, byte-identical streams,
  offsets landing precisely at EOF.
- A size check is not a correctness check.
- Render it, or listen to it.
- Read the context around a grep hit before believing it. This binary has
  produced at least four coincidences that looked like findings.
- Separate proven from guessed, in writing.


---

## Supporting both editions

Right now the port is built against **one** copy of the game. Both the
shareware and the registered editions are on archive.org, and running against
either is a goal - it is what makes this a preservation project rather than a
port of one person's disc.

**The scope of this changed on 2026-08-02, and it grew.** The player ran the
shareware build and it is not the registered game with fifty waves removed: it
has **an extra menu item, an extra slide deck, an extra ending path and its own
Instructions layout**. So "shareware support" is no longer a mode flag over
shared code - it is a second program to decompile, the same way the registered
one was. See "What shareware mode actually is" below; the measurements in this
section still stand and are what that work builds on.

**MEASURED, against both editions.** Two real images have now been pulled from
archive.org and compared with the copy in `..`, and the answer is smaller than
this section assumed. `docs/reversing-notes.md` has the table.

1. **The wave table.** ~~Hypothesis, untested~~ - **confirmed by counting.**
   `tools/count_waves.py` counts `1000:86b8`'s dispatch arms straight out of an
   unpacked image: **75 in `..`, 25 in both shareware images.** The exit
   screen's "50 more exciting waves" is exactly that difference.
2. **Which edition is in `..`.** ~~Unknown~~ - **REGISTERED**, and it is the
   only one of the three that is. What says so is `PRIZE.GFX`, the ending text
   with its `existance` misspelling, and the 75 arms. (An earlier reading of
   this from the archive's contents was wrong - see the correction in the
   notes.)
3. **The resource sets.** ~~Would have to become version-aware~~ - **they do
   not differ.** `..`'s `TUBES.RES` is **byte-identical** to the shareware
   download's, same md5. Ten `GAMEBG`, both special-atom fade families,
   `TUBESEND.BIN`: all present in the shareware archive too. The second
   archive, `msdos_Tubes_1993`, differs from both only by its publisher splash
   (`IMPULSE.DAT` for `SOFT.*`) and is itself a 25-wave shareware build - an
   Impulse re-release, not the registered edition.
4. **`tools/vercheck.py`** can still say whether the three images came off one
   toolchain, which is worth knowing but no longer blocks anything.
5. **`TUBES.SAV`.** Two banks by game mode, decoded. Whether the layout matches
   across editions is still unknown and is now the only open item here.

**So there is nothing to detect in the DATA, and nothing missing from it.** The
edition lives entirely in the executable. That part held up: the assets a
shareware player has on disk are byte-for-byte the ones a registered player
has, which is what makes the plan below possible at all.

What did **not** hold up is the sentence that followed it - "and the port IS the
executable, so reading a shareware install already works". It works in the
sense that nothing crashes. It does not work in the sense that matters, because
running the port against a shareware install currently gives the player the
**registered** game: 75 waves, both special atoms, no Preview, no ordering
slides, no exit screen. The edition living in the executable does not mean
there is nothing to do; it means **all** of the work is in the executable.

---

## What shareware mode actually is

**Decided:** the port gets a `--shareware` switch - a flag for now, with where
it eventually belongs (auto-detect from the install, a menu choice, or both)
left open until the behaviour is in. The goal is that **a player who only ever
had the shareware disc can play the game they remember**, which is a fitting
thing for an abandonware preservation port to be able to do, and it is the
first time this port will reproduce a program other than the one in `..`.

### What is different, and what says so

Player-reported from a real shareware run on 2026-08-02, then checked against
the strings in `SW_UNP.EXE`. **None of it is decompiled yet.** The evidence
column is what authority currently backs each row - `CLAUDE.md`'s order applies
and only the third column settles anything.

| Difference | Reported | Corroborated by the binary's own text |
|---|---|---|
| 25 waves, not 75 | yes | 25 dispatch arms at `1000:86b8`; *"50 more exciting Waves!"* |
| ~~**No Bonus and no AntiMatter in normal play**~~ **DECOMPILED** | yes | two rate bytes zeroed at `1000:9e63`; see below |
| ~~**An extra menu item, "Preview Registered"**~~ **DECOMPILED** - **two** items inserted, at 3 and 8 | yes | the item table at a `0x24` stride, and `entry`'s dispatch, agreeing independently |
| ~~Preview has **its own wave list**, first wave the **Mischief Crystal** one~~ **DECOMPILED** | yes | a 5-arm chain in `1000:7fc7`; arm 1 is Mischief Crystals, and none of the five occurs in the normal 25 |
| **An "Ordering Info" slide deck** | yes | `Ordering Info`, `Order by Phone`, `Order by Fax`, `Order by BBS (OPEN Door 5)`, `Down - Next Slide  Register!` |
| **Quitting goes to the registration deck first** | yes | *"You can't stop now!"*, *"Lanny is 1/3 of the way to his goal and he still needs your help."* |
| Then `TUBESEND.BIN` is dumped to the **DOS screen** | yes | `TUBESEND.BIN`, `ExitText Resource Error!` |
| No wave-75 ending | - | `PRIZE.GFX`, `existance` and the whole ending text are **absent** from the shareware image |

Two the player did not see, found while checking the above, and both change
the shape of the work:

* ~~**the Instructions text is RE-WRAPPED between the editions.**~~ **WRONG,
  retracted, and the item is closed with no code.** `1ac3:2d0a` extracts to the
  same 21 slides and 174 items as `1b2e:2d63`, and the two string pools decode
  to the same **152 Pascal strings byte-for-byte** - checked through the
  generator and again without it. The two `45 seconds` strings that started
  this are in segment `1000`, the game unit: they are **wave briefings**, and
  the shareware is missing two wrappings because it is missing the two waves
  that carry them. `src/instructions.cpp` serves both editions.

  **And the waves are shared too**, which nothing here had established.
  `1000:86b8` and `1000:7fc7` pair 1..25 as an isomorphism - 14 distinct
  briefing routines, every repeat in the same place, identical text 25 of 25 -
  so the shareware's 25 ARE the registered's first 25 and one `kWaveTable` is
  right for both. See `docs/reversing-notes.md`, "The Instructions are NOT
  re-wrapped";
* ~~**the backgrounds are split, not merely fewer.**~~ **READ, and the guess
  here was wrong in both halves** - which is why it was written down as a
  guess. Normal shareware play is `Random(5)+1`, so `GAMEBG1..5` rolled with no
  immediate repeat, against the registered `Random(10)+1`: **one operand**, not
  a hand-picked set. The five literals are the **Preview**, fixed per wave -
  wave 1 `GAMEBG10`, 2 `GAMEBG5`, 3 `GAMEBG9`, 4 `GAMEBG6`, 5 `GAMEBG7`. So
  `GAMEBG8` is never shown by the shareware at all and `GAMEBG5` is shown by
  both paths.

Also worth recording because the game says it about itself: *"Lanny is 1/3 of
the way to his goal"*. 25 of 75. The game's own text agreeing with a count
taken off the dispatch arms is the fourth time this project's second-rank
authority has confirmed something derived the hard way.

### Savegames and high scores must be separated

**The player's call, and it is right.** The two editions must not share save
state. In 1994 they could not - they were separate installs with separate
directories - so a single port binary that can be either one has a hazard the
original never had: a 25-wave shareware save loaded into a 75-wave registered
session, or the reverse, silently reinterpreted.

This is the **highest-risk item in the whole plan**, because it is the only one
that can damage files the player owns. `CLAUDE.md`'s second rule - do not write
to the player's game directory - is about accidents; this is about a write that
looks entirely intentional and lands in the wrong schema.

So, in order:

1. ~~**Read the shareware save and high-score routines before writing
   anything.**~~ - **DONE, and there is no format work.** All three routines
   are the **same code**: `1ac3:00ac` / `1ac3:0243` / `1000:8fa0` against the
   registered `1b2e:00ac` / `1b2e:0243` / `1000:96db`, same sizes, same
   unit-relative offsets. Decompiled from both images they are 334 lines each
   and differ in **22 lines, all of them string-literal addresses**. Same 396
   byte banks at `DS:0x1610` / `DS:0x179c`, same 36-byte records, same bank
   test on `[0x1d4e]`. `sav_decode.py` and `src/save.cpp` are already right for
   a shareware install.

   **And Preview writes nothing** - two explicit guards on `[0x1d4b]`, at
   `1000:5ed0` (the F2 save prompt) and `1000:9fb9` (the high score entry). So
   no third bank, no extra file.
2. **Refuse rather than reinterpret** - and note the formats being **identical**
   makes this MORE important, not less. A 25-wave shareware save is a
   structurally valid registered save, so nothing in a mismatched file will
   look wrong. This is the port's own hazard, from one binary running either
   edition against either install; the original never had it, so the edition
   tag has to be the port's own and cannot be derived.
3. **Decide the filenames from what the original does.** Writing `TUBES.SAV`
   into a shareware install is faithful, because that is what the shareware
   build does. The combination that has no original behaviour to copy is
   `--shareware` pointed at a **registered** install - a port-side affordance -
   and that one must not touch the registered `TUBES.SAV` or `TUBES.HSC`. A
   port-owned filename for the non-native combination is the obvious answer;
   settle it once both formats are read.

### The exit screen, and the registered edition

`TUBESEND.BIN` is 80 x **23** character/attribute pairs, and the two missing
rows are the whole answer to how it is presented: the program `Move`s it to
`0xB800` and **quits**, leaving the banner on the shell with the DOS prompt
landing in the gap underneath. It is not a screen the game displays. It is a
screen the game leaves behind.

For **shareware mode that is straightforwardly faithful**, and the mechanism
should follow the original rather than approximate it: on exit, write the
decoded banner to the **real terminal** the port was launched from, in CP437
with the attribute bytes as ANSI colour, and let the shell prompt land under
it. That is the same effect by the same means. Fall back to rendering it in the
window as a final frame when there is no tty - and only then, since the fallback
is the part that is invented.

For **the registered edition the honest answer is that it cannot be faithful**,
because the registered executable never names `TUBESEND` and never showed it.
But it does not have to be an invention either, and this is the useful part:
the `.RES` is byte-identical, so **the banner is genuinely sitting in the
registered player's own game files**, unreferenced. Showing it is displaying a
resource they have, not fabricating one. So it belongs behind a **port-owned
affordance** that does not pretend to be the original - the same category as
the fifth Credits page in section 4 of this file, and for the same reason. An
explicit `--exit-screen`, or a Credits-side item, is fine; the registered
game's own quit path stays silent, as it is.

The rule that keeps this straight: **the port may show the player their own
data, but it may not claim the original showed it.**

### The work, in order

1. **Import `SW_UNP.EXE` into its own Ghidra project** - separate from
   `../ghidra-project`, because two programs with the same segment layout in
   one project is a confusion waiting to happen. Run
   `ghidra_scripts/MapProgram.java` over it exactly as the registered image was
   mapped: the call graph plus the strings each function references is what
   identified every interface stage the first time, and the string table above
   means the new stages will name themselves.
2. **Diff the two maps.** Most of this program is the program already
   decompiled. What matters is which functions are new, which are missing, and
   which shifted - and the last group is the trap, since a shifted address that
   still decompiles is the easiest way to write down a wrong finding.
3. **Read the new stages**: the menu with its extra arm, the Preview's wave
   list and background list, the Ordering Info deck, the quit path, and the
   `TUBESEND.BIN` load and dump.
4. ~~**Read the spawn distribution**~~ - **DONE**, and it is the smallest
   possible difference. The spawn code is **byte-identical** between the
   editions, at the same addresses (`1000:4a28` Bonus, `1000:4a64`
   AntiMatter). The whole change is what the session setup writes into two rate
   bytes: registered sets `[0x1d49]=50` and `[0x1d4a]=25` unconditionally,
   shareware zeroes both and restores **those same two values** only when the
   Preview flag `[0x1d4b]` is set. With a rate of 0 the roll is still made and
   always rejected, so shareware mode is two constants rather than a code path,
   and Flashium is unaffected because the fallback `Random(8)+1` includes it.

   **This item previously also claimed the RNG sequence does not diverge
   between the editions. It does** - the gate roll is spent either way but the
   fallback only on rejection, so a rate of 0 spends strictly more calls: 280
   against 301 from seed 12345, first difference at call 21. Caught by a test
   written during the port, not by re-reading the code. The `DEMO.SCR` oracle
   survives for a different reason - the demo runs with the rates **on** in
   both editions, so Preview against registered is 280 against 280 - and that
   is very likely why View Demo sets the Preview flag at all. See the notes.

   The control comparison settles the design: **the registered build contains
   the Preview path too**, reads `[0x1d4b]` at addresses identical to the
   shareware's, and writes it exactly once - to zero, at startup. So the two
   builds are one source, and `preview = false` in registered mode is not the
   port approximating anything; it is what the original does.
5. **Read the save and high-score routines** - see above; this gates any write
   path.
6. **Then port it**, behind `--shareware`, with the generated tables extracted
   mechanically rather than transcribed.

The `.RES` being identical is what makes this tractable: every asset the
shareware program needs is already decoded, already loadable, and already
rendering. The work is entirely code.

---

## Before publishing to GitHub

The repository has never contained game data and `.gitignore` is aggressive
about keeping it that way, so publishing is mostly a matter of paperwork:

- **The licence is chosen: MIT**, for this project's own code. The `LICENSE`
  file is still to be written. The reasoning below is what the choice was made
  against and is kept because it is what a reader will want to check.

  **Choose a licence.** `third_party/nuked-opl3` is
  **LGPL-2.1-or-later** - checked in the file, not from memory: `opl3.c` says
  "either version 2.1 of the License, or (at your option) any later version".
  That is **weak** copyleft and does **not** relicense this project. Any of
  MIT, BSD-3, MPL-2.0 or GPL-2.0-or-later works for our own code, provided the
  emulator keeps its notices, modifications to *it* stay LGPL, and the user can
  relink - which shipping source satisfies outright.
  Two things to watch. **Apache-2.0 is the awkward one** (its patent-termination
  clause is the known one-way incompatibility with GPLv2/LGPLv2.1), so pick it
  only after deciding deliberately. And **static linking is fine** under
  LGPL-2.1 §6 as long as the pieces needed to relink are available; that
  matters for a PSP build, where dynamic linking is not really on the table.
  Also note the emulator is `RegisterSink`-swappable, so a target that cannot
  take LGPL at all can drop in a different core without touching anything
  reversed.
- **The game's own text in `src/`** - `wave_text.cpp`, `instructions.cpp`,
  `credits.cpp`, `cutscene.cpp`. **Decided: it stays**, and the reasoning is
  the player's.

  Tubes was released as SHAREWARE, and the shareware package's own
  `TUBES.DOC` carries the cutscene's story in plain text, near enough verbatim
  - "In a lab far far away in the Great White North... Dr. Lanny B. Brilliant
  was completing his work on the creation of 8 new elements not yet included
  on the periodic table." So the story is not something this project lifted
  out of a binary that was never meant to be read; the publisher shipped it in
  a text file meant to travel with the game.

  Two things to be straight about, because this file's job is to separate
  proven from assumed. "Abandonware" is not a legal category - the copyright
  did not lapse because the company stopped selling it - so the argument that
  carries weight is the shareware one, not that one. And neither `TUBES.DOC`
  nor `CATALOG.TXT` in this copy contains an explicit distribution notice, so
  there is no licence text to point at; the terms are inferred from the
  release model.

  The **assets** rule is untouched and is the one that matters: no `.RES`, no
  `.EXE`, no extracted sprites, no rendered output. Text tables generated from
  the binary are the exception, taken knowingly.

  The fallback still exists if it is ever wanted - read the Pascal
  ShortStrings out of the user's own `TUBES.EXE` at runtime - and the
  generators make it cheap, since the table layout would not change. It is an
  option, not a plan.
- `README.md` already leads with "you need your own copy" and explains why.
  Keep that first; it is the thing that makes the project defensible.
- Re-read `.gitignore` before the first push, and check `git log --stat` for
  anything game-derived that slipped in early. `git ls-files` should show only
  source, docs, scripts and the vendored emulator.
- `assets-extracted/` is ignored wholesale, including every rendered PNG, WAV,
  MIDI and DRO produced during analysis. None of it should ever be committed.
- Consider whether the Ghidra project should be mentioned in the README as
  *deliberately* outside the repo, since it is derived from copyrighted data.
