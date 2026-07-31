// The session's outer loop - `1000:9e53`'s wave loop, the end-of-session
// banners at the tail of `1000:3a67`, and the two screens the loop goes
// through: the stats screen `1000:8da5` and the Continue screen `1000:8c38`.
//
// Also `1000:2dd0`, the in-game key handler, which is where ESC and Pause
// live. That function is nested TWO deep - inside `1000:3a67` inside
// `1000:9e53` - and reaches the outermost frame through two static links,
// which is why the abort flag it writes cannot be found by searching `3a67`.
//
// Platform-agnostic on purpose: this is rules and layout, no SDL. The caller
// draws. See `docs/reversing-notes.md`, "Closing the session loop".
#ifndef TUBES_SESSION_H
#define TUBES_SESSION_H

#include <cstdint>
#include <string>
#include <vector>

#include "font.h"

namespace tubes {

// ---------------------------------------------------------------------------
// The three control flags
// ---------------------------------------------------------------------------
//
// `1000:9e53` keeps these adjacent, and Ghidra's decompiler names them two
// bytes low - its `local_1ff` is really `BP-0x1fd`. The listing at
// `1000:a5e8`..`1000:a6a9` is what these come from.
struct SessionFlags {
    bool aborted = false;    // BP-0x1fd, written ONLY by the key handler
    bool gameOver = false;   // BP-0x1fe, written by `3a67` at 47f8 / 5d0a
    bool replay = false;     // BP-0x1ff, written by the Continue screen
};

// ---------------------------------------------------------------------------
// `1000:2dd0`, the in-game key handler
// ---------------------------------------------------------------------------
//
// Called from `1000:5cf7` once per frame, and only when `KeyPressed`
// (`2000:5e52`) is true. Turbo Pascal's `ReadKey` returns #0 followed by the
// scancode for an extended key, and `2000:7823` folds that into `0x80 + scan`,
// so F1..F5 arrive as 0xbb..0xbf.
namespace gamekey {
constexpr uint8_t kEsc = 0x1b;
constexpr uint8_t kF1 = 0xbb;
constexpr uint8_t kF2 = 0xbc;
constexpr uint8_t kF3 = 0xbd;
constexpr uint8_t kF4 = 0xbe;
constexpr uint8_t kF5 = 0xbf;
}  // namespace gamekey

enum class GameAction {
    kIgnored,
    kAbort,         // ESC - sets `aborted` and returns immediately
    kHelp,          // F1
    kSave,          // F2
    kMusicToggle,   // F3
    kSoundToggle,   // F4
    kPause,         // F5
};

// `1000:2dd0`'s dispatch, in its own order. Two rewrites happen before the
// case, and both matter:
//
// * in attract mode (`DS:0x1d4e` = 0) ANY key is rewritten to ESC, which is
//   how a keypress drops the demo back to the title;
// * F2 is rewritten to nothing when `DS:0x1d4b` is set, which is the build
//   flag that disables saving.
GameAction classifyGameKey(uint8_t code, bool attractMode, bool saveDisabled);

// ---------------------------------------------------------------------------
// The end-of-session banners, `1000:5d64`
// ---------------------------------------------------------------------------
//
// Drawn by `3a67` itself after its loop falls out, before it returns. All
// three are centred over 0..319 in the heading font.
enum class Banner { kNone, kWaveComplete, kGameOver, kAborted };

constexpr int kBannerY = 92;             // 0x5c
constexpr int kBannerRuleY = 95;         // 0x5f
constexpr uint8_t kBannerColour = 47;    // 0x2f
constexpr uint8_t kBannerMode = textmode::kShadow | textmode::kPeak;   // 0x83

constexpr int kAbortHintY = 115;         // 0x73
constexpr uint8_t kAbortHintMode = textmode::kShadow | textmode::kFadeDown;

// `1000:3a43`. Only shown when the mode is not attract and saving is enabled,
// and the abort arm then calls `1000:2dd0` a second time so the offer works.
constexpr const char* kAbortHint = "F2 to Save Game, ESC for Main Menu!";

// The Perfect Bonus, `1000:5dae`: 0x9c4 added to the score before the banner.
constexpr int kPerfectBonus = 2500;

struct BannerText {
    const char* line1;
    const char* rule;     // NOT derived from line1 - see below
    const char* music;    // empty when the banner plays nothing
};

// The rule under each banner is its own string constant and is a different
// length in each case: `Game Over` is nine characters over SEVEN underscores.
// Deriving it from the caption would be wrong for two of the three.
BannerText bannerText(Banner b);

// ---------------------------------------------------------------------------
// The stats screen, `1000:8da5`
// ---------------------------------------------------------------------------
//
// Not a blackboard, whatever `MapProgram` labelled it: it re-blits the held
// `GAMEBG` and puts text over it, exactly as the briefing does. The blackboard
// is the cutscene at `1b2e:1651`.
struct SessionTotals {
    int chainsThisWave = 0;      // -0x17c
    int totalChains = 0;         // -0x14e
    bool perfectBonus = false;   // -0x17d
    int continuesLeft = 0;       // -0x14f
};

// Which of the three loaded fonts a row is drawn in.
enum class StatsFont { kHeading, kLabel, kNumber };

struct StatsRow {
    int y = 0;
    StatsFont font = StatsFont::kLabel;
    uint8_t colour = 0;
    uint8_t mode = 0;
    std::string text;
};

// Builds the screen exactly as `1000:8da5` draws it, and MUTATES `totals` the
// way that function does.
//
// The mutation is the part that is easy to miss: between the two chain lines
// the original does `-0x14e := -0x14e + -0x17c; -0x17c := 0`, so the running
// total is accumulated *in the draw code* and the per-wave counter is zeroed
// by the very screen that displays it. This is not a pure view - calling it
// twice would double-count, which is why the caller must build the rows once
// and then only redraw them.
std::vector<StatsRow> buildStatsScreen(SessionTotals& totals, int wave,
                                       int score, bool isHighScore);

// `1000:8da5`'s music, started after the drawing and before the flip.
constexpr const char* kStatsMusic = "STAT.MUS";

// `1000:86b8`'s.
constexpr const char* kBriefingMusic = "BRIEF.MUS";

// `1000:4494`, just before `3a67`'s frame loop - so once per WAVE, not once
// per session. The choice is made from the drop counter and NOT from the
// difficulty, which is what it looks like it should be: a wave entered with no
// drops left plays the fast song for its whole length.
constexpr const char* kPlayMusic = "GAME.MUS";
constexpr const char* kPlayMusicLastDrop = "FASTGAME.MUS";
inline const char* playMusicFor(int dropsLeft) {
    return dropsLeft == 0 ? kPlayMusicLastDrop : kPlayMusic;
}

// ---------------------------------------------------------------------------
// The Continue screen, `1000:8c38`
// ---------------------------------------------------------------------------
//
// A five-tick countdown. The number on screen IS the counter, and letting it
// reach zero declines - there is no separate "No" to press.
constexpr int kContinueTicks = 5;
constexpr const char* kContinueMusic = "CONTINUE.MUS";

constexpr int kContinueTitleY = 70;      // 0x46
constexpr int kContinueRuleY = 73;       // 0x49
constexpr int kContinueCountY = 90;      // 0x5a
constexpr uint8_t kContinueTitleColour = 159;
constexpr uint8_t kContinueCountColour = 15;

// What one tick of the Continue prompt decided.
enum class ContinueResult {
    kWaiting,    // still counting down
    kAccepted,   // the player pressed the accept key
    kDeclined,   // declined outright, or the countdown ran out
};

// `1000:8c38`'s body, one tick per call. `accept` and `decline` are the two
// keys the original's `WaitKey(2)` distinguishes by returning 1 and 2.
//
// On acceptance the caller must apply `applyContinue` - the original does the
// resets inline, but they touch the Game rather than this screen.
class ContinuePrompt {
public:
    // `1000:8c3f`: the screen does not appear at all with no continues left.
    bool begin(const SessionTotals& totals) {
        ticksLeft_ = totals.continuesLeft >= 1 ? kContinueTicks : 0;
        return ticksLeft_ > 0;
    }

