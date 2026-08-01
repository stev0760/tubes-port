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
#include <utility>
#include <vector>

#include "board.h"
#include "save.h"
#include "session.h"
#include "wave.h"

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

// The frame rate, MEASURED - see docs/reversing-notes.md. It was carried as
// 18.2 Hz, the PC BIOS tick, for the whole project on the strength of "attract
// mode produced ~17 state changes a second". It is not 18.2.
//
// The game installs its own timer (`226c:00c6` reprograms the PIT and hooks
// INT 8) and `21ea:06ba` waits out a per-frame period held in `DS:0x0d40`,
// against a dividend of 145. Read live, that word is 9 in the play session and
// 6 on the title screen, the menu and the briefing.
//
// Two independent routes agree:
//
//   145 / 9                                      = 16.11 Hz
//   least squares of the original's DEMO.SCR byte index against wall clock,
//   979 timed readings over 130 s, converted to game frames through the
//   port's own exact frame->index mapping
//                                                = 16.18 Hz
//
// 0.4% apart, so the formula is right and the constant is derived from it.
//
// NOT resolved: how 145 relates to the PIT divisor the game actually writes,
// which is 16384 = 72.83 Hz. 72.83/9 is 8.09 Hz and is contradicted by the
// measurement, so the tick the period counts is not simply that interrupt.
constexpr float kFrameDividend = 145.0f;   // `21ea:0706  MOV AX,0x91`
constexpr int kSessionPeriod = 9;          // DS:0x0d40 in the play session
constexpr int kTitlePeriod = 6;            // ...on title, menu and briefing

constexpr float kFrameHz = kFrameDividend / kSessionPeriod;   // 16.11 Hz
constexpr float kTitleHz = kFrameDividend / kTitlePeriod;     // 24.17 Hz

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
// States 1 and 2 are a two-frame teardown after a record lands - `1000:18ec`
// steps 1 to 2 and 2 to 0, and `drawn()` is `state > 2`, so neither renders.
// They exist so a slot is not reallocated on the frame it was released.
constexpr uint8_t kLanded = 1;
constexpr uint8_t kLanded2 = 2;
constexpr uint8_t kRise = 3;      // up a feed tube, y decreasing
constexpr uint8_t kGoLeft = 5;    // across the top, x decreasing
constexpr uint8_t kGoRight = 6;   // across the top, x increasing
constexpr uint8_t kDescend = 7;   // down a play column, y increasing
constexpr uint8_t kInTube = 8;    // caught, sliding down to its slot
constexpr uint8_t kTipped = 9;    // released, falling into the beaker
}  // namespace atomstate

// The test tube's tipping animation, `1000:463a`. `tube.state = 3` runs it and
// `tube.phase` (+0x05) selects both the body and the sprite.
namespace tubephase {
constexpr uint8_t kUpright = 1;
constexpr uint8_t kTilted = 2;
constexpr uint8_t kPoured = 3;
constexpr uint8_t kRelease = 4;   // never rendered; see kTipFrames below
}  // namespace tubephase

// The session holds one sound handle per ATOM TYPE, `sound[t]` at `F9 - 0x72 +
// 4t`, loaded by name at `1000:a2e0` onward. Index 0 of that same array is
// `DROP.SFX`, so a lost atom is literally the sound of type nothing:
//
//     sound[0]      DROP        an atom lost, or tipped into a full column
//     sound[1..7]   R/G/B/C/P/Y/PNK FADE
//     sound[8]      FFADE       Flashium
//     sound[9]      AFADE       the AntiMatter blast
//     sound[10]     GLDFADE     a Bonus caught
//     sound[18]     CRFADE      the Crystal
//
// and three more sit just below the array, at -0x76, -0x7a and -0x7e. Types
// 11..17 and 19 have no sound, which is the same set that has no fade family.
//
// ONE plays at a time. `SBSOUND.DRV`'s play entry calls its own stop routine
// before anything else and holds a single position/length pair, so a new sound
// cuts off whatever was going.
namespace sfx {
constexpr int8_t kNone = -1;
constexpr int8_t kDrop = 0;          // = sound[0]; 1..19 are the atom types
constexpr int8_t kHitGlass = 20;     // F9-0x76, landing on the beaker floor
constexpr int8_t kHitAtom = 21;      // F9-0x7a, landing on another atom
constexpr int8_t kSelect = 22;       // F9-0x7e, the wave-mode element cycle
constexpr int8_t kCount = 23;
}  // namespace sfx

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

