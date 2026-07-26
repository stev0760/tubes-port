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

// Atom motion, in pixels per frame, measured by sampling the live atom array.
// The original moves whole pixels per frame, which is why the simulation steps
// in frames rather than integrating a velocity.
constexpr float kNetworkSpeed = 4.0f;    // up the outer tubes and across the top
constexpr float kDescendSpeed = 18.0f;   // falling down a play column
constexpr int kTubeSlideFrames = 3;      // 18 px pitch at the tube's 6 px/frame

// The measured route. Atoms enter at the bottom of an outer vertical tube and
// rise; the tube x positions come from observed dwell points, and the entry
// lane y = 187 and top lane y = 68 are both measured.
constexpr float kEntryY = 187.0f;
constexpr float kTopY = 0.0f;
constexpr float kTubeMouthY = 68.0f;     // where the test tube can catch
constexpr float kLostY = 190.0f;         // past the beaker rim: the atom is gone
const float kEntryColumns[] = {34.0f, 58.0f, 246.0f, 270.0f, 294.0f};

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
    falling_.active = true;
    falling_.colour = nextColour();
    falling_.column = static_cast<int>(rng_ % board_.cols());
    falling_.leg = Leg::kRise;
    const size_t n = sizeof(kEntryColumns) / sizeof(kEntryColumns[0]);
    falling_.x = kEntryColumns[rng_ % n];
    falling_.y = kEntryY;
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

    const float targetX = static_cast<float>(playColumnX(falling_.column));

    switch (falling_.leg) {
        case Leg::kRise:
            falling_.y -= kNetworkSpeed;
            if (falling_.y <= kTopY) {
                falling_.y = kTopY;
                falling_.leg = Leg::kCross;
            }
            break;

        case Leg::kCross: {
            const float d = targetX - falling_.x;
            if (std::fabs(d) <= kNetworkSpeed) {
                falling_.x = targetX;
                falling_.leg = Leg::kDescend;
            } else {
                falling_.x += (d > 0 ? kNetworkSpeed : -kNetworkSpeed);
            }
            break;
        }

        case Leg::kDescend:
            // Down and B both speed an atom along, confirmed from the input
            // bit map: the demo holds Down for long stretches.
            falling_.y += (buttons & (button::kDown | button::kB))
                              ? kDescendSpeed
                              : kNetworkSpeed;
            break;
    }

    if (falling_.leg != Leg::kDescend) return;

    // The test tube catches an atom at the top lane, in its own column.
    if (falling_.y >= kTubeMouthY && falling_.column == tubeColumn_ &&
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
