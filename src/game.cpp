#include "game.h"

#include <algorithm>
#include <cmath>

namespace tubes {
namespace {

// Scoring. Two awards are measured against the running original:
//
//   a VERTICAL run of 3   ->  250     (seen twice in the demo trace)
//   a DIAGONAL run of 4   -> 1000     (recorded during live play)
//
// Those are the only two awards tied to a known run, and they differ in BOTH
// orientation and length - so the two variables are confounded and neither
// "diagonals pay 4x" nor "length drives the award" is established. The formula
// below is the simplest curve through both points, 250*(len-2)^2, giving
// 250 / 1000 / 2250 for runs of 3 / 4 / 5. It reproduces every measurement
// taken so far and is otherwise a guess; a run of 5, or a horizontal 4, would
// separate the two explanations in a single observation.
constexpr int kChainUnit = 250;

int awardForRun(const Run& r) {
    const int over = r.length - 2;          // 1 for a run of 3
    return kChainUnit * over * over;
}

// The score ramps toward its target instead of snapping. Measured on the
// original's own score variable, not a display layer: awards arrive as
// +166 then +834 for 1000, +41 then +209 for 250 - about a sixth per step.
constexpr int kScoreRampSteps = 6;

constexpr float kSpawnInterval = 1.6f;   // seconds; NOT measured
constexpr float kFastFallScale = 3.0f;   // holding Down accelerates the atom
constexpr float kMoveRepeat = 0.09f;     // seconds between repeats while held

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

float speedFor(Difficulty d) {
    switch (d) {
        case Difficulty::k101: return 30.0f;
        case Difficulty::k201: return 42.0f;
        case Difficulty::k301: return 56.0f;
    }
    return 30.0f;
}

}  // namespace

Game::Game(int cols, int rows, Difficulty diff, uint32_t seed)
    : board_(cols, rows),
      tubeCapacity_(kTubeCapacity),
      dropsRemaining_(dropsFor(diff)),
      startingDrops_(dropsFor(diff)),
      fallSpeed_(speedFor(diff)),
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
    falling_.active = true;
    falling_.colour = nextColour();
    falling_.column = static_cast<int>(rng_ % board_.cols());
    falling_.y = 0.0f;
}

void Game::resolveMatches() {
    std::vector<uint8_t> marked;
    std::vector<Run> runs;
    int chain = 0;

    // Clearing can drop atoms into new matches, so keep resolving.
    while (true) {
        int n = board_.findMatches(marked, &runs);
        if (n == 0) break;
        ++chain;
        for (const Run& r : runs) award(awardForRun(r));
        board_.removeMarked(marked);
    }

    chains_ += chain;
}

void Game::update(uint8_t buttons, float dt) {
    if (gameOver_) return;

    advanceScore();

    const uint8_t pressed = static_cast<uint8_t>(buttons & ~prevButtons_);
    prevButtons_ = buttons;

    // Move the test tube. A press moves immediately; holding the direction
    // repeats, so the tube slides instead of stopping after one column.
    const int dir = (buttons & button::kLeft)    ? -1
                    : (buttons & button::kRight) ? 1
                                                 : 0;
    if (dir == 0) {
        moveTimer_ = 0.0f;
    } else {
        const bool fresh = (pressed & (button::kLeft | button::kRight)) != 0;
        if (fresh) moveTimer_ = 0.0f;
        moveTimer_ -= dt;
        if (fresh || moveTimer_ <= 0.0f) {
            tubeColumn_ = std::clamp(tubeColumn_ + dir, 0, board_.cols() - 1);
            moveTimer_ = kMoveRepeat;
        }
    }

    // A tips the tube, dumping one atom into the beaker beneath it. The tube
    // holds several, so each press releases only the one at the mouth.
    if ((pressed & button::kA) && !tube_.empty()) {
        if (board_.drop(tubeColumn_, tube_.front())) {
            tube_.erase(tube_.begin());
            resolveMatches();
            if (board_.overflowing()) gameOver_ = true;
        }
    }

    if (!falling_.active) {
        spawnTimer_ += dt;
        if (spawnTimer_ >= kSpawnInterval) {
            spawnTimer_ = 0.0f;
            spawn();
        }
        return;
    }

    // B sucks the atom along faster; Down does the same, pending the real
    // dispenser being understood.
    float speed = fallSpeed_;
    if (buttons & (button::kDown | button::kB)) speed *= kFastFallScale;
    falling_.y += speed * dt;

    if (falling_.y < fallHeight_) return;

    // The atom has reached tube height. Catching it requires the tube to be
    // in the right column and to have room; anything else is a "drop" and
    // costs one from the pool.
    const bool caught = (falling_.column == tubeColumn_) && !tubeFull();
    if (caught) {
        int8_t held = falling_.colour;
        if (held == kBonus) {
            // "Turns into Flashium when caught and awards you an extra drop"
            // - the game's own Instructions. The only way the pool ever grows.
            ++dropsRemaining_;
            held = kFlashium;
        }
        tube_.push_back(held);
    } else if (dropsRemaining_ > 0) {
        --dropsRemaining_;
    } else {
        // Losing means dropping *more* atoms than allowed, so the allowance is
        // spent first and the next miss ends it.
        gameOver_ = true;
    }

    falling_.active = false;
    falling_.y = 0.0f;
}

}  // namespace tubes
