#include "game.h"

// WARNING ON PROVENANCE. The rules in this file are **behavioural
// reconstructions**, not ported code. They were arrived at by watching the
// original run and writing C++ that reproduces what was seen, which is exactly
// the "just rewrite it" approach CLAUDE.md rejects, and it is lossy in a way
// that is easy to miss: observation gives samples, the binary gives the
// function. This session alone produced a scoring rule fitted to two data
// points that was simply wrong, and a wildcard rule that was wrong until a
// player said so.
//
// Treat every constant and rule here as a placeholder to be REPLACED by the
// decompiled logic from 1000:3a67, not as a finished result. The measured
// traces are still worth keeping - they become the oracle the decompiled
// version has to reproduce.

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

constexpr int kTubeSlideFrames = 3;      // 18 px pitch at the tube's 6 px/frame

// The lanes an atom travels between. y = 187 is where atoms enter at the
// bottom of a feed tube and y = 68 is the top lane where the test tube can
// catch; both measured.
constexpr int kEntryY = 187;
constexpr int kTopY = 0;
constexpr int kTubeMouthY = 68;
constexpr int kLostY = 190;

// Feed tubes, by side. Which tube feeds which column has NOT been recovered -
// only that left-hand tubes serve columns 1..3 and right-hand ones 4..6, since
// the router picks its turn direction with `column < 4`.
const int kFeedLeft[] = {34, 58};
const int kFeedRight[] = {246, 270, 294};

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

// How often an atom is dispensed. NOT measured - the demo trace could not
// resolve it, and the number below is only a playable placeholder. Flagged in
// PLAN.md rather than quietly left to look like a finding.
constexpr int kSpawnIntervalFrames = 29;

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
int dropsFor(Difficulty d) {
    switch (d) {
        case Difficulty::k101: return 9;
        case Difficulty::k201: return 6;
        case Difficulty::k301: return 3;
    }
    return 9;
}

// Difficulty also sets atom speed - the binary's difficulty routine does more
// than pick the drop allowance - but the per-difficulty values have NOT been
// measured. The px/frame constants above were all taken at one setting, so no
// multiplier is applied rather than inventing three numbers. See PLAN.md.

}  // namespace

Game::Game(int cols, int rows, Difficulty diff, uint32_t seed)
    : board_(cols, rows),
      tubeCapacity_(kTubeCapacity),
      dropsRemaining_(dropsFor(diff)),
      startingDrops_(dropsFor(diff)),
      rng_(seed ? seed : 1) {
    tubeColumn_ = cols / 2;
    tube_.reserve(static_cast<size_t>(tubeCapacity_));
}

int8_t Game::nextColour() {
    // xorshift32 - deterministic, so a fixed seed replays identically.
    //
    // Only the seven ordinary colours are dispensed. The specials (Flashium,
    // AntiMatter, Bonus, ...) plainly do appear in the original, but at rates
    // that have not been measured, and inventing a rate would put a made-up
    // number where a measured one belongs. See PLAN.md.
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<int8_t>(kFirstColour + rng_ % kColourCount);
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

void Game::spawn() {
    falling_ = Falling{};
    falling_.active = true;
    falling_.colour = nextColour();
    // Columns are 1..6 in the original's own numbering, matching the DS:0x18
    // table. A left-hand column is fed from a left-hand tube so the atom
    // turns right, and vice versa - that pairing is what `column < 4` in the
    // router encodes. WHICH tube feeds which column is not yet recovered.
    falling_.column = 1 + static_cast<int>(rng_ % 6);
    if (falling_.column < 4) {
        const size_t n = sizeof(kFeedLeft) / sizeof(kFeedLeft[0]);
        falling_.x = kFeedLeft[rng_ % n];
    } else {
        const size_t n = sizeof(kFeedRight) / sizeof(kFeedRight[0]);
        falling_.x = kFeedRight[rng_ % n];
    }
    falling_.anchorX = falling_.x;
    falling_.y = kEntryY;
    falling_.targetY = kTopY;
    falling_.state = atomstate::kRise;
    falling_.velocity = kNetworkVel;
}

// One frame of the dispenser path. Speeds are the measured px/frame values,
// and each leg ends when it reaches its target rather than after a duration.
void Game::stepFrame(uint8_t buttons, uint8_t pressed) {
    advanceScore();

    // Move the test tube. A press moves immediately; holding repeats, so the
    // tube slides instead of stopping after one column.
    const int dir = (buttons & button::kLeft)    ? -1
                    : (buttons & button::kRight) ? 1
                                                 : 0;
    if (dir == 0) {
        moveTimer_ = 0;
    } else {
        const bool fresh = (pressed & (button::kLeft | button::kRight)) != 0;
        if (fresh) moveTimer_ = 0;
        if (fresh || --moveTimer_ <= 0) {
            tubeColumn_ = std::clamp(tubeColumn_ + dir, 0, board_.cols() - 1);
            moveTimer_ = kTubeSlideFrames;
        }
    }

    // A tips the tube, dumping one atom into the beaker beneath it.
    if ((pressed & button::kA) && !tube_.empty()) {
        if (board_.drop(tubeColumn_, tube_.front())) {
            tube_.erase(tube_.begin());
            resolveMatches();
            if (board_.overflowing()) gameOver_ = true;
        }
    }

    if (!falling_.active) {
        if (++spawnTimer_ >= kSpawnIntervalFrames) {
            spawnTimer_ = 0;
            spawn();
        }
        return;
    }

    // --- FUN_1000_0f80, transliterated -----------------------------------
    Falling& a = falling_;
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
            if (a.y < a.targetY + 9) {
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
                a.velocity = kDescendVel;
            }
            if (a.x > a.anchorX - 9) {
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
                a.velocity = kDescendVel;
            }
            if (a.x < a.anchorX + 9) {
                a.y = a.targetY + crossArcOffset(a.x - a.anchorX);
            }
            break;
        }

        case atomstate::kDescend:
            // Down and B speed an atom along - confirmed from the input bit
            // map, where the demo holds Down for long stretches. The boosted
            // and unboosted descent rates are measured (18 and 4 px/frame);
            // the code that sets the velocity field has not been located, so
            // this assignment is inferred, unlike the states above.
            a.velocity = (buttons & (button::kDown | button::kB))
                             ? kDescendVel
                             : kNetworkVel;
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
    if (falling_.y >= kTubeMouthY &&
        kAtomColumnX[falling_.column] == playColumnX(tubeColumn_) &&
        !tubeFull()) {
        int8_t held = falling_.colour;
        if (held == kBonus) {
            // "Turns into Flashium when caught and awards you an extra drop"
            // - the game's own Instructions. The only way the pool ever grows.
            ++dropsRemaining_;
            held = kFlashium;
        }
        tube_.push_back(held);
        falling_.active = false;
        return;
    }

    // Otherwise it falls past and is lost. A missed atom does NOT land in the
    // beaker - the beaker fills only from the tube, via Button A.
    if (falling_.y >= kLostY) {
        falling_.active = false;
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