// The tube's record carries `array[1..5] of AtomRec` inline, at `tube + 7 +
// 28*n`, with its count in the byte at `tube + 0x21`. Slot 1 is the BOTTOM:
// each slot's y offset within the tube is a literal in the fill routines -
// 52, 39, 26, 13, 0 for slots 1..5, the 13 px row pitch again - and the atom's
// absolute y is that plus the tube's own y of 0x44.
//
// Five is a literal in the original too. `1000:08d2` and `1000:0a27` fill
// `while count < 5`, so a Multiplier tops the tube up to five whatever a
// Filler has done to it - the Filler does not shrink a capacity variable, it
// parks an immovable atom in slot 1.
constexpr int kTubeSlots = 5;

// The tube's own y, the literal `0x44` the session setup writes, and the five
// slot offsets the fill routines write as literals. Index 0 is unused so the
// table can be read with the original's 1..5.
constexpr int kTubeY = 68;
constexpr int kSlotDy[kTubeSlots + 1] = {0, 52, 39, 26, 13, 0};

// A caught atom drops to its slot, and a tipped one falls into the beaker, at
// the same flat 9 px a frame - `1000:187e` and `1000:15c0`.
constexpr int kTubeDropPx = 9;

// The tipping animation, `1000:463a`. A divider at tube+0x16 counts to 2, so
// each phase lasts two frames and the whole tip is six.
constexpr int kTipDivider = 2;

// Phase 2 bunches the contents up as the tube tilts, phase 3 lines them all up
// as it pours. Both are written as literals per slot, not computed.
constexpr int kTiltY[kTubeSlots + 1] = {0, 99, 93, 87, 81, 73};
constexpr int kPourY = 82;

// Flashium, type 8, has NO SPRITE OF ITS OWN. `1000:486b` rewrites its slot in
// the ball table from one of the seven ordinary colours instead:
//
//     Inc(subTick);
//     if subTick = 5 then begin
//         subTick := 1;
//         Inc(flash);  if flash = 8 then flash := 1;
//         ball[8] := ball[flash]
//     end
//
// so it advances every FOUR frames - the counter starts at 1 and fires when it
// reaches 5 - which at 18.2 Hz is the 4-ish per second a play session measured.
// The cycle was known from watching; this is the code doing it.
constexpr int kFlashPeriod = 5;

// The Bonus atom's award, `1000:0846  ADD [award], 0x3e8`.
constexpr int kBonusAward = 1000;

// The beaker's five rows, as the y a falling record must reach to land in
// them - `1000:15da` onward, indexed by the Pascal row 1..5. Row 1's 131 is
// three pixels above where the cell actually draws (134); every other row is
// exact. Transliterated as found.
constexpr int kLandY[kTubeSlots + 1] = {0, 131, 147, 160, 173, 186};

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
    // Record +0x0d. The router overloads it: in the tube (state 8) it is the
    // slot's y offset, 52 down to 0; falling into the beaker (state 9) it is
    // first the target y and then, on arrival, the row 1..5 that y meant. That
    // reuse is the original's, not a simplification - `1000:167c` rewrites the
    // field in place from one meaning to the other.
    int slotDy = 0;
    // Record +0x0f. Set when a caught atom reaches its slot; it is what gates
    // the catch-time specials at `1000:180c`, not the catch itself.
    bool arrived = false;
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

// The array is `array[1..12]`, though. Records 7..12 are the atoms tipped out
// of the test tube and falling into the beaker - a pool of six, allocated by
// the first free one at `1000:4764`, which halts the game if none is. The
// router runs over all twelve in one loop at `1000:47fe`; the two halves are
// distinguished only by the state a record is in.
constexpr int kAtomRecords = 12;

class Game {
public:
    // `randomTrace`, if given, collects every `Random(n)` call as (n, seed
    // going in). It is a CONSTRUCTOR argument rather than a setter because the
    // session's very first roll - the test tube's starting column, 1000:43d6 -
    // happens in here, and a sink attached afterwards silently loses it. That
    // is the exact off-by-one the trace exists to find.
    Game(int cols, int rows, Difficulty diff, uint32_t seed,
         std::vector<std::pair<int, uint32_t>>* randomTrace = nullptr);

    // Callers pass the raw button state each frame. NOTHING is edge-detected:
    // the original gates its whole input block on the test tube being idle
    // (`1000:44f0`) and reads the buttons as levels, so holding A tips once
    // every six frames rather than once. The port used to edge-detect A, which
    // made holding it do nothing at all.
    void update(uint8_t buttons, float dt);

    // Advances EXACTLY one game frame. `update` turns elapsed real time into a
    // variable number of frames, which is right for a player and wrong for a
    // recording: `DEMO.SCR` is one input byte per frame and replaying it has to
    // consume them one for one.
    void stepOnce(uint8_t buttons);

