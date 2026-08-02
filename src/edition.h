#pragma once

// Which build of Tubes is being played.
//
// PROVENANCE. Everything here is decompiled out of `SW_UNP.EXE`, the unpacked
// shareware image, and cross-checked against the registered `TUBES_UNP.EXE`.
// See `docs/reversing-notes.md`, "The shareware program is a DIFFERENT program"
// onward, for the derivation of every constant.
//
// THE ONE FACT THAT SHAPES THIS FILE: the two executables are ONE Pascal
// source. The registered build contains the whole Preview path - it reads
// `DS:0x1d4b` in the same three functions, at addresses identical to the
// shareware's, and writes it exactly once, to zero, at `1000:b1e4`. It simply
// has no menu arm that turns it on.
//
// So this is not a compatibility layer bolted onto a port of one edition. It is
// the same program with two settings, which is what the original is, and
// `preview = false` in registered mode is the original's behaviour rather than
// the port approximating it.
//
// The assets do not enter into it: `TUBES.RES` is BYTE-IDENTICAL between the
// editions, same md5. Every difference below lives in code.

#include <cstdint>

namespace tubes {

enum class Edition {
    kRegistered,   // `..`            - 75 waves
    kShareware,    // 25 waves, plus the Preview
};

// `1000:86b8`'s dispatch has one arm per wave, and `tools/count_waves.py`
// counts them out of an unpacked image without disassembling it: 75 registered,
// 25 shareware. The exit screen's "50 more exciting Waves!" is exactly that
// difference, and the shareware's own registration deck says Lanny is "1/3 of
// the way to his goal" - 25 of 75.
constexpr int kWaveCountRegistered = 75;
constexpr int kWaveCountShareware = 25;

constexpr int waveCountFor(Edition e) {
    return e == Edition::kShareware ? kWaveCountShareware : kWaveCountRegistered;
}

// The backgrounds, `1000:7fc7` against the registered `1000:86b8`. The two
// builds differ by ONE OPERAND:
//
//     repeat n := Random(N) + 1 until n <> LastBackground;   { DS:0x2056 }
//     LastBackground := n;
//     Load('GAMEBG' + Str(n) + '.GFX')
//
// with N = 10 registered (`1000:86d0  PUSH 0xa`) and N = 5 shareware. So normal
// shareware play rolls GAMEBG1..5 with no immediate repeat - it is not a
// hand-picked subset, which an earlier reading guessed and got wrong in both
// halves.
constexpr int backdropCountFor(Edition e) {
    return e == Edition::kShareware ? 5 : 10;
}

// `DS:0x2056`, seeded to 0xff by `entry` (`1000:ab0c` shareware, `1000:b20d`
// registered) so the first roll of a session can never match.
constexpr int kNoLastBackdrop = 0xFF;

// ---------------------------------------------------------------------------
// The Preview
// ---------------------------------------------------------------------------

// `DS:0x1d4b`. A byte, written ONLY in `entry` - four sites in the shareware
// image, one in the registered - and read in four places. It is the whole
// feature: with it set, the ordinary session runs with the registered rules.
//
// Three menu arms set it, which is why an early reading that expected one
// menu item found "two preview arms" and could not place them:
//
//     arm  3   the menu item `Preview Registered`
//     arm  7   `View Demo`
//     arm 11   the attract-mode demo
//
// So the shareware DEMOS ITS REGISTERED CONTENT - the advertisement is the
// demo. Obvious commercially, and not guessable from the menu.

// The Preview's wave list, the `else` side of `1000:7fc7`'s dispatch - five
// arms against the normal twenty-five. NONE of the five occurs anywhere in the
// normal chain: they are registered-only waves carried in the shareware binary
// and reachable only this way. Checked per function, zero hits each.
constexpr int kPreviewWaveCount = 5;

// And the Preview's backgrounds, which are FIXED per wave rather than rolled -
// these are the five `GAMEBG` literals the shareware image names beside the
// constructed prefix, and the reason it names them at all.
//
// Two oddities, recorded because they are what the table says rather than what
// the marketing does. `GAMEBG8` is shown by NO shareware path - normal play
// only rolls 1..5 - and `GAMEBG5` is shown by both, being inside the normal
// roll and Preview wave 2's fixed choice. Since the pitch promises five new
// backgrounds and the new ones are plainly 6..10, this reads like `GAMEBG5`
// where `GAMEBG8` was meant. That is a guess about intent; the table is not.
constexpr int kPreviewBackdrop[kPreviewWaveCount + 1] = {
    0, 10, 5, 9, 6, 7,
};

// The special-atom rates, `DS:0x1d49` (AntiMatter, type 9) and `DS:0x1d4a`
// (Bonus, type 10). `1000:49fd`'s distribution is BYTE-IDENTICAL between the
// editions, at the same addresses - `1000:4a28` and `1000:4a64` in both - and
// takes a second roll against these two bytes:
//
//     if type = 10 then
//         if Random(100)+1 < [0x1d4a] then type := 10 else type := Random(8)+1
//     if type =  9 then
//         if Random(100)+1 < [0x1d49] then type :=  9 else type := Random(8)+1
//
// The session setup is the entire difference. Registered `1000:9e53` writes 50
// and 25 unconditionally; shareware `1000:9718` zeroes both at `1000:9e63` and
// restores EXACTLY THOSE TWO VALUES under the Preview flag at `1000:9e79`.
constexpr int kAntiMatterChanceOn = 0x32;   // 50
constexpr int kBonusChanceOn = 0x19;        // 25

// A rate of 0 is unreachable by `Random(100)+1`, which is at least 1, so every
// roll of 9 or 10 falls through to `Random(8)+1`.
//
// `Random(8)+1` INCLUDES 8, so Flashium is dispensed normally in shareware.
// Only 9 and 10 are suppressed, and the specials family at 11 is untouched.
//
// THE RNG SEQUENCE DOES DIVERGE, and an earlier draft of this file claimed it
// did not. The gate roll `Random(100)+1` is indeed spent either way - but the
// FALLBACK roll is spent only when the special is REJECTED. The registered
// game grants a share of its 9s and 10s and skips the fallback each time;
// shareware normal play rejects every one and always spends it. Measured from
// seed 12345: 280 calls registered against 301 shareware, first difference at
// call 21. `testTheEditionsDivergeExactlyWhereTheSpecialsAre` pins it.
//
// The `DEMO.SCR` oracle survives anyway, for a better reason than the wrong
// one: **the demo always runs with the rates ON in both editions.** Registered
// arms 6 and 9 set 50 and 25 unconditionally, and the shareware's View Demo
// (arm 7) and attract loop (arm 11) set the Preview flag, which restores
// exactly those two values. Preview against registered measures 280 calls
// against 280, identical throughout.
//
// That is very likely WHY those two arms set the flag. A single `DEMO.SCR`
// ships in a `.RES` byte-identical between the editions; played at rate 0 it
// would desync exactly as measured above. The marketing reading - the demo is
// the advertisement - and the technical necessity coincide, and only the
// second one is forced.
constexpr int kRateOff = 0;

// Whether the two special atoms are dispensed at all. The Preview turns them
// back on because it has to: its waves 1 and 2 both require AntiMatter to
// complete and are literally unplayable without type 9. The rate branch and the
// wave list are each other's reason, and they were read hours apart by
// different routes without either looking for the other.
constexpr bool specialAtomsEnabled(Edition e, bool preview) {
    return e != Edition::kShareware || preview;
}

// ---------------------------------------------------------------------------
// What the Preview may NOT do
// ---------------------------------------------------------------------------
//
// Both are explicit branches on `DS:0x1d4b`, read off the disassembly rather
// than inferred from behaviour:
//
//     1000:5ed0   the "F2 to Save Game, ESC for Main Menu" prompt, and the
//                 call into 1000:2dd0 behind it, are skipped when set
//     1000:9fb9   CMP byte ptr [0x1d4b],0x0 / JNZ - skips CALL 1000:8fa0,
//                 the high score entry
//
// So a Preview run can neither be saved nor place a score. That matters beyond
// fidelity: `View Demo` (arm 7) sets `[0x1d4e] = 0`, and `[0x1d4e]` selects the
// high-score bank, so reading arm 7 as an ordinary play mode would have had
// the port filing demo scores into the Endurance bank. The original's guard is
// what stops it.
constexpr bool canSave(bool preview) { return !preview; }
constexpr bool canEnterHiScore(bool preview) { return !preview; }

// The session's edition state, together, because every rule above needs both.
struct EditionState {
    Edition edition = Edition::kRegistered;
    bool preview = false;               // DS:0x1d4b

    int waveCount() const {
        return preview ? kPreviewWaveCount : waveCountFor(edition);
    }
    int backdropCount() const { return backdropCountFor(edition); }
    bool specialAtoms() const { return specialAtomsEnabled(edition, preview); }
    int antiMatterChance() const { return specialAtoms() ? kAntiMatterChanceOn : kRateOff; }
    int bonusChance() const { return specialAtoms() ? kBonusChanceOn : kRateOff; }
    bool canSave() const { return tubes::canSave(preview); }
    bool canEnterHiScore() const { return tubes::canEnterHiScore(preview); }
};

}  // namespace tubes
