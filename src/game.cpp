#include "game.h"

// PROVENANCE. Most of this file is now transliterated from `1000:3a67` and
// `1000:9e53` and says so at each site: the atom router and its arc tables,
// the spawn (period, column choice, type distribution), the difficulty seeds,
// the test tube's slide, and the Down/B speed boost.
//
// What is NOT from code, and is marked where it appears:
//
//   * scoring, which comes from the game's own Detailed Instructions - good
//     corroboration, but the match-and-clear routine is still unread;
//   * the chain bonus multiplier, deliberately unimplemented;
//   * kOriginalFps, assumed to be the PC timer's 18.2 Hz;
//   * the beaker as a single plane of types, which the original is not.
//
// The order of authority is in CLAUDE.md: decompiled code settles a rule, the
// game's text corroborates, measurement locates and validates but never
// derives. This file has already carried a scoring rule fitted to two observed
// awards that was simply wrong, and a spawn interval that was pure invention.

#include <algorithm>

namespace tubes {
namespace {

// Scoring, stated outright by the game's own Detailed Instructions:
//
//     Vertical                     250
//     Horizontal                   500
//     Diagonal (either direction) 1000
//
// The award depends on ORIENTATION ONLY - there is no length scaling - and
// that reproduces both live measurements exactly: a vertical run of 3 paid 250
// (seen twice), and a diagonal run of 4 paid 1000, not some multiple of it.
//
// An earlier version of this file fitted a curve, 250*(len-2)^2, through those
// same two points. It matched them only by coincidence, and it was unnecessary:
// the answer was already written down in the Instructions. Worth remembering
// before fitting anything again.
int awardForRun(const Run& r) {
    switch (r.kind) {
        case RunKind::kVertical:   return 250;
        case RunKind::kHorizontal: return 500;
        case RunKind::kDiagonal:   return 1000;
    }
    return 250;
}

// Length drives the CHAIN COUNT instead, which is what the HUD's "Chains"
// figure shows: "3 atom molecules count as 1 chain. 4 atom molecules count as
// 2 chains. 5 atom molecules count as 3 chains."
int chainsForRun(const Run& r) {
    return r.length - 2;
}

// The Instructions also say "forming multiple chains all at once will create a
// chain bonus point multiplier", but never say how large. It is deliberately
// NOT implemented: a 5-cell clear was observed paying exactly 1250, which is
// 250 + 1000 - two simultaneous chains summed with no multiplier at all. Until
// something distinguishes the two, plain summation is what the evidence shows.

// The score ramps toward its target instead of snapping. Measured on the
// original's own score variable, not a display layer: awards arrive as
// +166 then +834 for 1000, +41 then +209 for 250 - about a sixth per step.
constexpr int kScoreRampSteps = 6;

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

void Game::award(int points) {
    if (points <= 0) return;
    scorePending_ += points;
    // A larger award ramps in larger steps, so everything lands in about the
    // same number of frames rather than a big chain trickling in.
    const int step = (points + kScoreRampSteps - 1) / kScoreRampSteps;
    scoreStep_ = std::max(scoreStep_, step);
}

void Game::advanceScore() {
    if (scorePending_ <= 0) {
        scoreStep_ = 0;
        return;
    }
    const int step = std::min(scorePending_, std::max(1, scoreStep_));
    score_ += step;
    scorePending_ -= step;
    if (scorePending_ == 0) scoreStep_ = 0;
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
    advanceScore();

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

    // A tips the tube, dumping one atom into the beaker beneath it.
    if ((pressed & button::kA) && !tube_.empty()) {
        if (board_.drop(tubeColumn_, tube_.front())) {
            tube_.erase(tube_.begin());
            resolveMatches();
            if (board_.overflowing()) gameOver_ = true;
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
            // No velocity assignment here. The field's only writers are the
            // spawn and the Down/B boost in the caller, so an atom comes down
            // at exactly the speed it crossed the top at - or at 9 px/frame
            // for the rest of its flight if the player boosted it.
            a.accY += a.velocity;
            a.y += a.accY / kSubPixel;
            a.accY &= kSubPixel - 1;
            break;
    }

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

void Game::resolveMatches() {
    std::vector<uint8_t> marked;
    std::vector<Run> runs;

    // Clearing can drop atoms into new matches, so keep resolving.
    while (true) {
        int n = board_.findMatches(marked, &runs);
        if (n == 0) break;
        for (const Run& r : runs) {
            award(awardForRun(r));
            chains_ += chainsForRun(r);
        }
        board_.removeMarked(marked);
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
