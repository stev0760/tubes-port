#include "session.h"

namespace tubes {

// ---------------------------------------------------------------------------
// `1000:2dd0`
// ---------------------------------------------------------------------------

GameAction classifyGameKey(uint8_t code, bool attractMode, bool saveDisabled) {
    // `1000:2de2`: in attract mode the key is rewritten to ESC before the
    // case, so any key at all leaves the demo. This is a rewrite rather than a
    // separate branch, which is why the demo also honours the abort banner.
    if (attractMode) code = gamekey::kEsc;
    // `1000:2ded`: F2 becomes nothing when the save flag is set.
    if (saveDisabled && code == gamekey::kF2) return GameAction::kIgnored;

    switch (code) {
    case gamekey::kEsc: return GameAction::kAbort;
    case gamekey::kF1:  return GameAction::kHelp;
    case gamekey::kF2:  return GameAction::kSave;
    case gamekey::kF3:  return GameAction::kMusicToggle;
    case gamekey::kF4:  return GameAction::kSoundToggle;
    case gamekey::kF5:  return GameAction::kPause;
    default:            return GameAction::kIgnored;
    }
}

// ---------------------------------------------------------------------------
// The banners
// ---------------------------------------------------------------------------

BannerText bannerText(Banner b) {
    switch (b) {
    // `1000:3a12` over `1000:3a20` - 13 characters over 11 underscores.
    case Banner::kWaveComplete:
        return {"Wave Complete", "___________", "VICTORY.MUS"};
    // `1000:3a2c` over `1000:39f8` - 9 characters over 7 underscores.
    // The rule is its own constant in every case and is not measured from the
    // caption; two of the three would come out wrong if it were.
    case Banner::kGameOver:
        return {"Game Over", "_______", "DEATH.MUS"};
    // `1000:3a36` over `1000:3a20`, sharing the Wave Complete rule. This arm
    // plays nothing - it is the only one of the three with no music.
    case Banner::kAborted:
        return {"Game Aborted", "___________", ""};
    default:
        return {"", "", ""};
    }
}

Banner bannerFor(const SessionFlags& flags, bool waveComplete) {
    // `1000:5dbb`, `1000:5e2a`, `1000:5e97` in that order.
    if (waveComplete) return Banner::kWaveComplete;
    if (flags.gameOver) return Banner::kGameOver;
    if (flags.aborted) return Banner::kAborted;
    return Banner::kNone;
}

// ---------------------------------------------------------------------------
// `1000:8da5`
// ---------------------------------------------------------------------------

namespace {

// The two label colours, `1000:8d5e` onward: 0x9b for a caption and 0xaf for
// the number under it.
constexpr uint8_t kStatLabelColour = 155;
constexpr uint8_t kStatValueColour = 175;
constexpr uint8_t kStatTitleColour = 159;

void addRow(std::vector<StatsRow>& out, int y, StatsFont font, uint8_t colour,
            uint8_t mode, std::string text) {
    StatsRow r;
    r.y = y;
    r.font = font;
    r.colour = colour;
    r.mode = mode;
    r.text = std::move(text);
    out.push_back(r);
}

}  // namespace

std::vector<StatsRow> enterStatsScreen(SessionTotals& totals, int& liveChains,
                                       int wave, int score, bool isHighScore) {
    totals.chainsThisWave = liveChains;
    std::vector<StatsRow> rows =
        buildStatsScreen(totals, wave, score, isHighScore);
    // `1000:8ee4` zeroed the one byte both of these stand for, so the play
    // session's copy follows the screen's rather than being zeroed separately -
    // if the two ever disagree, this is the line that is wrong.
    liveChains = totals.chainsThisWave;
    return rows;
}

std::vector<StatsRow> buildStatsScreen(SessionTotals& totals, int wave,
                                       int score, bool isHighScore) {
    std::vector<StatsRow> rows;

    // `1000:8d45` + Str(wave) + `1000:8d4b`, then the rule at y 41.
    addRow(rows, 38, StatsFont::kHeading, kStatTitleColour, textmode::kPeak,
           "Wave " + std::to_string(wave) + " Stats");
    addRow(rows, 41, StatsFont::kHeading, kStatTitleColour, textmode::kPeak,
           "___________");

    addRow(rows, 60, StatsFont::kLabel, kStatLabelColour, textmode::kFadeDown,
           "Molecule Chains");
    addRow(rows, 68, StatsFont::kNumber, kStatValueColour, textmode::kFadeDown,
           std::to_string(totals.chainsThisWave));

    // `1000:8d68`. The accumulation happens between the two lines, in the
    // middle of the drawing - so the per-wave counter is zeroed by the screen
    // that just displayed it.
    totals.totalChains += totals.chainsThisWave;
    totals.chainsThisWave = 0;

    addRow(rows, 82, StatsFont::kLabel, kStatLabelColour, textmode::kFadeDown,
           "Total Molecule Chains");
    addRow(rows, 90, StatsFont::kNumber, kStatValueColour, textmode::kFadeDown,
           std::to_string(totals.totalChains));

    addRow(rows, 104, StatsFont::kLabel, kStatLabelColour, textmode::kFadeDown,
           "Score");
    addRow(rows, 112, StatsFont::kNumber, kStatValueColour, textmode::kFadeDown,
           std::to_string(score));

    // `1000:8dfc`: the High Score line's y is 0x87, and moves to 0x97 when the
    // Perfect Bonus lines are drawn, because they occupy the row it would use.
    int highScoreY = 135;
    if (totals.perfectBonus) {
        highScoreY = 151;
        addRow(rows, 126, StatsFont::kLabel, kStatLabelColour,
               textmode::kFadeDown, "Perfect Bonus!");
        addRow(rows, 134, StatsFont::kNumber, kStatValueColour,
               textmode::kFadeDown, std::to_string(kPerfectBonus));
    }

    if (isHighScore) {
        addRow(rows, highScoreY, StatsFont::kLabel, 38, textmode::kFadeUp,
               "High Score!");
    }
    return rows;
}

// ---------------------------------------------------------------------------
// `1000:8c38`
// ---------------------------------------------------------------------------

ContinueResult ContinuePrompt::tick(bool accept, bool decline) {
    if (ticksLeft_ <= 0) return ContinueResult::kDeclined;
    // `1000:8cfb` tests the declining key first, so a frame carrying both
    // keys declines. Order preserved rather than tidied.
    if (decline) { ticksLeft_ = 0; return ContinueResult::kDeclined; }
    if (accept) { ticksLeft_ = 0; return ContinueResult::kAccepted; }
    // `1000:8d35`: the counter drops after the key wait, and reaching zero
    // ends the loop - which is a decline, not an acceptance.
    if (--ticksLeft_ <= 0) return ContinueResult::kDeclined;
    return ContinueResult::kWaiting;
}

void applyContinue(SessionFlags& flags, SessionTotals& totals) {
    // `1000:8d0e`..`1000:8d2d`. The score is zeroed and the drop seed
    // restored, but neither chain total is touched.
    if (totals.continuesLeft > 0) --totals.continuesLeft;
    flags.replay = true;
    flags.gameOver = false;
}

// ---------------------------------------------------------------------------
// The wave loop
// ---------------------------------------------------------------------------

StageTransition advanceStage(SessionStage stage, const SessionFlags& flags,
                             int mode) {
    StageTransition t;
    switch (stage) {
    case SessionStage::kBriefing:
        t.next = SessionStage::kPlay;
        return t;

    case SessionStage::kPlay:
        // The banner is inside `3a67`, drawn after its loop falls out, so it
        // is always the next thing - including on an abort.
        t.next = SessionStage::kBanner;
        return t;

    case SessionStage::kBanner:
        // `1000:a5e8`: an abort jumps clear of everything, so there is no
        // stats screen and no Continue offer.
        if (flags.aborted) { t.next = SessionStage::kFinished; return t; }
        // `1000:a5f2`: the stats screen is wave mode only.
        if (modeHasWaves(mode)) { t.next = SessionStage::kStats; return t; }
        if (flags.gameOver) { t.next = SessionStage::kFinished; return t; }
        t.next = SessionStage::kBriefing;
        t.advanceWave = false;   // no progression outside wave mode
        return t;

    case SessionStage::kStats:
        // `1000:a604`: the Continue screen runs only on a game over.
        if (flags.gameOver) { t.next = SessionStage::kContinue; return t; }
        // `1000:a60f`: the progression is skipped when `replay` is set.
        t.advanceWave = !flags.replay;
        t.next = SessionStage::kBriefing;
        return t;

    case SessionStage::kContinue:
        // `1000:a69b`: the loop repeats until aborted or still game over. An
        // accepted Continue has cleared `gameOver`, so it comes back here.
        if (flags.aborted || flags.gameOver) {
            t.next = SessionStage::kFinished;
            return t;
        }
        t.advanceWave = !flags.replay;
        t.next = SessionStage::kBriefing;
        return t;

    case SessionStage::kFinished:
    default:
        t.next = SessionStage::kFinished;
        return t;
    }
}

}  // namespace tubes
