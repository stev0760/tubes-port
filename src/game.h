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

// Which leg of the dispenser path an atom is on. The route was measured by
// sampling the live atom array over time - static analysis never found the
// code that moves atoms, and did not need to:
//
//     spawn -> bottom of an outer vertical tube (y = 187)
//           -> ascends, x constant
//           -> crosses the top (y ~ 0)
//           -> descends into a play column
//
enum class Leg : uint8_t { kRise, kCross, kDescend };

struct Falling {
    bool active = false;
    int8_t colour = kEmpty;
    Leg leg = Leg::kRise;
    // Original screen pixels, the same space the beaker geometry uses, so a
    // position here can be compared straight against a captured trace.
    float x = 0.0f;
    float y = 0.0f;
    int column = 0;     // destination play column, 0..5
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
    // The original moves things a whole number of pixels per frame, so the
    // simulation steps in frames and `update()` only converts real time into
    // them.
    void stepFrame(uint8_t buttons, uint8_t pressed);

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

    float fallHeight_ = 130.0f;   // retained for callers; unused by the path
    int spawnTimer_ = 0;          // frames since the last dispense
    float frameAccum_ = 0.0f;     // real time carried between frames

    // The tube slides at 6 px/frame over an 18 px column pitch, so a column
    // change takes three frames.
    int moveTimer_ = 0;

    uint8_t prevButtons_ = 0;
    // Press edges seen since the last frame was stepped. The caller may update
    // faster than the fixed step, so a press can arrive on a call that steps no
    // frames; without holding it here that press would be swallowed.
    uint8_t pendingPressed_ = 0;
    uint32_t rng_ = 1;
};

}  // namespace tubes
