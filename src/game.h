// Game state: the dispenser, the player's test tube, and the beaker.
//
// Atoms enter at the foot of one of six feed tubes, rise, cross the top along
// that tube's lane and come back down a play column - tracing the artwork
// rather than falling straight. The test tube slides on a rail and holds
// atoms stacked; A tips one into the beaker, B speeds an atom along.
//
// Six atoms are in flight at once, one per column, because the original's
// array is indexed by the column. See `Falling` and PLAN.md.

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

// Atom movement states, transliterated from FUN_1000_0f80 - the router the
// game loop calls once per atom per frame. These are the original's own state
// numbers, kept so a live record can be compared against this struct directly.
namespace atomstate {
constexpr uint8_t kFree = 0;      // slot unused; the spawn looks for this
constexpr uint8_t kRise = 3;      // up a feed tube, y decreasing
constexpr uint8_t kGoLeft = 5;    // across the top, x decreasing
constexpr uint8_t kGoRight = 6;   // across the top, x increasing
constexpr uint8_t kDescend = 7;   // down a play column, y increasing
}  // namespace atomstate

// The playfield geometry lives in four consecutive six-word tables in DGROUP,
// which is why no tube x value ever appears in a comparison in the game loop.
// Read out of the image at DGROUP:0x00 rather than inferred:
//
//     DS:0x00  feed x    10  34  58 246 270 294   the tube an atom enters at
//     DS:0x0c  lane y    26  13   0   0  13  26   the height it crosses at
//     DS:0x18  column x 143 125 107 197 179 161   where it comes back down
//     DS:0x24  tube x   104 122 140 158 176 194   the test tube's six stops
//
// All four are indexed by the column, 1..6. Column 1's feed x was carried as
// "inferred from the mirror symmetry" for a session because no atom happened
// to use that column while sampling; it is measured now.
constexpr int kFeedX[7] = {0, 10, 34, 58, 246, 270, 294};
constexpr int kLaneY[7] = {0, 26, 13, 0, 0, 13, 26};
constexpr int kAtomColumnX[7] = {0, 143, 125, 107, 197, 179, 161};
constexpr int kTubeStopX[7] = {0, 104, 122, 140, 158, 176, 194};

// Velocity is stored in 1/128 pixel per frame, in the record at +0x09. The
// spawn copies it from a per-session variable that `1000:9e53` seeds from the
// difficulty, so it is not one constant:
//
//     Tubes 101  0x100 = 256 = 2 px/frame
//     Tubes 201  0x180 = 384 = 3 px/frame
//     Tubes 301  0x200 = 512 = 4 px/frame
//
// and every fifteenth wave adds 0x20 - a quarter of a pixel a frame. The 512
// a live record once read was a late wave on an easy setting, not the base.
constexpr int kSubPixel = 128;

// Nine pixels a frame. Two things use it, and both are per-frame rather than
// sticky, because `1000:1906` - the last thing the router does to every atom -
// reloads the velocity from the session's base value every single frame:
//
//   * Down or B sets it on ONE atom, the one heading for the column the test
//     tube is parked under. The boost therefore lasts exactly one frame and
//     the player has to HOLD the button.
//   * the Bonus atom, type 10, gets it unconditionally in that same reload -
//     which is why GOLDBALL travels the tube visibly faster than anything
//     else.
//
// An earlier version made the boost permanent, on the grounds that the field
// had "exactly three writers" and none of them reset it. The search covered
// `1000:3a67` and the fourth writer is in `1000:0f80`.
constexpr int kBoostVel = 0x480;

// The test tube slides a flat 6 pixels a frame between its stops.
constexpr int kTubeSlidePx = 6;

// The dispenser's period, in frames, from the same seeding block: 70/60/50 by
// difficulty. It shortens by one per wave and lengthens by twelve every
// fifteenth, so it drifts down about three per fifteen waves.
constexpr int kSpawnIntervalFrames[3] = {70, 60, 50};

// One entry of the original's `array[1..12] of AtomRec` - 28 bytes, living in
// `1000:3a67`'s own frame at [BP-0x1c6]. Every field offset below is read off
// the spawn at `1000:49af`, which writes the record whole:
//
//     +0x00 x        +0x02 y        +0x04 anchor x   +0x06 target y
//     +0x08 state    +0x09 velocity +0x0b type       +0x0c column
//     +0x10/+0x12 the fixed-point accumulators
//     +0x14/+0x16 saved x, +0x18/+0x1a saved y - one pair per video page
//
// `state == 0` means the slot is free; the renderer draws a record only when
// `state > 2`, which is the same test the original makes at every draw site.
struct Falling {
    int8_t colour = kEmpty;
    int x = 0;
    int y = 0;
    int anchorX = 0;      // column base the arc offsets are measured from
    int targetY = 0;
    uint8_t state = atomstate::kFree;
    int column = 1;       // destination column, 1..6
    int velocity = 0;
    int accX = 0;
    int accY = 0;
    // True while the router is applying its corner offset, i.e. the atom is
    // rounding a bend rather than running along a straight pipe. Kept because
    // the arc offsets are what the router computes, not because anything is
    // painted over the atom - the original paints nothing over it.
    bool onArc = false;

