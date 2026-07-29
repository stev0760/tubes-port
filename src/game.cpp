#include "game.h"

// PROVENANCE. Most of this file is now transliterated from `1000:3a67` and
// `1000:9e53` and says so at each site: the atom router and its arc tables,
// the spawn (period, column choice, type distribution), the difficulty seeds,
// the test tube's slide, and the Down/B speed boost.
//
// Scoring and the beaker came over too - `1000:22a6` and its four matchers -
// so the awards, the chain bonus multiplier and the fade are code now, not
// readings of the Instructions.
//
// What is NOT from code, and is marked where it appears:
//
//   * kOriginalFps, assumed to be the PC timer's 18.2 Hz;
//   * the specials' behaviours, which the beaker update post-processes at
//     `1000:2790` through a scan helper that is not decoded yet.
//
// The order of authority is in CLAUDE.md: decompiled code settles a rule, the
// game's text corroborates, measurement locates and validates but never
// derives. This file has already carried a scoring rule fitted to two observed
// awards that was simply wrong, and a spawn interval that was pure invention.

#include <algorithm>

namespace tubes {
namespace {

// Scoring, and now from CODE rather than from the Instructions. Each matcher
// adds its own literal to the pending total:
//
//     1000:1c56  vertical     ADD [pending], 0xfa    = 250
//     1000:1e47  horizontal   ADD [pending], 0x1f4   = 500
//     1000:2052  diagonal     ADD [pending], 0x3e8   = 1000   (both directions)
//
// which confirms what the Detailed Instructions said. What the Instructions
// did NOT say, and what no amount of watching gave up, is that the award is
// added ONCE PER SEED POSITION - so a run of four pays twice and a run of five
// three times. "4 atom molecules count as 2 chains" turns out to be a literal
// description of the scan, not a separate chain counter.
//
// Board::step() sums those awards; the multiplier and the ramp are below.

// The score ramps in, and the ramp is where the chain bonus multiplier lives.
// From `1000:2410` and the flush at `1000:58c5`:
//
//     if rampSteps > 0 then begin
//         multiplier := multiplier + runsThisFrame;
//         if increment = 0 then
//             increment := (pending * multiplier) div rampSteps;
//         total := total + increment;
//     end
//     ... and once rampSteps counts down to zero, the remainder is flushed
//         and pending and multiplier are cleared.
//
// So the money is `pending * multiplier`, paid over six frames, and the
// multiplier is the number of DISTINCT runs formed at the same time. One
// three-run pays 250 x 1; two simultaneous runs pay (250 + 250) x 2. That is
// exactly the "chain bonus point multiplier" the Instructions mention without
// quantifying, and it was previously left unimplemented for want of a number.
//
// Note this contradicts one earlier live measurement, which recorded a
// diagonal run of four paying 1000 where this pays 2000. The measurement came
// from the black-box session whose conclusions have already been overturned
// twice; the code is the authority. Flagged in PLAN.md rather than silently
// resolved.
constexpr int kScoreRampSteps = 6;

// `1000:1c70` sets a countdown to 10 on every match, and `1000:5d43` steps it
// down once a frame. While it runs, the frame loop refuses to declare the wave
// complete - so it is the guard that lets a cascade finish before the board is
// judged empty.
constexpr int kClearFrames = 10;

// The lanes an atom travels between. y = 187 is where atoms enter at the
// bottom of a feed tube and y = 68 is the top lane where the test tube can
// catch; both measured.
constexpr int kEntryY = 187;

// The descent ACCELERATES. `1000:13ed`, the first thing state 7 does:
//
//     if rec.y >= 50 then rec.acc := rec.acc + $480
//                    else rec.acc := rec.acc + rec.velocity;
//
// so the difficulty's 2/3/4 px a frame only applies to the top fifty pixels of
// a play column, and below that every atom falls at a flat nine - the same
// 0x480 the Down/B boost and the Bonus atom use. A missed ball therefore drops
// away much faster than it travelled the network, which is what it looks like
// and what the port did not do: it fell the whole way at the difficulty speed.
//
// It also means the Down/B boost cannot affect the last stretch of a descent.
// The boost writes `rec.velocity`, and below y = 50 the velocity is not read.
constexpr int kAccelY = 50;

// The catch is a WINDOW, not "past the mouth" - `1000:1423` and `1000:142d`
// bracket it at 60..70. Eleven pixels against a nine-pixel step, so an atom
// lands inside it on one frame and is past it on the next.
constexpr int kCatchTop = 60;
constexpr int kCatchBottom = 70;

// The tube's sprite is drawn 3 px left of the column it serves, so an atom
// sitting in its mouth is at `tube.x + 3`. The same +3 appears in the router's
// in-tube state and in the six waypoint targets, which are the six column x's
// minus three.
constexpr int kTubeMouthDx = 3;

// `1000:1516`. Past this the atom is lost; the original pins its y here as
// well, before handing the record to the two-frame teardown.
constexpr int kLostY = 187;

// The network topology - which feed tube serves which column along which lane
// - is the pair of DGROUP tables in game.h, indexed by the column:
//
//   column   feed x   lane y   dest x
//      1        10       26      143
//      2        34       13      125
//      3        58        0      107
//      4       246        0      197
//      5       270       13      179
//      6       294       26      161
//
// It is mirror-symmetric about x = 152: 34+270, 58+246, 10+294, 125+179,
// 107+197 and 143+161 all equal 304. The outermost tube takes the LOWEST lane
// and travels furthest, landing on an inner column, so the arcs nest by
// crossing over one another - which is exactly how the furniture is drawn,
// and it is why the six draw slots pair the columns off 1/6, 2/5, 3/4.

// The corner is rounded by displacing the OTHER axis while within 9 px of the
// turn. Transliterated rather than approximated, because the two tables are
// not the same - {9,6,4,2,1} rising against {9,6,3,2,1} horizontally - which
// marks them as hand-tuned pixel art rather than a computed curve.
int riseArcOffset(int distanceToCorner) {
    if (distanceToCorner <= 1) return 9;
    if (distanceToCorner <= 2) return 6;
    if (distanceToCorner <= 4) return 4;
    if (distanceToCorner <= 6) return 2;
    return 1;
}

int crossArcOffset(int distanceToCorner) {
    if (distanceToCorner <= 1) return 9;
    if (distanceToCorner <= 2) return 6;
    if (distanceToCorner <= 4) return 3;
    if (distanceToCorner <= 6) return 2;
    return 1;
}

// The original's frame rate. ASSUMED, not measured: attract mode produced
// ~17 state changes a second, and the PC timer's 18.2 Hz is the obvious
// candidate, but nothing here proves the game loop is tied to it.
constexpr float kOriginalFps = 18.2f;

// The test tube holds five, stated outright by the in-game Detailed
// Instructions and independent of difficulty. The old 5/3/2-by-difficulty
// guess came from TESTUBE1/2/3 sprite heights; the sprites differ for some
// other reason.
constexpr int kTubeCapacity = 5;

// A new game seeds the drop pool from the difficulty. Confirmed by playing all
// three settings, and consistent with the 9/6/3 triple measured in the binary.
// The difficulty block at `1000:a483` writes three numbers per setting, and
// this is all three. Tubes 101 / 201 / 301 differ in the drop allowance, the
// atom's speed through the network, and how often one is dispensed.
int difficultyIndex(Difficulty d) {
    switch (d) {
        case Difficulty::k101: return 0;
        case Difficulty::k201: return 1;
        case Difficulty::k301: return 2;
    }
    return 0;
}

int dropsFor(Difficulty d) {
    static const int kDrops[3] = {9, 6, 3};
    return kDrops[difficultyIndex(d)];
}

int velocityFor(Difficulty d) {
    static const int kVel[3] = {0x100, 0x180, 0x200};   // 1/128 px per frame
    return kVel[difficultyIndex(d)];
}

}  // namespace

Game::Game(int cols, int rows, Difficulty diff, uint32_t seed,
           std::vector<std::pair<int, uint32_t>>* randomTrace)
    : board_(cols, rows),
      tubeCapacity_(kTubeCapacity),
      dropsRemaining_(dropsFor(diff)),
      startingDrops_(dropsFor(diff)),
      spawnInterval_(kSpawnIntervalFrames[difficultyIndex(diff)]),
      networkVel_(velocityFor(diff)),
      rng_(seed ? seed : 1),
      randomTrace_(randomTrace) {
    // 1000:43d6. The test tube STARTS IN A RANDOM COLUMN - `tube.stop :=
    // Random(6) + 1` - and it is the session's very first call to the
    // generator, before anything is dispensed.
    //
    // The port used to park it in the middle. That is wrong twice over: the
    // tube is in the wrong place, and, worse, every later roll is off by one
    // call, so the whole spawn sequence differs. Replaying DEMO.SCR is what
    // exposed it - the recorded player missed all nine drops in 979 frames
    // because the atoms were not where the recording expected them.
    tubeColumn_ = random(kAtomSlots);
    tubeX_ = kTubeStopX[tubeColumn_ + 1];
    tubeTargetX_ = tubeX_;
    // `1000:3be0` seeds the dispenser countdown with **1**, not with the
    // interval - it is the last thing the session's setup does, right after the
    // loop that parks all twelve records at (303, 186). The tick is
    // `Dec(timer); if timer = 0 then dispense`, so a seed of 1 fires on the
    // very first frame and only then reloads the full period.
    //
    // The port seeded it with the interval, which delayed the first dispense by
    // one whole period and slid the entire recorded input stream 50 frames out
    // of step with the game. That is why the demo's player kept reaching for
    // atoms that were not there: the demo's first Left presses are at stream
    // frames 16, 17 and 20, and the original consumes them SIXTEEN frames after
    // its first atom appears, not thirty-three frames before it.
    spawnTimer_ = 1;
    tube_.reserve(static_cast<size_t>(tubeCapacity_));
}

// The fill routines `Move` the mouth's whole record into the new slot and then
// rewrite its type and its y from the per-index literal - `1000:092d`. So a
// filled slot inherits the mouth's x and its arrived flag, and only the type
// and the vertical position are its own.
void Game::pushTubeSlot(int8_t colour) {
    Falling s = tube_.empty() ? Falling{} : tube_.back();
    s.colour = colour;
    s.state = atomstate::kInTube;
    s.slotDy = kSlotDy[tube_.size() + 1];
    s.y = kTubeY + s.slotDy;
    tube_.push_back(s);
}

std::vector<int8_t> Game::tubeTypes() const {
    std::vector<int8_t> out;
    out.reserve(tube_.size());
    for (const Falling& s : tube_) out.push_back(s.colour);
    return out;
}

// A slot at rest sits at the tube's x plus 3 - the same +3 the router writes
// every frame at `1000:17d8` - and at the tube's y plus its own offset.
void Game::setTubeAtoms(const std::vector<int8_t>& v) {
    tube_.clear();
    for (size_t i = 0; i < v.size() && i < static_cast<size_t>(kTubeSlots); ++i) {
        Falling s;
        s.colour = v[i];
        s.state = atomstate::kInTube;
        s.slotDy = kSlotDy[i + 1];
        s.x = tubeX_ + 3;
        s.y = kTubeY + s.slotDy;
        s.arrived = true;
        tube_.push_back(s);
    }
}

// Turbo Pascal 7's `Random`, transliterated. This used to be an xorshift32,
// which was fine for "deterministic across runs" and useless for the one thing
// that matters now: replaying `DEMO.SCR` requires the SAME sequence the
// original produces, because the demo records only the player's buttons and
// every atom it catches was rolled by this generator.
//
// The step is `2000:75bb`, which computes `RandSeed * $08088405 + 1` with
// shifts and adds rather than a 32-bit multiply the 8086 does not have:
//
//     AX := RandSeedLo;  BX := RandSeedHi;  CX := AX;
//     DX:AX := AX * $8405;          { CS:[0xda1], dumped and checked }
//     CX := CX shl 3;  CH := CH + CL;      { lo * $0808 }
//     DX := DX + CX;  DX := DX + BX;
//     BX := BX shl 2;  DX := DX + BX;  DH := DH + BL;
//     BX := BX shl 5;  DH := DH + BL;      { hi * $8405 }
//     AX := AX + 1;  DX := DX + carry;
//     RandSeed := DX:AX
//
// and `Random(n)` at `2000:755e` steps it and takes the top 32 bits of the
// 48-bit product `RandSeed * n` - i.e. `(RandSeed * n) shr 32`, with RandSeed
// read as UNSIGNED. That is a scaled fraction of the range, not a modulus, and
// it is not the same sequence a `% n` would give from the same seed.
int Game::random(int n) {
    if (randomTrace_) randomTrace_->emplace_back(n, rng_);
    rng_ = rng_ * 0x08088405u + 1u;
    return static_cast<int>(
        (static_cast<uint64_t>(rng_) * static_cast<uint32_t>(n)) >> 32);
}

// The dispensed type, transliterated from `1000:49fd`. One roll of 1..11
// picks a *class*, then three of those classes take a second roll:
//
//     type := Random(11) + 1
//     if type = 10 then                              { Bonus }
//         if Random(100)+1 < DS:0x1d4a then 10 else Random(8)+1
//     if type =  9 then                              { AntiMatter }
//         if Random(100)+1 < DS:0x1d49 then  9 else Random(8)+1
//     if type = 11 then                              { the specials }
//         case Random(100)+1 of
//              0..29: 11 Xenon      30..59: 12 Multiplier
//             60..74: 16 Filler     75..89: 14 Convertor
//             90..94: 13 EvilMult   95..100: 15 Blocker
//
// Three things fall out that no amount of watching would have given up:
//
//  * slot 11 is not "Xenon", it is the whole special family sharing one
//    eleventh of the roll, split 30/30/15/15/5/6 between six of them;
//  * the failed-roll fallback is Random(8)+1, which INCLUDES 8 - so Flashium
//    is dispensed as an ordinary member of the pool with no rate of its own;
//  * the 25 and 50 are DS:0x1d4a and DS:0x1d49, written by the session setup
//    to 0x19 and 0x32, so they are per-session knobs rather than literals.
//
// This replaces a "seven colours only" placeholder that PLAN.md carried as
// unmeasured. The specials were always visible in play; what was missing was
// any measured rate to give them.
constexpr int kBonusChance = 25;        // DS:0x1d4a
constexpr int kAntiMatterChance = 50;   // DS:0x1d49

int8_t Game::nextColour() {
    int type = random(11) + 1;
    if (type == kBonus) {
        if (random(100) + 1 >= kBonusChance) type = random(8) + 1;
    } else if (type == kAntiMatter) {
        if (random(100) + 1 >= kAntiMatterChance) type = random(8) + 1;
    } else if (type == kXenon) {
        const int r = random(100) + 1;
        if (r <= 29)      type = kXenon;
        else if (r <= 59) type = kMultiplier;
        else if (r <= 74) type = kFiller;
        else if (r <= 89) type = kConvertor;
        else if (r <= 94) type = kEvilMultiplier;
        else              type = kBlocker;
    }
    return static_cast<int8_t>(type);
}

// One frame of the beaker: `1000:22a6`, which the frame body calls once at
// `1000:47d0` whether or not anything is happening - that is what animates the
// clear and the settle. The ramp's own clock is NOT here; see `stepScoreRamp`.
// THE ENDURANCE RAMP, `1000:235c`. The game speeds up as you clear, and it is
// driven by MATCHES - not by atoms dispensed, and not by time:
//
//     if runsThisFrame >= 1 then                        { 1000:2342 }
//       if (waveMode = 1) or (waveMode = 0) then begin
//         Inc(counter);                                 { [fe84], a BYTE }
//         if counter = 0 then exit;                     { wrap guard, 1000:2368 }
//         if counter mod 5 = 0 then begin
//             if not latch5 then begin
//                 spawnInterval := spawnInterval - 5;
//                 latch5 := true end
//         end else latch5 := false;
//         if counter mod 10 = 0 then begin
//             if not latch10 then begin
//                 velocity      := velocity + $20;
//                 spawnInterval := spawnInterval + 5;
//                 latch10 := true end
//         end else latch10 := false
//       end
//
// The two latches make each crossing fire once. Every tenth match the two
// adjustments cancel, so the net shape is: the interval drops five frames per
// ten matches while the velocity climbs 0x20 with it.
//
// THIS IS WHY THE DEMO REPLAY FELL APART, and it stayed hidden for a long time
// because nothing about it is visible early. The original's spawns land on
// exact 50-frame centres for 29 atoms and then switch to 45, on the frame its
// fifth match landed. The port dispensed at 50 forever, so from there its atoms
// arrived later and later against a test tube that was in the right place the
// whole time - and the first thing anyone notices is a green ball dropping.
// Measured off the running original: spawn 29 at frame 1400.1 and spawn 30 at
// 1445.3, where the port had 1400 and 1450.
//
// `1000:23fe` increments the same counter on the path for the other wave modes,
// which the port has no equivalent for.
void Game::stepEnduranceRamp(int runs) {
    if (runs < 1) return;

    rampCounter_ = static_cast<uint8_t>(rampCounter_ + 1);
    if (rampCounter_ != 0) {
        if (rampCounter_ % 5 == 0) {
            if (!rampLatch5_) {
                // A byte and a word in the original, so they wrap there too.
                spawnInterval_ = (spawnInterval_ - 5) & 0xFF;
                rampLatch5_ = true;
            }
        } else {
            rampLatch5_ = false;
        }
        if (rampCounter_ % 10 == 0) {
            if (!rampLatch10_) {
                networkVel_ = (networkVel_ + 0x20) & 0xFFFF;
                spawnInterval_ = (spawnInterval_ + 5) & 0xFF;
                rampLatch10_ = true;
            }
        } else {
            rampLatch10_ = false;
        }
    }
}

void Game::updateBeaker() {
    const BoardStep s = board_.step();

    if (s.award > 0) {
        scorePending_ += s.award;
        rampSteps_ = kScoreRampSteps;   // 1000:1c63  MOV [rampSteps], 6
        clearTimer_ = kClearFrames;     // 1000:1c70  MOV [clearTimer], 10
        // The increment is NOT cleared here. `1000:1c63` writes the step count
        // and the clear timer and nothing else, and the only zero into the
        // increment is at `1000:58d2`, on the frame the ramp runs out. So an
        // award landing mid-ramp extends the ramp at the rate already running
        // rather than recomputing it, and the difference is paid by the
        // remainder at the flush. The port used to zero it here, which made
        // every second award pay out faster than the original's.
        pendingSound_ = s.soundType;
    }
    // An AntiMatter blast pays nothing but still arms the clear timer, so a
    // cascade behind it is not judged complete early - `1000:0f76`.
    if (s.blast) {
        clearTimer_ = kClearFrames;
        pendingSound_ = s.soundType;
    }
    // 1000:2777, at the tail of the gravity pass: if anything moved this frame,
    // one HITATOM. Not one per atom - a whole beaker settling is a single
    // knock, which is why the flag is a boolean and not a count.
    if (s.settled) pendingSound_ = sfx::kHitAtom;
    chains_ += s.chainsVertical + s.chainsHorizontal + s.chainsDiagonal;

    stepEnduranceRamp(s.runs);

    // 1000:2410 - one sixth of the award, paid every frame the ramp is live.
    if (rampSteps_ > 0) {
        scoreMultiplier_ += s.runs;
        if (rampIncrement_ == 0) {
            rampIncrement_ = scorePending_ * scoreMultiplier_ / rampSteps_;
        }
        score_ += rampIncrement_;
    }

    if (clearTimer_ > 0) --clearTimer_;

    // NO overflow loss. The port used to end the game the moment a column
    // reached the top, which froze it solid with no message - `update()`
    // returns immediately once gameOver_ is set, so the window stayed up and
    // nothing ever moved again. It reads as a crash and was reported as one.
    //
    // It was also invented. `1000:3a67` sets its game-over flag in exactly two
    // places: `1000:5d0a`, when the drop counter wraps past zero to 0xff, and
    // `1000:47f8`, when the wave's objective pattern is satisfied. A full
    // beaker is neither. Filling a column simply means nothing more can be
    // tipped into it - `Board::drop` already returns false and the atom stays
    // in the test tube.
}

// The ramp's clock, `1000:58c5`, which is a separate statement in the frame
// body and sits AFTER the router at `1000:4819`:
//
//     Dec(rampSteps);
//     if rampSteps = 0 then begin
//         increment := 0;
//         q := LongDiv(pending * multiplier, 6);      { remainder in BX:CX }
//         score := score + remainder;
//         pending := 0;  multiplier := 0
//     end
//
// Keeping it out of `updateBeaker()` is not tidying. The beaker runs at
// `1000:47d0`, before the router, so a Bonus caught this frame arms the ramp
// too late for the beaker's own payment - and this decrement then spends the
// first of the six steps anyway. With the two fused, the Bonus was paid seven
// sixths of its award instead of six; a match, whose award is raised inside
// the beaker, was unaffected, which is why the fusion looked right.
void Game::stepScoreRamp() {
    if (rampSteps_ == 0) return;
    if (--rampSteps_ == 0) {
        rampIncrement_ = 0;
        // The flush pays the division's remainder, so the total is exactly
        // pending * multiplier however the sixths round.
        score_ += scorePending_ * scoreMultiplier_ % kScoreRampSteps;
        scorePending_ = 0;
        scoreMultiplier_ = 0;
    }
}

// The other half of the specials. The beaker-side four are in `board.cpp`,
// where `1000:2790` runs them over the settled grid; these four fire when the
// TEST TUBE catches one, and they are dispatched out of the router by
// `1000:180c` on the frame the caught atom finishes sliding down to its slot:
//
//     if rec.arrived <> 0 then begin
//         if rec.type = 10 then Bonus(link);                { 1000:07db }
//         if ds:[0x1d48] <> 0 then begin
//             if rec.type = 12 then Multiplier(link);       { 1000:08d2 }
//             if rec.type = 13 then EvilMultiplier(link);   { 1000:0a27 }
//             if rec.type = 16 then Filler(link);           { 1000:0b55 }
//         end
//     end
//
// The Bonus sits deliberately OUTSIDE the `DS:0x1d48` gate and the other three
// inside it - the same gate that guards the Blocker on the beaker side.
//
// All four work on `slot[count]`, the mouth of the tube - which is normally the
// atom that just arrived, but need not be: an atom caught while an earlier one
// is still sliding arrives second, and the routine still acts on the top slot.
// That is the original's own behaviour and is left in.
//
// Each of the four rewrites the type of the slot it touches, which is what
// stops them firing again on the next frame - `arrived` stays set for as long
// as the atom is in the tube, so the dispatch is re-entered every frame and the
// type is the only guard. Same trick as AntiMatter rewriting its cells to 9.
void Game::catchSpecial() {
    if (tube_.empty()) return;
    const int8_t type = tube_.back().colour;

    if (type == kBonus) {
        // 1000:07f7  slot[count].type := 8. The Instructions' "turns into
        // Flashium when caught" is the code's own doing, one byte.
        tube_.back().colour = kFlashium;
        // 1000:0803: on the way from zero the drop counter's sprite has been
        // erased, so it is redrawn before the count goes back up. The port has
        // no counter to redraw yet; the increment at 1000:082e is the point,
        // and it is the only thing in the game that grows the pool.
        ++dropsRemaining_;

        // 1000:0846 onward is the score path, and it is the same six-frame
        // ramp `updateBeaker()` runs - identical code, down to computing the
        // increment only when it is still zero and adding it once inline.
        bonusAward_ += kBonusAward;         // 1000:0846
        ++scoreMultiplier_;                 // 1000:084d
        scorePending_ += bonusAward_;       // 1000:0859
        rampSteps_ = kScoreRampSteps;       // 1000:0866
        clearTimer_ = kClearFrames;         // 1000:086c
        if (rampIncrement_ == 0) {          // 1000:0872
            rampIncrement_ = scorePending_ * scoreMultiplier_ / rampSteps_;
        }
        score_ += rampIncrement_;           // 1000:08c4
        pendingSound_ = kBonus;             // 1000:083b, sound[10] = GLDFADE
    }

    if (!board_.specialsEnabled()) return;

    // 1000:08d2 - the Multiplier fills the tube with random ordinary balls.
    //
    //     slot[count].type := Random(8) + 1;
    //     i := count;
    //     while i < 5 do begin
    //         Inc(i);
    //         Move(slot[count], slot[i], 28);      { the caught record }
    //         slot[i].type := Random(8) + 1;
    //         slot[i].dy   := yofs[i];             { 52 39 26 13 0 }
    //         slot[i].y    := slot[i].dy + $44
    //     end;
    //     count := 5
    //
    // Random(8) + 1 is 1..8, so Flashium is one of the eight it can roll -
    // the same distribution the dispenser uses for an ordinary atom.
    if (type == kMultiplier) {
        tube_.back().colour = static_cast<int8_t>(random(8) + 1);
        while (static_cast<int>(tube_.size()) < kTubeSlots) {
            pushTubeSlot(static_cast<int8_t>(random(8) + 1));
        }
    }

    // 1000:0a27 - the Evil Multiplier is the same routine with the roll
    // replaced by a literal 11. It writes Xenon into the caught slot and then
    // copies that record upward, so every slot it fills is Xenon too.
    if (type == kEvilMultiplier) {
        tube_.back().colour = kXenon;
        while (static_cast<int>(tube_.size()) < kTubeSlots) {
            pushTubeSlot(kXenon);
        }
    }

    // 1000:0b55 - the Filler.
    //
    //     for i := 5 downto 2 do begin
    //         Move(slot[i - 1], slot[i], 28);
    //         slot[i].dy := yofs[i];  slot[i].y := slot[i].dy + $44
    //     end;
    //     slot[1].type := 17                       { FILLBALL }
    //
    // The count is NOT touched, so the shift pushes the top slot out of the
    // stack: the Filler is at slot[count] and is exactly what gets discarded.
    // What is left is the same number of atoms with an immovable one under
    // them, and `1000:4715` will not tip a 17 - which is where "permanently
    // reduces the tube's capacity by one" actually comes from. Nothing
    // anywhere writes a capacity variable.
    if (type == kFiller) {
        tube_.pop_back();
        Falling fill;
        fill.colour = kObstacle;
        fill.state = atomstate::kInTube;
        fill.arrived = true;
        tube_.insert(tube_.begin(), fill);
        // The shift moves every slot up one, and each one's y offset is
        // rewritten from the literal for its NEW index - 1000:0b96 onward.
        for (size_t i = 0; i < tube_.size(); ++i) {
            tube_[i].slotDy = kSlotDy[i + 1];
            tube_[i].y = kTubeY + tube_[i].slotDy;
        }
    }

    // Not ported: all three gated routines carry a tail guarded by
    // `DS:0x1d4e = 4`, a wave mode, which keeps a running count of what is in
    // the tube - the fills increment it per ball, the Filler and the router's
    // release path decrement it, and reaching zero adds 2 to the clear timer.
    // The port has no wave modes, so there is nothing for it to count.
}

// Dispense one atom, transliterated from `1000:4918`. The column is rolled,
// not assigned round-robin, and it is re-rolled up to ten times to find a free
// slot - so the network naturally thins out when it is busy:
//
//     tries := 0;
//     repeat  col := Random(6) + 1;  Inc(tries)
//     until (atom[col].state = 0) or (tries = 10);
//     if tries = 10 then col := 1;
//     interval := spawnInterval;                 { reloaded either way }
//     if atom[col].state <> 0 then exit;         { give up this period }
//
// The retry giving up on column 1 and then finding column 1 busy is how the
// original skips a beat under load. Note the timer is reloaded before the
// bail-out, so a skipped dispense costs a full period rather than retrying
// next frame.
void Game::spawn() {
    int col = 1;
    int tries = 0;
    do {
        col = random(kAtomSlots) + 1;
        ++tries;
        if (atoms_[col].state == atomstate::kFree) break;
    } while (tries != 10);
    if (tries == 10) col = 1;

    spawnTimer_ = spawnInterval_;
    if (atoms_[col].state != atomstate::kFree) return;

    Falling& a = atoms_[col];
    a = Falling{};
    a.column = col;
    a.x = kFeedX[col];
    a.anchorX = kFeedX[col];      // record +0x04, written from the same table
    a.y = kEntryY;
    a.targetY = kLaneY[col];
    a.state = atomstate::kRise;
    a.velocity = networkVel_;
    a.colour = nextColour();
}

// The test tube: one input read and one step of its state machine, from
// `1000:44f0` through `1000:47c4`.
//
//     if tube.state <> 0 then goto RunStateMachine;      { 1000:44f0 }
//     btn := ReadButtons;
//     if btn and $10 <> 0 then tube.state := 3;          { A - tip }
//     if (btn and 4) and (tube.stop > 1) then            { Left }
//         tube.state := 1;  Dec(tube.stop);  tube.target := stopX[tube.stop]
//     if (btn and 8) and (tube.stop < 6) then            { Right }
//         tube.state := 2;  Inc(tube.stop);  tube.target := stopX[tube.stop]
//
// The whole input block sits inside `if state = 0`, so nothing is accepted
// while the tube is busy. That is why holding Left slides one column at a time
// rather than accelerating, and it is also the ONLY thing gating A: the press
// is not edge-detected anywhere. Holding A tips repeatedly, one atom every six
// frames, because six frames is exactly how long the animation takes to hand
// the state back. The port used to edge-detect A, which made holding it do
// nothing at all.
// Down or B speeds the atom heading for the column the tube is under, from
// `1000:4534`:
//
//     case tube.stop of
//       1: atom[3].velocity := $480;      4: atom[6].velocity := $480;
//       3: atom[1].velocity := $480;      6: atom[4].velocity := $480;
//     else atom[tube.stop].velocity := $480
//
// - the four literal cases write fixed frame offsets that decode to exactly
// those slots at the array's 28-byte stride, so the permutation is 1<->3 and
// 4<->6 with 2 and 5 fixed. That is just "the slot whose destination x is where
// the tube is", so comparing the x values reproduces it without hard-coding it.
//
// TWO things about WHERE this sits, both of which the port had wrong:
//
//   * it is INSIDE `if tube.state = 0`, so a tube that is sliding or tipping
//     grants no boost at all. The port ran it unconditionally every frame.
//   * it runs BEFORE the Left/Right handler updates `tube.stop`, so on the
//     frame a direction is pressed the boost still goes to the slot the tube
//     was leaving. The port moved the tube first and boosted after.
//
// The velocity field is reloaded by the router at the end of every frame, so
// this lasts exactly one frame and the player has to hold the button - which is
// what the recorded demo does, holding Down for 150 of its first 250 frames.
void Game::boostAtomUnderTube(uint8_t buttons) {
    if (!(buttons & (button::kDown | button::kB))) return;
    for (int c = 1; c <= kAtomSlots; ++c) {
        if (atoms_[c].active() &&
            kAtomColumnX[atoms_[c].column] == playColumnX(tubeColumn_)) {
            atoms_[c].velocity = kBoostVel;
        }
    }
}

void Game::stepTube(uint8_t buttons) {
    if (tubeState_ == 0) {
        if (buttons & button::kA) {
            tubeState_ = 3;
        }
        boostAtomUnderTube(buttons);
        if ((buttons & button::kLeft) && tubeColumn_ > 0) {
            tubeState_ = 1;
            --tubeColumn_;
            tubeTargetX_ = kTubeStopX[tubeColumn_ + 1];
        }
        if ((buttons & button::kRight) && tubeColumn_ < kAtomSlots - 1) {
            tubeState_ = 2;
            ++tubeColumn_;
            tubeTargetX_ = kTubeStopX[tubeColumn_ + 1];
        }
    }

    // 1000:45e7. The port used to step a whole column every three frames
    // instead, which put the tube on a stop on every frame - so a capture that
    // caught the original mid-slide could never be matched.
    if (tubeState_ == 1) {
        tubeX_ -= kTubeSlidePx;
        if (tubeX_ <= tubeTargetX_) { tubeX_ = tubeTargetX_; tubeState_ = 0; }
        return;
    }
    if (tubeState_ == 2) {
        tubeX_ += kTubeSlidePx;
        if (tubeX_ >= tubeTargetX_) { tubeX_ = tubeTargetX_; tubeState_ = 0; }
        return;
    }
    if (tubeState_ != 3) return;

    // --- the tipping animation, 1000:463a ------------------------------
    //
    //     Inc(divider);
    //     if divider <> 2 then exit;              { a phase lasts two frames }
    //     divider := 0;
    //     Inc(phase);
    //     case phase of
    //       2: begin for i := 1 to 5 do Dec(slot[i].x);
    //                slot[1].y := 99;  slot[2].y := 93;  slot[3].y := 87;
    //                slot[4].y := 81;  slot[5].y := 73 end;
    //       3: for i := 1 to 5 do slot[i].y := 82;
    //       4: for i := 1 to 5 do Inc(slot[i].x);
    //     end;
    //     if phase = 4 then begin
    //         tube.state := 0;  phase := 1;  <release>
    //     end
    //
    // Six frames end to end. Phase 2 is the tube tilting - the contents bunch
    // toward the middle - and phase 3 is it pouring, with all five in a line.
    // Both sets of positions are written as literals per slot, not computed
    // from an angle, so they are transliterated as a table.
    //
    // The loops run over all five slots whatever the count, which only matters
    // because it moves slots that hold nothing. The port keeps `tube_` sized to
    // the count, so it moves what exists; the surplus is invisible either way.
    if (++tipDivider_ != kTipDivider) return;
    tipDivider_ = 0;
    ++tubePhase_;

    if (tubePhase_ == tubephase::kTilted) {
        for (size_t i = 0; i < tube_.size(); ++i) {
            --tube_[i].x;
            tube_[i].y = kTiltY[i + 1];
        }
    } else if (tubePhase_ == tubephase::kPoured) {
        for (Falling& s : tube_) s.y = kPourY;
    } else if (tubePhase_ == tubephase::kRelease) {
        for (Falling& s : tube_) ++s.x;
    }

    if (tubePhase_ == tubephase::kRelease) {
        tubeState_ = 0;
        // Back to 1 BEFORE the frame's draw, which is why nothing ever renders
        // phase 4 and why three tube sprites cover four phases.
        tubePhase_ = tubephase::kUpright;
        releaseTippedAtom();
    }
}

// `1000:4715`. The mouth's record is handed to the first free record of 7..12
// and starts falling; the tube just loses a slot.
//
//     if tube.count = 0 then exit;
//     if tube.slot[tube.count].type = 17 then exit;      { FILLBALL }
//     tube.slot[tube.count].state  := 9;
//     tube.slot[tube.count].column := tube.stop;
//     n := 1; while atom[n + 6].state <> 0 do Inc(n);
//     if n = 6 then RunError;                            { halts the game }
//     Move(tube.slot[tube.count], atom[n + 6], 28);
//     Dec(tube.count)
//
// Two things the port had wrong before this. It tipped `slot[1]`, so the tube
// emptied oldest-first; and a type 17 in the mouth simply refuses, which is the
// whole of what the Filler does to you.
//
// The record is MOVED, so the falling atom keeps the position it had in the
// tube - it appears exactly where the mouth was, not at the beaker.
void Game::releaseTippedAtom() {
    if (tube_.empty()) return;
    if (tube_.back().colour == kObstacle) return;

    Falling tipped = tube_.back();
    tipped.state = atomstate::kTipped;
    // The board's columns are left to right and so are the tube's stops, so
    // the stop index IS the column. The network's own 1..6 numbering is a
    // different order and is not involved here.
    tipped.column = tubeColumn_ + 1;
    tipped.arrived = false;

    for (int n = kAtomSlots + 1; n <= kAtomRecords; ++n) {
        if (atoms_[n].state != atomstate::kFree) continue;
        atoms_[n] = tipped;
        tube_.pop_back();
        return;
    }
    // The original halts here. Six slots against six columns and a fall of at
    // most nine frames, so it cannot happen; if it somehow does, dropping the
    // tip is better than dropping the game.
}

// One frame of the dispenser path. Speeds are the measured px/frame values,
// and each leg ends when it reaches its target rather than after a duration.
void Game::stepFrame(uint8_t buttons) {
    updateBeaker();

    stepTube(buttons);

    // The router runs over all TWELVE records, then the dispenser ticks. Both
    // are unconditional in the original - it does not wait for the network to
    // empty, which is why six atoms can be in flight at once. `1000:47fe`:
    //
    //     for i := 1 to 12 do Router(@atom[i], BP);
    //
    // and 7..12 are the tipped atoms, in the same loop as the network. Nothing
    // distinguishes the two halves except which states their records are in.
    for (int c = 1; c <= kAtomRecords; ++c) {
        if (atoms_[c].active()) stepAtom(atoms_[c]);
    }

    // The tube's own contents are routed too, but by a SECOND loop at
    // `1000:4849` - and that one is skipped entirely while the tube is tipping:
    //
    //     if tube.state <> 3 then
    //         for i := 1 to tube.count do Router(@tube.slot[i], BP);
    //
    // That gate is what lets the animation own the slots' positions. Without
    // it the router would put every slot back at `tube.x + 3` and slide its y
    // toward the resting offset on the very frame the animation moved it.
    if (tubeState_ != 3) {
        for (Falling& s : tube_) stepAtom(s);
    }

    // 1000:486b, between the router and the dispenser tick.
    if (++flashTick_ == kFlashPeriod) {
        flashTick_ = 1;
        if (++flashColour_ == kFlashium) flashColour_ = kRedium;
    }

    if (--spawnTimer_ <= 0) spawn();

    // Last, as `1000:58c5` is - after the router, so a special caught this
    // frame has already armed the ramp.
    stepScoreRamp();
}

// One frame of one atom: `FUN_1000_0f80`, transliterated, plus the catch and
// miss tests that follow it in the caller.
void Game::stepAtom(Falling& a) {
    switch (a.state) {
        case atomstate::kRise: {
            a.accY += a.velocity;
            a.y -= a.accY / kSubPixel;
            a.accY &= kSubPixel - 1;
            if (a.y < a.targetY) {
                a.y = a.targetY;
                a.accY = 0;
                // column < 4 decides the turn: columns 1..3 sit right of
                // their feed tube, 4..6 sit left of theirs.
                a.state = (a.column < 4) ? atomstate::kGoRight
                                         : atomstate::kGoLeft;
            }
            a.onArc = (a.y < a.targetY + 9);
            if (a.onArc) {
                const int d = riseArcOffset(a.y - a.targetY);
                a.x = a.anchorX + ((a.column < 4) ? d : -d);
            }
            break;
        }

        case atomstate::kGoRight: {
            a.accX += a.velocity;
            a.x += a.accX / kSubPixel;
            a.accX &= kSubPixel - 1;
            a.anchorX = kAtomColumnX[a.column];
            if (a.anchorX < a.x) {
                a.x = a.anchorX;
                a.accX = 0;
                a.state = atomstate::kDescend;
            }
            a.onArc = (a.x > a.anchorX - 9);
            if (a.onArc) {
                a.y = a.targetY + crossArcOffset(a.anchorX - a.x);
            }
            break;
        }

        case atomstate::kGoLeft: {
            a.accX += a.velocity;
            a.x -= a.accX / kSubPixel;
            a.accX &= kSubPixel - 1;
            a.anchorX = kAtomColumnX[a.column];
            if (a.x < a.anchorX) {
                a.x = a.anchorX;
                a.accX = 0;
                a.state = atomstate::kDescend;
            }
            a.onArc = (a.x < a.anchorX + 9);
            if (a.onArc) {
                a.y = a.targetY + crossArcOffset(a.x - a.anchorX);
            }
            break;
        }

        case atomstate::kDescend:
            // See kAccelY: the difficulty speed only holds for the first fifty
            // pixels, and everything below that falls at a flat nine.
            a.accY += (a.y >= kAccelY) ? kBoostVel : a.velocity;
            a.y += a.accY / kSubPixel;
            a.accY &= kSubPixel - 1;
            break;

        // In the tube, `1000:17d1`. The atom tracks the tube sideways and
        // drops to its slot at a flat 9 px a frame - no fixed point, no
        // velocity, just an integer step and a clamp:
        //
        //     rec.x := tube.x + 3;
        //     if rec.type = 17 then begin              { FILLBALL snaps }
        //         rec.y := tube.y + rec.dy;  rec.arrived := 1 end;
        //     <the specials dispatch, if rec.arrived>
        //     if (tube.y + rec.dy = rec.y) and rec.arrived then exit;
        //     rec.y := rec.y + 9;
        //     if tube.y + rec.dy < rec.y then begin
        //         rec.y := tube.y + rec.dy;
        //         PlaySound(if rec.dy = 52 then <floor> else <stack>);
        //         rec.arrived := 1
        //     end
        //
        // The FILLBALL is the only type that arrives instantly, which makes
        // sense: the Filler inserts it under everything, where a slide would
        // have to travel upward.
        case atomstate::kInTube: {
            a.x = tubeX_ + 3;
            const int rest = kTubeY + a.slotDy;
            if (a.colour == kObstacle) {
                a.y = rest;
                a.arrived = true;
            }
            if (a.arrived) catchSpecial();
            if (a.arrived && a.y == rest) break;
            a.y += kTubeDropPx;
            if (a.y > rest) {
                a.y = rest;
                a.arrived = true;
                // 1000:18b3 - the bottom slot rings the glass, anything above
                // it knocks against the atom below.
                pendingSound_ = (a.slotDy == kSlotDy[1]) ? sfx::kHitGlass
                                                         : sfx::kHitAtom;
            }
            break;
        }

        // Falling out of the tube into the beaker, `1000:15bd`. The target is
        // recomputed EVERY frame from the first free row of the column, so an
        // atom already on its way down lands correctly if the column settles
        // under it. The field it is kept in is the same `+0x0d` the tube used
        // for the slot offset, and on arrival it is rewritten in place from a
        // y to the row number that y meant - `1000:167c`.
        case atomstate::kTipped: {
            a.y += kTubeDropPx;
            const int col = a.column - 1;
            int row = 0;                       // Pascal 1..5, 0 for "full"
            for (int r = board_.rows(); r >= 1; --r) {
                if (board_.at(col, r - 1) == kEmpty) { row = r; break; }
            }
            // 1000:1666 - a full column gets 0xbb, a target no atom reaches by
            // falling short of it, so the branch below always fires.
            a.slotDy = row ? kLandY[row] : 0xbb;
            if (a.y < a.slotDy) break;

            a.state = atomstate::kLanded;
            if (row == 0) {
                // 1000:16d3. The column is full: the atom is destroyed and it
                // costs a drop. It does NOT sit on top or bounce.
                if (a.colour != kBonus && dropsRemaining_ > 0) --dropsRemaining_;
                pendingSound_ = sfx::kDrop;      // 1000:172a
                break;
            }
            board_.set(col, row - 1, a.colour);
            // 1000:1765. Reaching the floor rings the glass; landing on a
            // stack knocks. Same pair as the in-tube slide above.
            pendingSound_ = (row == board_.rows()) ? sfx::kHitGlass
                                                   : sfx::kHitAtom;
            break;
        }

        // 1000:18ec. A landed record spends two more frames going 1 -> 2 -> 0
        // before its slot can be reallocated. Neither state draws.
        case atomstate::kLanded:
            a.state = atomstate::kLanded2;
            break;
        case atomstate::kLanded2:
            a.state = atomstate::kFree;
            break;
    }

    // The LAST thing the router does to every atom, every frame, at
    // `1000:1906`:
    //
    //     if record.type = 10 then record.velocity := 0x480
    //                        else record.velocity := session.baseVelocity
    //
    // so the velocity is reloaded on every single frame. The Down/B boost that
    // the caller writes just before this loop therefore lasts exactly one
    // frame: the player has to HOLD the button, which is what the game
    // actually does and what the previous version got wrong.
    //
    // That mistake is worth naming. The velocity field was declared to have
    // "exactly three writers" after searching `1000:3a67` - but the field is
    // also written here, in `1000:0f80`, which had never been disassembled.
    // The search was sound and the conclusion was still false, because the
    // search covered one function and the answer was in another. A closed list
    // is only closed over what you looked at.
    //
    // It also settles a lead that had been sitting in PLAN.md from play:
    // GOLDBALL travels the tube very fast. It is type 10, and its velocity is
    // forced to 0x480 - nine pixels a frame - by type, every frame.
    a.velocity = (a.colour == kBonus) ? kBoostVel : networkVel_;

    if (a.state != atomstate::kDescend) return;

    // The catch, `1000:1423`:
    //
    //     if (rec.y >= 60) and (rec.y <= 70) and (rec.x = tube.x + 3)
    //        and (tube.count <> 5) and (tube.state <> 3) then begin
    //         rec.acc := 0;  rec.state := 8;
    //         Inc(tube.count);  rec.dy := yofs[tube.count];
    //         Move(rec, tube.slot[tube.count], 28);
    //         rec.state := 1
    //     end
    //
    // Three things the port had wrong. It is a WINDOW at 60..70, not "past the
    // mouth"; the tube will not catch while it is tipping; and the network
    // record goes to state 1 rather than straight to free, so its slot cannot
    // be reused for two more frames.
    //
    // The test is `rec.x = tube.x + 3` - the tube's ACTUAL x, not its stop
    // index. The two differ for the three frames of a slide, because Left and
    // Right move the stop immediately and the tube then takes 6 px a frame to
    // catch up. So a tube on its way to a column does NOT catch there yet, and
    // a tube on its way out still catches at the one it is leaving until it has
    // physically left.
    //
    // The port compared stop indices, which caught an atom up to three frames
    // early. That is not a wash: catching early puts an extra atom in a tube
    // that holds five, and `tube.count <> 5` then refuses a LATER atom the
    // original had room for. Two of the demo's atoms were lost that way.
    //
    // Note the boost at `1000:4534` genuinely does use the stop index, so the
    // asymmetry between the two tests is the original's, not an oversight here.
    if (a.y >= kCatchTop && a.y <= kCatchBottom &&
        a.x == tubeX_ + kTubeMouthDx &&
        !tubeFull() && tubeState_ != 3) {
        // Into the mouth, not into the stack: the atom becomes slot[count] and
        // then SLIDES down to its resting offset over the next few frames. The
        // port used to teleport it into place and fire its special at once.
        Falling s = a;
        s.state = atomstate::kInTube;
        s.slotDy = kSlotDy[tube_.size() + 1];
        s.arrived = false;
        s.accY = 0;
        tube_.push_back(s);
        a.state = atomstate::kLanded;
        return;
    }

    // Otherwise it falls past and is lost - `1000:1516`. A missed atom does NOT
    // land in the beaker; the beaker fills only from the tube, via Button A.
    if (a.y > kLostY) {
        a.y = kLostY;
        a.state = atomstate::kLanded;
        pendingSound_ = sfx::kDrop;         // 1000:15a0, whatever was missed
        // 1000:153e. A missed BONUS costs nothing. It is the same exemption the
        // full-column loss in state 9 makes, and the port had it in one place
        // and not the other.
        if (a.colour == kBonus) return;
        if (dropsRemaining_ > 0) {
            --dropsRemaining_;
        } else {
            // Losing means dropping *more* atoms than allowed, so the
            // allowance is spent first and the next miss ends it.
            gameOver_ = true;
        }
    }
}

void Game::stepOnce(uint8_t buttons) {
    if (gameOver_) return;
    stepFrame(buttons);
}

void Game::update(uint8_t buttons, float dt) {
    if (gameOver_) return;

    // Convert elapsed real time into whole frames. The original is a
    // fixed-step game - every speed it uses is a whole number of pixels per
    // frame - so integrating those against a variable dt would land atoms
    // between the lattice positions the descent actually uses.
    frameAccum_ += dt * kOriginalFps;
    int steps = static_cast<int>(frameAccum_);
    frameAccum_ -= static_cast<float>(steps);
    if (steps > 8) steps = 8;   // a stall must not teleport atoms

    for (int i = 0; i < steps && !gameOver_; ++i) stepFrame(buttons);
}

}  // namespace tubes
