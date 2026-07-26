#include "game.h"

#include <algorithm>
#include <cmath>

namespace tubes {
namespace {

// A chain of 3 scores the base; every extra atom in the run is worth more,
// so long runs pay disproportionately. Provisional - the original tables have
// not been recovered.
constexpr int kScorePerAtom = 10;
constexpr int kChainBonus = 25;

constexpr float kSpawnInterval = 1.6f;   // seconds between dispensed atoms
constexpr float kFastFallScale = 3.0f;   // holding Down accelerates the atom
constexpr float kMoveRepeat = 0.09f;     // seconds between repeats while held

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
      dropLimit_(dropsFor(diff)),
      fallSpeed_(speedFor(diff)),
      rng_(seed ? seed : 1) {
    tubeColumn_ = cols / 2;
}

int8_t Game::nextColour() {
    // xorshift32 - deterministic, so a fixed seed replays identically.
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<int8_t>(rng_ % kAtomCount);
}

void Game::spawn() {
    falling_.active = true;
    falling_.colour = nextColour();
    falling_.column = static_cast<int>(rng_ % board_.cols());
    falling_.y = 0.0f;
}

void Game::resolveMatches() {
    std::vector<uint8_t> marked;
    int chain = 0;

    // Clearing can drop atoms into new matches, so keep resolving.
    while (true) {
        int n = board_.findMatches(marked);
        if (n == 0) break;
        ++chain;
        score_ += n * kScorePerAtom + (chain - 1) * kChainBonus;
        board_.removeMarked(marked);
    }

    chains_ += chain;
}

void Game::update(uint8_t buttons, float dt) {
    if (gameOver_) return;

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

    // Release a held atom into the beaker beneath the tube.
    if ((pressed & button::kA) && held_ != kEmpty) {
        if (board_.drop(tubeColumn_, held_)) {
            held_ = kEmpty;
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

    float speed = fallSpeed_;
    if (buttons & button::kDown) speed *= kFastFallScale;
    falling_.y += speed * dt;

    if (falling_.y < fallHeight_) return;

    // The atom has reached tube height. Catching it requires the tube to be
    // in the right column and empty; anything else counts against the drop
    // limit, per the manual.
    const bool caught =
        (falling_.column == tubeColumn_) && (held_ == kEmpty);
    if (caught) {
        held_ = falling_.colour;
    } else {
        ++drops_;
        if (drops_ > dropLimit_) gameOver_ = true;
    }

    falling_.active = false;
    falling_.y = 0.0f;
}

}  // namespace tubes
