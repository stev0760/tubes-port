// Game state: the dispenser, the player's test tube, and the beaker.
//
// Atoms spawn at the bottom right and trace up and over the tube arc, which
// gives the player a preview of the colours coming, then fall out of the tube
// into the play area. The test tube slides on a rail and holds several atoms
// stacked; A tips one into the beaker, B speeds an atom along.
//
// The arc is not implemented - atoms currently just fall. See PLAN.md.

#pragma once

#include <cstdint>
#include <vector>

#include "board.h"

namespace tubes {

// Matches the bit layout of a recorded .SCR demo, so a recording can be fed
// straight into the same update path as live input.
namespace button {
constexpr uint8_t kUp = 0x01;
constexpr uint8_t kDown = 0x02;
constexpr uint8_t kLeft = 0x04;
constexpr uint8_t kRight = 0x08;
constexpr uint8_t kA = 0x10;
constexpr uint8_t kB = 0x20;
}  // namespace button

enum class Difficulty {
    k101,   // 9 drops
    k201,   // 6 drops
    k301,   // 3 drops
};

struct Falling {
    bool active = false;
    int8_t colour = kEmpty;
    int column = 0;
    float y = 0.0f;     // pixels from the top of the play area
};

class Game {
public:
    Game(int cols, int rows, Difficulty diff, uint32_t seed);

    // `held` is edge-detected internally, so callers pass the raw button
    // state each frame rather than tracking presses themselves.
    void update(uint8_t buttons, float dt);

    const Board& board() const { return board_; }
    const Falling& falling() const { return falling_; }

    int tubeColumn() const { return tubeColumn_; }

    // The test tube holds up to five atoms, stacked. Index 0 is the mouth -
    // the one the next A press tips into the beaker. Five is not inferred from
    // sprite heights any more: the game's own Detailed Instructions state "The
    // test tube you control to collect and release atoms can hold up to 5
    // atoms at a time", with no mention of difficulty.
    const std::vector<int8_t>& tubeAtoms() const { return tube_; }
    int tubeCapacity() const { return tubeCapacity_; }
    bool tubeFull() const {
        return static_cast<int>(tube_.size()) >= tubeCapacity_;
    }
    int8_t heldAtom() const {
        return tube_.empty() ? static_cast<int8_t>(kEmpty) : tube_.front();
    }

    // Drops are a single pool that counts DOWN, not misses counting up. It is
    // seeded once per game from the difficulty (9/6/3), decremented by a miss,
    // incremented by a caught Bonus atom, and left alone by clearing a wave -
    // every one of those measured against the running original. The HUD shows
    // this number, labelled "Drops".
    int dropsRemaining() const { return dropsRemaining_; }
    int startingDrops() const { return startingDrops_; }

    // The displayed score ramps toward the awarded total rather than jumping;
    // the original's own score variable does this, in roughly sixths.
    int score() const { return score_; }
    int scoreTarget() const { return score_ + scorePending_; }

    int chains() const { return chains_; }
    bool gameOver() const { return gameOver_; }

    // Height of the play area in pixels; the dispenser drops across it.
    void setFallHeight(float h) { fallHeight_ = h; }

private:
    void spawn();
    void resolveMatches();
    int8_t nextColour();
    void award(int points);
    void advanceScore();

    Board board_;
    Falling falling_;

    int tubeColumn_ = 0;
    std::vector<int8_t> tube_;
    int tubeCapacity_ = 5;

    int dropsRemaining_ = 9;
    int startingDrops_ = 9;
    int score_ = 0;
    int scorePending_ = 0;    // awarded but not yet ramped in
    int scoreStep_ = 0;
    int chains_ = 0;
    bool gameOver_ = false;

    float fallSpeed_ = 34.0f;    // pixels per second
    float fallHeight_ = 130.0f;
    float spawnTimer_ = 0.0f;

    // Held directions repeat, so the tube keeps sliding rather than moving a
    // single column per press.
    float moveTimer_ = 0.0f;

    uint8_t prevButtons_ = 0;
    uint32_t rng_ = 1;
};

}  // namespace tubes