    const Board& board() const { return board_; }

    // A record, 1..12. Slots 1..6 are the network, one per column; 7..12 are
    // the atoms tipped out of the tube. Always valid; check `active()`.
    const Falling& atom(int col) const { return atoms_[col]; }

    // The tube's stop, 0..5, and its actual x - which is between two stops
    // while it is sliding. Renderers want the x; the board wants the stop.
    int tubeColumn() const { return tubeColumn_; }
    int tubeX() const { return tubeX_; }
    // `1000:44f0` tests the tube's state and, when it is not 0, jumps straight
    // past the input block to the state machine - so the input driver is not
    // CALLED AT ALL on a frame where the tube is sliding or tipping.
    //
    // For live play that is invisible: not reading the keyboard and reading it
    // then ignoring it look the same. For a REPLAY it is the whole ball game,
    // because in demo playback those driver vectors are the demo reader and
    // calling one is what advances the recording. A `.SCR` is therefore one
    // byte per frame the tube was IDLE, not one byte per frame - so a replay
    // that steps the stream unconditionally drifts out of step the first time
    // the player moves, and never recovers.
    bool acceptsInput() const { return tubeState_ == 0; }
    // The tipping animation's phase, 1..3 as far as any renderer sees - it
    // selects TESTUBE1/2/3. Phase 4 exists but never survives to a draw.
    uint8_t tubePhase() const { return tubePhase_; }

    // The colour Flashium is wearing this frame, 1..7. A renderer must draw
    // type 8 with THIS type's sprite; there is no sprite for 8 itself, and one
    // drawn as-is is invisible. See kFlashPeriod.
    int8_t flashColour() const { return flashColour_; }

