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
constexpr int kTubeMouthY = 68;
constexpr int kLostY = 190;

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

Game::Game(int cols, int rows, Difficulty diff, uint32_t seed)
    : board_(cols, rows),
      tubeCapacity_(kTubeCapacity),
      dropsRemaining_(dropsFor(diff)),
      startingDrops_(dropsFor(diff)),
      spawnInterval_(kSpawnIntervalFrames[difficultyIndex(diff)]),
      networkVel_(velocityFor(diff)),
      rng_(seed ? seed : 1) {
    tubeColumn_ = cols / 2;
    spawnTimer_ = spawnInterval_;
    tube_.reserve(static_cast<size_t>(tubeCapacity_));
}

// xorshift32 - deterministic, so a fixed seed replays identically. The
// original calls a library `Random(n)`; only the distribution below is
// transliterated, not the generator.
int Game::random(int n) {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<int>(rng_ % static_cast<uint32_t>(n));
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

// One frame of the beaker: `1000:22a6`, plus the score ramp that `1000:58c5`
// drives around it. The whole thing runs every frame whether or not anything
// is happening, which is what animates the clear and the settle.
void Game::updateBeaker() {
    const BoardStep s = board_.step();

    if (s.award > 0) {
        scorePending_ += s.award;
        rampSteps_ = kScoreRampSteps;   // 1000:1c63  MOV [rampSteps], 6
        rampIncrement_ = 0;
        clearTimer_ = kClearFrames;     // 1000:1c70  MOV [clearTimer], 10
        pendingSound_ = s.soundType;
    }
    chains_ += s.chainsVertical + s.chainsHorizontal + s.chainsDiagonal;

    if (rampSteps_ > 0) {
        scoreMultiplier_ += s.runs;
        if (rampIncrement_ == 0) {
            rampIncrement_ = scorePending_ * scoreMultiplier_ / rampSteps_;
        }
        score_ += rampIncrement_;
        if (--rampSteps_ == 0) {
            // The flush pays the division's remainder, so the total is exactly
            // pending * multiplier however the sixths round.
            score_ += scorePending_ * scoreMultiplier_ % kScoreRampSteps;
            scorePending_ = 0;
            scoreMultiplier_ = 0;
            rampIncrement_ = 0;
        }
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

// One frame of the dispenser path. Speeds are the measured px/frame values,
// and each leg ends when it reaches its target rather than after a duration.
void Game::stepFrame(uint8_t buttons, uint8_t pressed) {
    updateBeaker();

    // --- the test tube, transliterated from 1000:4528 ------------------
    //
    // The whole input block sits inside `if state = 0`, so a direction is
    // only accepted when the tube is parked. That is why holding Left slides
    // one column at a time rather than accelerating: the next press is
    // ignored until the slide finishes.
    //
    //     if (btn and 4) and (index > 1) then           { Left }
    //         state := 1;  Dec(index);  target := stop[index]
    //     if (btn and 8) and (index < 6) then           { Right }
    //         state := 2;  Inc(index);  target := stop[index]
    //     case state of
    //       1: begin x := x - 6; if x <= target then begin x := target;
    //                                                     state := 0 end end
    //       2: begin x := x + 6; if x >= target then begin x := target;
    //                                                     state := 0 end end
    //
    // The port used to step a whole column every three frames instead, which
    // put the tube on a stop on every frame - so a capture that caught the
    // original mid-slide could never be matched.
    if (tubeState_ == 0) {
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
    if (tubeState_ == 1) {
        tubeX_ -= kTubeSlidePx;
        if (tubeX_ <= tubeTargetX_) { tubeX_ = tubeTargetX_; tubeState_ = 0; }
    } else if (tubeState_ == 2) {
        tubeX_ += kTubeSlidePx;
        if (tubeX_ >= tubeTargetX_) { tubeX_ = tubeTargetX_; tubeState_ = 0; }
    }

    // A tips the tube, dumping one atom into the beaker beneath it. Matching
    // is NOT resolved here: the beaker update runs every frame and picks the
    // new atom up on the next one, which is what lets the clear animate.
    if ((pressed & button::kA) && !tube_.empty()) {
        if (board_.drop(tubeColumn_, tube_.front())) {
            tube_.erase(tube_.begin());
        }
    }

    // Down or B speeds the atom heading for the column the tube is under.
    // The original dispatches on the tube's stop index through a chain of
    // comparisons - 1 to slot 3, 3 to slot 1, 4 to slot 6, 6 to slot 4, and
    // the rest to the same-numbered slot - which is just "the slot whose
    // destination x is where the tube is". Comparing the x values reproduces
    // it without hard-coding the permutation.
    if (buttons & (button::kDown | button::kB)) {
        for (int c = 1; c <= kAtomSlots; ++c) {
            if (atoms_[c].active() &&
                kAtomColumnX[atoms_[c].column] == playColumnX(tubeColumn_)) {
                atoms_[c].velocity = kBoostVel;
            }
        }
    }

    // The router runs over every slot, then the dispenser ticks. Both are
    // unconditional in the original - it does not wait for the network to
    // empty, which is why six atoms can be in flight at once.
    for (int c = 1; c <= kAtomSlots; ++c) {
        if (atoms_[c].active()) stepAtom(atoms_[c]);
    }

    if (--spawnTimer_ <= 0) spawn();
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
            a.accY += a.velocity;
            a.y += a.accY / kSubPixel;
            a.accY &= kSubPixel - 1;
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

    // The test tube catches an atom at the top lane, in its own column.
    // The atom's column is the original's 1..6 numbering and the board's is
    // 0..5, and they are not in the same order - kAtomColumnX runs
    // 143,125,107,197,179,161. Compare the x positions rather than the
    // indices, which sidesteps the mapping entirely.
    if (a.y >= kTubeMouthY &&
        kAtomColumnX[a.column] == playColumnX(tubeColumn_) &&
        !tubeFull()) {
        int8_t held = a.colour;
        if (held == kBonus) {
            // "Turns into Flashium when caught and awards you an extra drop"
            // - the game's own Instructions. The only way the pool ever grows.
            ++dropsRemaining_;
            held = kFlashium;
        }
        tube_.push_back(held);
        a.state = atomstate::kFree;
        return;
    }

    // Otherwise it falls past and is lost. A missed atom does NOT land in the
    // beaker - the beaker fills only from the tube, via Button A.
    if (a.y >= kLostY) {
        a.state = atomstate::kFree;
        if (dropsRemaining_ > 0) {
            --dropsRemaining_;
        } else {
            // Losing means dropping *more* atoms than allowed, so the
            // allowance is spent first and the next miss ends it.
            gameOver_ = true;
        }
    }
}

void Game::update(uint8_t buttons, float dt) {
    if (gameOver_) return;

    pendingPressed_ |= static_cast<uint8_t>(buttons & ~prevButtons_);
    prevButtons_ = buttons;

    // Convert elapsed real time into whole frames. The original is a
    // fixed-step game - every speed it uses is a whole number of pixels per
    // frame - so integrating those against a variable dt would land atoms
    // between the lattice positions the descent actually uses.
    frameAccum_ += dt * kOriginalFps;
    int steps = static_cast<int>(frameAccum_);
    frameAccum_ -= static_cast<float>(steps);
    if (steps > 8) steps = 8;   // a stall must not teleport atoms

    for (int i = 0; i < steps && !gameOver_; ++i) {
        // Only the first stepped frame sees the press edges, or holding a key
        // across several frames would register as several distinct taps.
        const uint8_t edge = (i == 0) ? pendingPressed_ : static_cast<uint8_t>(0);
        if (i == 0) pendingPressed_ = 0;
        stepFrame(buttons, edge);
    }
}

}  // namespace tubes