    int ticksLeft() const { return ticksLeft_; }
    bool active() const { return ticksLeft_ > 0; }

    ContinueResult tick(bool accept, bool decline);

private:
    int ticksLeft_ = 0;
};

// `1000:8d0b`'s arm, the writes an accepted Continue makes. It zeroes the
// score and restores the drop seed, but leaves BOTH chain totals alone - so a
// continued game keeps the chains it has made.
void applyContinue(SessionFlags& flags, SessionTotals& totals);

// ---------------------------------------------------------------------------
// The wave loop, `1000:9e53` at `a5d2`..`a6a9`
// ---------------------------------------------------------------------------
enum class SessionStage {
    kBriefing,   // `1000:86b8`, wave mode only
    kPlay,       // `1000:3a67`
    kBanner,     // the tail of `3a67` at `1000:5d64`
    kStats,      // `1000:8da5`, wave mode only
    kContinue,   // `1000:8c38`
    kFinished,   // the loop has fallen out
};

// True when the mode takes a briefing and a stats screen. Every wave test in
// the original is `mode <> 0 and mode <> 1` - 0 is attract, 1 is endurance.
inline bool modeHasWaves(int mode) { return mode != 0 && mode != 1; }

// The stage the loop enters on, `1000:a5d2`.
inline SessionStage firstStage(int mode) {
    return modeHasWaves(mode) ? SessionStage::kBriefing : SessionStage::kPlay;
}

// What the banner shows given the flags, `1000:5dbb`..`1000:5e9d`. The
// original tests all three in this order and can draw more than one; the port
// shows the first, because two captions share one pair of rows.
Banner bannerFor(const SessionFlags& flags, bool waveComplete);

// The transition out of a stage. `advanceWave` is set when `1000:a616`'s
// progression should run before the next briefing - it runs only on a wave
// that was cleared and not replayed.
struct StageTransition {
    SessionStage next = SessionStage::kFinished;
    bool advanceWave = false;
};

// `1000:a5e8` onward. `stage` is the stage that just finished.
StageTransition advanceStage(SessionStage stage, const SessionFlags& flags,
                             int mode);

}  // namespace tubes

#endif  // TUBES_SESSION_H