    bool active() const { return state != atomstate::kFree; }
    bool drawn() const { return state > 2; }
};

// The network holds six atoms at once, one per column, and the slot index IS
// the column. That is not an implementation choice: the spawn at `1000:4967`
// indexes the array by the column it rolled, and every one of the six draw
// sites in the frame is hard-coded to one slot. See `kAtomSlots` uses in
// main.cpp for why the rendering depends on it.
constexpr int kAtomSlots = 6;

class Game {
public:
    Game(int cols, int rows, Difficulty diff, uint32_t seed);

    // `held` is edge-detected internally, so callers pass the raw button
    // state each frame rather than tracking presses themselves.
    void update(uint8_t buttons, float dt);

    const Board& board() const { return board_; }

    // The atom travelling column `col`, 1..6. Always valid; check `active()`.
    const Falling& atom(int col) const { return atoms_[col]; }

    // The tube's stop, 0..5, and its actual x - which is between two stops
    // while it is sliding. Renderers want the x; the board wants the stop.
    int tubeColumn() const { return tubeColumn_; }
    int tubeX() const { return tubeX_; }

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
    // the original's own score variable does this, in sixths.
    int score() const { return score_; }
    int scoreTarget() const {
        return score_ + scorePending_ * scoreMultiplier_;
    }
    // Non-zero while a clear animation is running.
    int clearTimer() const { return clearTimer_; }
    // The fade family whose sound to play, consumed by the caller.
    int8_t takeSound() {
        const int8_t s = pendingSound_;
        pendingSound_ = kEmpty;
        return s;
    }

    int chains() const { return chains_; }
    bool gameOver() const { return gameOver_; }

    // Height of the play area in pixels; the dispenser drops across it.
    void setFallHeight(float h) { fallHeight_ = h; }

    // --- state injection, for the pixel-diff harness --------------------
    // Reproducing a frame captured from the original requires putting this
    // engine into the original's exact state rather than simulating up to
    // something similar. Comparing two runs that merely look alike cannot
    // tell a rendering bug from a divergence in the simulation.
    Board& boardMutable() { return board_; }
    void setTubeColumn(int c) { tubeColumn_ = c; tubeX_ = kTubeStopX[c + 1]; }
    // Pin the tube mid-slide, which is where a paused capture often finds it.
    void setTubeX(int x) { tubeX_ = x; }
    void setAtom(int col, const Falling& f) { atoms_[col] = f; }
    void setTubeAtoms(const std::vector<int8_t>& v) { tube_ = v; }

private:
    void spawn();
    void stepAtom(Falling& a);
    void updateBeaker();
    int random(int n);
    int8_t nextColour();
    // The original moves things a whole number of pixels per frame, so the
    // simulation steps in frames and `update()` only converts real time into
    // them.
    void stepFrame(uint8_t buttons, uint8_t pressed);

    Board board_;
    // Index 1..6 by column; [0] is never used, matching the Pascal array.
    Falling atoms_[kAtomSlots + 1];

    // The test tube's own record: x at +0x00, state at +0x04, stop index at
    // +0x1e, target x at +0x1f. State 0 is parked and is the ONLY state that
    // accepts input; 1 and 2 are sliding left and right.
    int tubeColumn_ = 0;          // the original's +0x1e, less one
    int tubeX_ = kTubeStopX[1];
    int tubeTargetX_ = kTubeStopX[1];
    uint8_t tubeState_ = 0;
    std::vector<int8_t> tube_;
    int tubeCapacity_ = 5;

    int dropsRemaining_ = 9;
    int startingDrops_ = 9;
    int score_ = 0;
    // The score ramp, from 1000:2410 and the flush at 1000:58c5. `pending` is
    // this clear's award, `multiplier` the number of distinct runs, and the
    // total paid is their product, spread over six frames.
    int scorePending_ = 0;
    int scoreMultiplier_ = 0;
    int rampSteps_ = 0;
    int rampIncrement_ = 0;
    // 1000:1c70 sets this to 10 on a match; the frame loop counts it down and
    // will not declare a wave complete while it is running.
    int clearTimer_ = 0;
    int8_t pendingSound_ = kEmpty;
    int chains_ = 0;
    bool gameOver_ = false;

    float fallHeight_ = 130.0f;   // retained for callers; unused by the path
    // Counts DOWN to the next dispense and is reloaded from the interval, the
    // way `1000:490a` does it, rather than counting up to a threshold.
    int spawnTimer_ = 1;
    int spawnInterval_ = kSpawnIntervalFrames[0];
    int networkVel_ = 2 * kSubPixel;
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
