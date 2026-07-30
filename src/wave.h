// Wave mode: the objective a wave sets, and how the wave number selects it.
//
// Endurance runs one open-ended session; Wave mode runs 75 numbered waves,
// each with an objective and often a modifier. The whole system is one screen
// and twenty-five procedures in the original:
//
//     1000:86b8   the briefing screen. Its body is the WAVE TABLE - an
//                 if-chain 75 arms long on the wave number, one arm per wave,
//                 each calling one objective routine.
//     1000:62f1                   the 25 objective routines. Each sets
//        ..      1000:8581        DS:0x1d4e (the mode), the live counter, and
//                                 whichever of the colour, orientation,
//                                 rotation and modifier fields it needs.
//     1000:a4cd   the six counters the objectives read, seeded as literals.
//     1000:a616   the progression that steps them, once per wave CLEARED.
//     1000:192f   the scoring hook every matcher calls.
//
// Nothing here is derived from the wave number: the table is data in the
// binary and this file transcribes it. See docs/reversing-notes.md, "Wave
// mode, decompiled".

#pragma once

#include <cstdint>
#include <functional>

#include "board.h"

namespace tubes {

// `DS:0x1d4e`, and these are the original's own numbers - they are compared
// against literals all over `1000:3a67`, so renumbering them would be a lie.
//
// 0 and 1 are the two modes that never see a briefing (`1000:a5d2` skips it
// for both). 2..6 are set by the objective routines themselves, which is why
// the Game Mode menu's "Wave" arm at `1b2e:4e85` can set 2 and be overwritten
// on the first briefing.
enum class WaveMode : uint8_t {
    kDemo = 0,          // attract mode; `1000:b268` and `b29a`
    kEndurance = 1,     // `1b2e:4e51`
    kOrientation = 2,   // form N chains of a given orientation, any colour
    kColour = 3,        // form N chains of a given colour, maybe a given chain
    kSurvive = 4,       // live through N atoms
    kCrystals = 5,      // remove N Mischief Crystals with AntiMatter
    kMarked = 6,        // clear N marked atoms
};

inline bool isWaveMode(WaveMode m) { return static_cast<uint8_t>(m) >= 2; }

// The 25 objective routines, named after what their briefing says. The address
// is the routine; the mode is what it writes to `DS:0x1d4e`.
enum class Objective : uint8_t {
    kMarked,             // 1000:62f1  mode 6
    kMarkedCovered,      // 1000:643b  mode 6
    kMarkedXenon,        // 1000:6592  mode 6
    kCrystals,           // 1000:66cb  mode 5
    kFlashium,           // 1000:67d8  mode 3
    kShownAtom,          // 1000:68b7  mode 3
    kHorizontalColour,   // 1000:69e4  mode 3
    kVerticalColour,     // 1000:6b5d  mode 3
    kDiagonalColour,     // 1000:6cd6  mode 3
    kSurvive,            // 1000:6ede  mode 4
    kSurviveHidden,      // 1000:6fd5  mode 4
    kSurviveDisabled,    // 1000:7100  mode 4
    kTaskColour,         // 1000:72ad  mode 3
    kTaskChain,          // 1000:744d  mode 3
    kTaskBoth,           // 1000:764b  mode 3
    kTaskColourTimed,    // 1000:7802  mode 3
    kTaskChainTimed,     // 1000:798b  mode 3
    kTaskBothTimed,      // 1000:7b72  mode 3
    kAnyAtom,            // 1000:7cce  mode 3
    kAnyAtomPrefill,     // 1000:7de3  mode 3
    kAnyAtomMorph,       // 1000:7f42  mode 3
    kHorizontalAny,      // 1000:8056  mode 2
    kVerticalAny,        // 1000:81bb  mode 2
    kDiagonalAny,        // 1000:8320  mode 2
    kMystery,            // 1000:8581  - picks one of four at random
};

constexpr int kWaveCount = 75;

// `1000:86b8`'s dispatch. Waves are 1-based; anything outside 1..75 has no arm
// in the original either, and returns `kAnyAtom` here rather than reading off
// the end of the table.
Objective objectiveForWave(int wave);

// The 720-frame timer seeded at `1000:3b00` and reloaded at `1000:4b6d`. It is
// what the briefings call "every 45 seconds".
constexpr int kTaskTimerFrames = 720;

// The chain orientation codes the matchers pass to `1000:192f`. Fixed three
// ways over: by `{69e4, 8056} = 1`, `{6b5d, 81bb} = 2`, `{6cd6, 8320} = 0`,
// and by the rotation at `1000:4b86` wrapping 2 back to 0.
namespace chaincode {
constexpr uint8_t kDiagonal = 0;
constexpr uint8_t kHorizontal = 1;
constexpr uint8_t kVertical = 2;
}  // namespace chaincode

inline uint8_t chainCodeOf(RunKind k) {
    switch (k) {
        case RunKind::kHorizontal: return chaincode::kHorizontal;
        case RunKind::kVertical:   return chaincode::kVertical;
        case RunKind::kDiagonal:   return chaincode::kDiagonal;
    }
    return chaincode::kDiagonal;
}

// The counters a briefing reads. They belong to the SESSION, not the wave:
// `1000:a4cd` seeds them once as immediates on a new game, `1000:a525` reads
// the same fields out of a save instead, and `1000:a616` steps them after each
// wave the player clears.
//
// The six literals - 3, 30, 2, 0, 3, 8 - are the numbers PLAN.md carried for
// two sessions as "difficulty seeds, variables not yet named".
struct WaveProgress {
    int wave = 1;              // -0x170