    // The test tube holds up to five atoms, stacked. Index 0 is slot 1, the
    // BOTTOM of the tube; the LAST element is the mouth, and it is both the one
    // a catch lands in and the one the next A press tips out. Five is not
    // inferred from sprite heights any more: the game's own Detailed
    // Instructions state "The test tube you control to collect and release
    // atoms can hold up to 5 atoms at a time", with no mention of difficulty.
    //
    // These are full records, not bare types, because the tipping animation
    // moves them: the original holds `array[1..5] of AtomRec` inline in the
    // tube and the animation writes their x and y directly.
    const std::vector<Falling>& tubeAtoms() const { return tube_; }
    std::vector<int8_t> tubeTypes() const;
    int tubeCapacity() const { return tubeCapacity_; }
    bool tubeFull() const {
        return static_cast<int>(tube_.size()) >= tubeCapacity_;
    }
    // The tube is a STACK, not a queue - `1000:4715` tips `slot[count]`, the
    // last one caught, and `1000:180c` fires a special on `slot[count]` too.
    // The port used to tip `tube_.front()`, which emptied it oldest-first.
    int8_t heldAtom() const {
        return tube_.empty() ? static_cast<int8_t>(kEmpty) : tube_.back().colour;
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
    // The award in flight and the number of runs it will be multiplied by.
    // The HUD shows both, as "+<pending>" and "x<multiplier>", while the ramp
    // is running - `1000:582a` and `1000:5872`.
    int scorePending() const { return scorePending_; }
    int scoreMultiplier() const { return scoreMultiplier_; }

    // Non-zero while a clear animation is running.
    int clearTimer() const { return clearTimer_; }
    // Atoms a "survive N atoms" wave is still waiting to see out of play.
    int atomsInPlay() const { return inPlay_; }

    // `1000:a525` and `1000:3660`, the two halves of a saved game. They are
    // mirror images - fourteen fields each, in the same order - so they live
    // together and a field added to one is obvious in the other. `SessionTotals`
    // is the caller's, so its three fields are passed rather than owned.
    void loadFrom(const SaveSlot& s, SessionTotals& totals);
    void saveInto(SaveSlot& s, const SessionTotals& totals) const;
    // The sound to play, consumed by the caller. See `namespace sfx` - it is an
    // atom type for the fade families and DROP, or one of the three ids above
    // it. `sfx::kNone` means nothing happened.
    //
    // A SMALL QUEUE, not one slot - and this is the same departure the voice
    // pool in sfx.h is, for the same reason and agreed the same way.
    //
    // One slot matched the driver: a second event in the same frame cut off
    // the first, which is what calling `PlaySound` twice does on hardware
    // with one voice. But it truncated the sound BEFORE it was ever played,
    // which is worse than the original - the original at least starts it -
    // and it is what a player heard as a drop being swallowed when a match
    // landed in the same frame. Every event still fires from the site the
    // original calls `PlaySound` at; they no longer overwrite each other on
    // the way out.
    int8_t takeSound() {
        if (soundCount_ == 0) return sfx::kNone;
        const int8_t s = pendingSounds_[0];
        for (int i = 1; i < soundCount_; ++i) {
            pendingSounds_[i - 1] = pendingSounds_[i];
        }
        --soundCount_;
        return s;
    }

    int chains() const { return chains_; }
    bool gameOver() const { return gameOver_; }

    // `1000:8d0b`, the arm an accepted Continue takes. It zeroes the score,
    // restores the drop count from the seed `DS:0x1d51`, and clears the game
    // over. It does NOT touch either chain total - those live in the outer
    // frame and a continued game keeps the chains it has made, which is why
    // the stats screen's running total survives a Continue.
    void continueSession() {
        score_ = 0;
        scorePending_ = 0;
        scoreMultiplier_ = 0;
        dropsRemaining_ = startingDrops_;
        gameOver_ = false;
    }

    // ---- Wave mode -------------------------------------------------------
    //
    // A Game is one WAVE, not one session. `1000:9e53` owns the loop around
    // `1000:3a67` - brief, play, show the stats, step the progression - so
    // the caller drives that with `startWave` and `waveComplete`.
    //
    // Endurance is the default and touches none of this: its mode is 1, and
    // every wave test in the original is `mode <> 0 and mode <> 1`.
    WaveMode waveMode() const { return objective_.mode; }
    const WaveObjective& objective() const { return objective_; }
    const WaveProgress& progress() const { return progress_; }
    const TaskDisplay& taskDisplay() const { return task_; }
    const std::vector<Crystal>& crystals() const { return crystals_; }

    // Runs `1000:86b8`'s briefing for the wave the progress is on and seeds
    // the play state from it - `1000:3ac7`. `replay` is the original's
    // `-0x1ff`: after a Continue the objective must come back identical.
    void startWave(bool replay = false);

    // `1000:5cff`: the counter is spent, nothing is still clearing, and the
    // Task Display has no count left to run down.
    bool waveComplete() const { return waveComplete_; }

    // `1000:a616`, and it runs ONLY on a cleared wave. The caller then calls
    // `startWave` again for the next one.
    void advanceWave() { progress_.advance(); }

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
    // Seeds the tube from bare types, putting every slot at rest in its own
    // position - which is what a captured state describes and what a test
    // wants. A slot mid-slide has to be built by hand.
    void setTubeAtoms(const std::vector<int8_t>& v);
    // The HUD is part of the frame now, so a captured state has to be able to
    // describe it or a mid-game capture can never be matched.
    // Exposes one roll of the generator, so a test can check it against the
    // algorithm rather than against itself.
    int rollForTest(int n) { return random(n); }
    // The two values the endurance ramp moves, so a test can watch it step.
    int spawnIntervalForTest() const { return spawnInterval_; }
    int rampCounterForTest() const { return rampCounter_; }
    int runsThisFrameForTest() const { return runsThisFrame_; }
    int networkVelForTest() const { return networkVel_; }
    void stepRampForTest(int runs) { stepEnduranceRamp(runs); }
    void setScore(int v) { score_ = v; }
    void setChains(int v) { chains_ = v; }
    void setDropsRemaining(int v) { dropsRemaining_ = v; }
    void setScorePending(int pending, int multiplier) {
        scorePending_ = pending;
        scoreMultiplier_ = multiplier;
    }

private:
    void spawn();
    void stepAtom(Falling& a);
    void updateBeaker();
    // The endurance ramp, `1000:235c` - the dispense interval and the network
    // velocity both move as the player clears. Takes the number of runs the
    // matchers formed this frame, which is the original's `[BP-8]`.
    void stepEnduranceRamp(int runs);
    // The test tube's own state machine, `1000:45e7` - the slide between stops
    // and the tipping animation.
    void stepTube(uint8_t buttons);
    // The Down/B boost, `1000:4534` - inside the tube's `state = 0` guard and
    // before the Left/Right handler moves the stop.
    void boostAtomUnderTube(uint8_t buttons);
    // Phase 4: hand the mouth's record to a free record of 7..12 - `1000:4715`.
    void releaseTippedAtom();
    // Append a slot the way the Multiplier fills do - `1000:092d`.
    void pushTubeSlot(int8_t colour);
    // The score ramp's clock, `1000:58c5` - a separate statement in the frame
    // body, and after the router rather than with the beaker.
    void stepScoreRamp();
    // The four specials that fire when the TUBE catches one - `1000:180c`.
    void catchSpecial();
    // The two halves of `-0x1be`, the in-play count. Every site is the same
    // three lines and every one is gated on mode 4 - see `atomLeftPlay`.
    void atomEnteredPlay();
    void atomLeftPlay();
    int random(int n);
    int8_t nextColour();
    // `1000:192f`, wired to Board's per-seed hook.
    void creditObjective(RunKind kind, int8_t matchType);
    // The original moves things a whole number of pixels per frame, so the
    // simulation steps in frames and `update()` only converts real time into
    // them.
    void stepFrame(uint8_t buttons);

    Board board_;
    // Index 1..12; [0] is never used, matching the Pascal array. 1..6 are the
    // network by column, 7..12 the tipped-atom pool.
    Falling atoms_[kAtomRecords + 1];

    // The test tube's own record: x at +0x00, state at +0x04, phase at +0x05,
    // the tip divider at +0x16, stop index at +0x1e, target x at +0x1f. State 0
    // is parked and is the ONLY state that accepts input; 1 and 2 are sliding
    // left and right, and 3 is tipping.
    int tubeColumn_ = 0;          // the original's +0x1e, less one
    int tubeX_ = kTubeStopX[1];
    int tubeTargetX_ = kTubeStopX[1];
    uint8_t tubePhase_ = tubephase::kUpright;
    int tipDivider_ = 0;
    // Both start at 1, from the session prologue at `1000:3ab3`.
    int flashTick_ = 1;
    int8_t flashColour_ = kRedium;
    uint8_t tubeState_ = 0;
    std::vector<Falling> tube_;
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
    // The Bonus atom's award GROWS across the session. `1000:0846` adds 1000
    // to a word of its own and then pays the whole word, and the only other
    // write to it is the zero at `1000:3a8d` in the session prologue - so the
    // first Bonus is worth 1000, the second 2000, the third 3000.
    int bonusAward_ = 0;
    // 1000:1c70 sets this to 10 on a match; the frame loop counts it down and
    // will not declare a wave complete while it is running.
    int clearTimer_ = 0;
    // `-0x1be`, the byte right beside it, and the reason a "survive N atoms"
    // wave does not end the instant the last atom is DISPENSED. See
    // `atomLeftPlay`.
    int inPlay_ = 0;
    // Written through `queueSound`; drained by `takeSound`, oldest first.
    static constexpr int kPendingSounds = 4;
    int8_t pendingSounds_[kPendingSounds] = {};
    int soundCount_ = 0;

    void queueSound(int8_t s) {
        if (s == sfx::kNone) return;
        if (soundCount_ >= kPendingSounds) return;      // the frame is full
        pendingSounds_[soundCount_++] = s;
    }
    int chains_ = 0;
    bool gameOver_ = false;

    // Wave mode. `objective_` is `1000:9e53`'s `-0x1ef`..`-0x1ff` block,
    // `progress_` its six counters, `task_` the three fields in `1000:3a67`'s
    // own frame that the Task Display draws from.
    WaveObjective objective_;
    WaveProgress progress_;
    TaskDisplay task_;
    // `1000:0236`'s array. Only mode 5 ever has any.
    std::vector<Crystal> crystals_;
    // Seeded 720 at `1000:3b00`, reloaded at `1000:4b6d`. ONE timer serves
    // both the Task Display rotation and the beaker morph.
    int taskTimer_ = kTaskTimerFrames;
    bool waveComplete_ = false;

    float fallHeight_ = 130.0f;   // retained for callers; unused by the path
    // Counts DOWN to the next dispense and is reloaded from the interval, the
    // way `1000:490a` does it, rather than counting up to a threshold.
    int spawnTimer_ = 1;
    int spawnInterval_ = kSpawnIntervalFrames[0];
    int networkVel_ = 2 * kSubPixel;
    // The endurance ramp, `1000:235c`: both of these move as you clear, which
    // is why neither is a constant after the difficulty seeds them. `[fe84]`
    // is a byte in the original and is allowed to wrap; the two latches are
    // `[fe46]` and `[fe47]`, and they exist so a crossing fires once.
    uint8_t rampCounter_ = 0;
    int runsThisFrame_ = 0;   // what the ramp saw, for the rig diff
    bool rampLatch5_ = false;
    bool rampLatch10_ = false;
    float frameAccum_ = 0.0f;     // real time carried between frames

    // The tube slides at 6 px/frame over an 18 px column pitch, so a column
    // change takes three frames.
    int moveTimer_ = 0;

    uint32_t rng_ = 1;
    std::vector<std::pair<int, uint32_t>>* randomTrace_ = nullptr;
};

}  // namespace tubes