    int chainTargetChain = 3;  // -0x183, when the ORIENTATION is required
    int atomTarget = 30;       // -0x182
    int chainTargetColour = 2; // -0x184, when a colour, or nothing, is required
    int crystals = 0;          // -0x185, and `1000:66cb` increments it itself
    int marked = 3;            // -0x186
    int preFill = 8;           // -0x17b, atoms the pre-filled beaker starts with

    int interval = 70;         // -0x181, frames between dispenses
    int velocity = 256;        // -0x180, 1/128 px per frame

    // `1000:a616`, and it runs ONLY when the wave was cleared - a Continue
    // jumps past it at `1000:a60f`, which is why a retried wave is not harder.
    void advance();

    // True once the wave passes the table's last arm. `1000:a657` shows the
    // end-of-game screen at 75 and increments anyway.
    bool finished() const { return wave > kWaveCount; }
};

// What one briefing leaves behind: `1000:9e53`'s `-0x1ef` .. `-0x1ff` block,
// plus the four modifier bytes at `-0x187` .. `-0x18a`.
//
// `1000:86b8` zeroes exactly twelve of these before dispatching and leaves the
// counter, the colour and the chain alone - which is how a Continue replays a
// wave with the objective it already had.
struct WaveObjective {
    WaveMode mode = WaveMode::kEndurance;

    int counter = 0;             // -0x1f4, the live objective count
    int8_t reqColour = 0;        // -0x1f5, 0 = any, 8 = Flashium
    uint8_t reqChain = 0;        // -0x1f6
    bool anyOrientation = false; // -0x1f7
    bool rotateColour = false;   // -0x1f8
    bool rotateChain = false;    // -0x1f9
    bool rotateOnTimer = false;  // -0x1ef, else rotate after each task
    bool countIsShown = false;   // -0x1f3
    bool mysteryHidden = false;  // -0x1f1

    bool markedCovered = false;  // -0x187
    bool markedXenon = false;    // -0x188
    bool hiddenAtoms = false;    // -0x189
    int8_t disabledColour = 0;   // -0x18a, 0 = nothing disabled
    bool morphBeaker = false;    // -0x1f0
    bool preFillBeaker = false;  // -0x1f2
};

// The three half-size balls and the number beside them, drawn by `1000:2a4a`.
// They live in `1000:3a67`'s own frame and are seeded from the objective at
// `1000:3ac7`, so they track the required colour and chain as those rotate.
struct TaskDisplay {
    int8_t colour = 8;   // -0x1bf; `1000:3ad0` shows Flashium when none is set
    uint8_t chain = 0;   // -0x1c0
    int count = 0;       // -0x1be, non-zero only in mode 4
};

using RollFn = std::function<int(int)>;

// `1000:86b8`'s prologue and dispatch, without the drawing. `replay` is the
// original's `-0x1ff`: set after a Continue, and it suppresses every
// randomisation so the wave comes back identical.
//
// `obj` is updated in place because the original's fields are frame variables
// that survive the call - the twelve zeroed here are listed in the struct.
void applyBriefing(Objective o, WaveProgress& progress, WaveObjective& obj,
                   const RollFn& roll, bool replay);

// `1000:3ac7`. The play session seeds the Task Display from the objective.
TaskDisplay seedTaskDisplay(const WaveObjective& obj);

// `1000:192f`, called by every matcher with the orientation of the run it just
// found and the type that run resolved to. Returns true if it counted - which
// is also what arms the rotation and the Mystery Wave reveal.
bool creditRun(WaveObjective& obj, RunKind kind, int8_t matchType,
               TaskDisplay& task);

// The 720-frame tick, `1000:4b73`. Returns true if the required colour or
// chain moved, which is when the original plays a sound.
bool taskTimerExpired(WaveObjective& obj, TaskDisplay& task);

}  // namespace tubes
