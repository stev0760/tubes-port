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

// `DS:0x1d4b`. Written in ONE place - `1000:b1e4`, which sets it to zero - and
// read in three: `1000:2ded`'s F2 gate, `1000:5ed0`'s F2 hint on the abort
// banner, and `1000:a6ba`'s high-score offer. Nothing ever sets it, so saving
// is always enabled in the shipped build and this is a constant rather than a
// variable. Kept as a named one because all three sites read it and a later
// edition might not.
constexpr bool kSaveDisabled = false;

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

// ---------------------------------------------------------------------------
// A banner is not just "wait for a key", and the difference is visible
// ---------------------------------------------------------------------------
//
// The Wave Complete arm at `1000:5dbb` and the Game Over arm at `1000:5e2a`
// are the same five steps:
//
//     PlayMusic(<the arm's song>)
//     WriteCentred(<caption>);  WriteCentred(<rule>)
//     Delay($28)                          { 1000:5dfb / 5e68 }
//     ClearKeyBuffer                      { [$234e] and 2591:0552 }
//     repeat key := ReadInput until (MusicFinished = $ff) or (key <> 0)
//     StopMusic
//
// and `1000:5ef0` then runs `Delay($28)` again on the way out, for every arm.
//
// **The first Delay is why the banner cannot be missed.** 40 retraces at 70 Hz
// is 0.571 s in which no input is looked at, and the key buffer is FLUSHED
// afterwards - so the keypress that ended the wave, which for a survive wave
// is the player holding the tip button, cannot dismiss the banner it caused.
// A port that goes straight to "wait for a key" shows the banner for one frame
// and moves on, which is exactly what a player reported: "the first wave
// ending did not show wave complete".
//
// **`[DS:0x22ce]` is the music's own end.** It returns 0xff once the song has
// been through once, and the wait ends on that as well as on a key - which is
// the player's earlier report that the banner ends when its music does,
// confirmed from the code rather than from watching.
constexpr int kBannerHoldRetraces = 0x28;      // 40, both ends
// Mode X's 70 Hz - `kRetraceHz` below, spelled out here because it is declared
// further down the file.
constexpr float kBannerHoldSeconds =
    static_cast<float>(kBannerHoldRetraces) / 70.0f;

// Where a banner is in that sequence.
enum class BannerPhase {
    kHold,     // the opening Delay($28); input is not looked at
    kWait,     // a key or the end of the music
    kOutro,    // `1000:5ef0`'s Delay($28), on the way to the stats screen
};

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
// The classroom scene, `1b2e:0a11`
// ---------------------------------------------------------------------------
//
// One routine draws the backdrop for the briefing, the stats screen and the
// Continue screen. `2321:060b(x, y, w, h, colour)` is a filled rect - the
// order is settled by `1b2e:097a`, which threads its own two parameters into
// the slots the fixed call fills with `0x4a` and `0x1f`.
constexpr int kBoardY = 12;           // `2321:068d`'s [BP+0xe], x is 0

constexpr int kFrameX = 62;           // 0x3e - the frame behind the slide
constexpr int kFrameY = 26;           // 0x1a
constexpr int kFrameW = 196;          // 0xc4
constexpr int kFrameH = 145;          // 0x91
constexpr uint8_t kFrameColour = 19;  // 0x13

constexpr int kSlideX = 74;           // 0x4a - the slide at rest
constexpr int kSlideY = 31;           // 0x1f
constexpr int kSlideW = 172;          // 0xac
constexpr int kSlideH = 132;          // 0x84
constexpr uint8_t kSlideColour = 17;  // 0x11

// `1b2e:097a` places the four 4x4 corner clips at the slide's origin plus
// these; note they are 168 and 128, i.e. the slide's size less the clip.
constexpr int kCornerDX = 168;        // 0xa8
constexpr int kCornerDY = 128;        // 0x80

// The corners are UL / UR / **DL** / **DR**, in the order `1000:aaba` loads
// them into `DS:0x20fe`, `0x2102`, `0x2106` and `0x210a`. `LLCORNER.GFX` and
// `LRCORNER.GFX` also exist in the archive and are NOT these - a plausible
// guess that the load table disproves.
constexpr const char* kCornerNames[4] = {"ULCORNER.GFX", "URCORNER.GFX",
                                         "DLCORNER.GFX", "DRCORNER.GFX"};

// ---------------------------------------------------------------------------
// The professor, `1b2e:0656`
// ---------------------------------------------------------------------------
//
// He is drawn BEFORE `1b2e:0a11`, which is why the frame and slide do not
// paint over him: the frame spans x 62..257 and he stands at x 267.
//
//     Draw(267, 121, POINTER0)     { the standing pose }
//     Draw(267, 165, BOOKS)        { the stack he stands on }
//     FillRect(62, 26, 196, h, 19) { h is DS:0xbba, the rolling height }
//     Draw(57, h + 26, SLIDEBAR)   { the roller bar rides the frame's edge }
constexpr int kProfX = 267;           // 0x10b
constexpr int kProfY = 121;           // 0x79
constexpr int kBooksY = 165;          // 0xa5
constexpr int kBarX = 57;             // 0x39
constexpr int kBarDY = 26;            // 0x1a, added to the frame height

// `1b2e:0e37`, the key wait, advances `DS:0x20b0` once per iteration - so the
// professor waves his pointer WHILE the game waits for a key, one frame every
// ten retraces. The counter runs 1..5, and `1000:af23`/`1000:af34` alias
// entries 4 and 5 onto POINTER2 and POINTER1 with a struct copy
// (`2000:7133`), so the sequence PING-PONGS rather than looping:
//
//     1 -> POINTER1   2 -> POINTER2   3 -> POINTER3   4 -> POINTER2   5 -> POINTER1
//
// That is also why the wait will not exit until the frame is back at 1: it
// finishes the gesture before letting the screen change.
constexpr const char* kPointerNames[4] = {"POINTER0.GFX", "POINTER1.GFX",
                                          "POINTER2.GFX", "POINTER3.GFX"};
constexpr int kProfWaveFrames = 5;
constexpr int kProfWaveRetraces = 10;
// Index into kPointerNames for wave frame 1..5; 0 is the standing pose.
inline int pointerFrameFor(int wave) {
    static const int kSeq[kProfWaveFrames] = {1, 2, 3, 2, 1};
    if (wave < 1 || wave > kProfWaveFrames) return 0;
    return kSeq[wave - 1];
}

// The slide DROPS into place the first time the scene is shown, and only then
// - `1b2e:0a11` gates the whole sequence on `DS:0x210e`, which it sets. Six
// frames, each held for 10 vertical retraces, wobbling around the resting
// place before settling on it.
struct SlideFrame { int x, y; };
constexpr SlideFrame kSlideDrop[] = {
    {62, 30}, {66, 32}, {72, 30}, {79, 29}, {75, 37}, {71, 33},
};
constexpr int kSlideDropFrames =
    static_cast<int>(sizeof(kSlideDrop) / sizeof(kSlideDrop[0]));

// ---------------------------------------------------------------------------
// Timing: `23e7:0024` is a VERTICAL RETRACE wait
// ---------------------------------------------------------------------------
//
// It polls port 0x3da bit 3 low-then-high `n` times, so `n` is `n` VGA frames
// at the Mode X refresh rate of 70 Hz - NOT milliseconds, and not the game's
// own 16.11 Hz simulation tick.
//
// That settles `1b2e:0e37(param)`, which runs `param * 7` iterations of
// `23e7:0024(10)`: `param * 70` retraces, so **`param` seconds exactly**. The
// round number is the confirmation. The Continue screen passes 2 and the
// briefing 0x1e, so a Continue tick is two seconds and the briefing gives up
// after thirty.
constexpr float kRetraceHz = 70.0f;
constexpr int kSlideDropRetraces = 10;   // per drop frame
inline float waitKeySeconds(int param) {
    return static_cast<float>(param * 7 * 10) / kRetraceHz;
}

// ---------------------------------------------------------------------------
// Turbo Pascal 7's `Random` - `2000:75bb` steps it, `2000:755e` scales it
// ---------------------------------------------------------------------------
//
// The derivation is in `game.cpp`, above `Game::random`, and it is not a
// modulus: `Random(n)` is the top 32 bits of the 48-bit product
// `RandSeed * n`, with `RandSeed` read UNSIGNED.
//
// It lives here because the classroom animations draw from it too. The
// original has ONE `RandSeed` for the whole program, so the professor's mouth
// and the atom the dispenser picks come off the same sequence; the port keeps
// a stream per `Game` plus one for the scene, because its generator is a Game
// member and the Instructions screen has no Game at all. What is transliterated
// is the arithmetic and every call's argument - `Random(12) + 1`, `Random(4) +
// 4`, `Random(100) < 5` - not which stream they are drawn from.
struct PascalRandom {
    uint32_t seed = 1;

    int next(int n) {
        seed = seed * 0x08088405u + 1u;
        return static_cast<int>((static_cast<uint64_t>(seed) *
                                 static_cast<uint32_t>(n)) >> 32);
    }
};

// ---------------------------------------------------------------------------
// The joke slide, `1b2e:084e`
// ---------------------------------------------------------------------------
//
// The player reported this from play: "Lanny accidentally shows a WRONG slide -
// he is flashing, wearing an Absolute Magic shirt under his lab coat." It is
// `FLASH.GFX`, and the sprite settles it on sight - 172 x 132, which is the
// slide rectangle exactly, and what it draws is the professor holding his coat
// open over an "AM" T-shirt. `POINTERT` is the 28 x 21 head that goes with it,
// a startled face stamped over his own.
//
// `1b2e:0a11` calls `1b2e:084e` on EVERY scene redraw - every slide change and
// every screen entry - and it is gated twice:
//
//     if (DS:0x210f = 0) and (Random(100) < 5) then begin
//       Flip;  Delay($f);
//       FillRect(74, 31, 172, 132, 17);  <the four corners>
//       Draw(74, 31, FLASH);                       { 2321:068d, opaque }
//       Flip;  SetPage;
//       Draw(267, 121, POINTERT);                  { over his face }
//       Delay($1e);
//       Draw(267, 121, POINTER[DS:0x20b0]);        { and back }
//       Delay($f);
//       DS:0x210f := 1;
//       PlaySound(SLIDE.SFX)
//     end
//
// `DS:0x210f` is cleared once, by `1000:b1da` at start-up, and set here - so
// this is one-in-twenty per slide but **at most once per program run**.
//
// The slide rect and corners it draws are the same ones `1b2e:0a11` lays down
// immediately afterwards, which is what wipes the gag: it is on screen for its
// own two delays and no longer.
constexpr int kJokeChance = 5;            // `Random(100) < 5`
constexpr int kJokeLeadRetraces = 15;     // `Delay($f)` before it appears
constexpr int kJokeFaceRetraces = 30;     // `Delay($1e)` with POINTERT up
constexpr int kJokeTailRetraces = 15;     // `Delay($f)` after his head is back

struct JokeSlide {
    bool used = false;     // DS:0x210f - start-up clears it, this sets it
    int phase = 0;         // 0 idle, 1 lead, 2 FLASH + POINTERT, 3 FLASH alone
    float accum = 0.0f;

    // Call wherever the original calls `1b2e:084e`: from every `1b2e:0a11`.
    bool maybeStart(PascalRandom& rng) {
        if (used || phase != 0) return false;
        if (rng.next(100) >= kJokeChance) return false;
        used = true;
        phase = 1;
        accum = 0.0f;
        return true;
    }

    bool active() const { return phase != 0; }
    bool showFlash() const { return phase >= 2; }   // the wrong slide is up
    bool showFace() const { return phase == 2; }    // and POINTERT with it

    // True on the frame the gag ends, which is where `SLIDE.SFX` goes - the
    // original plays it last, as the projector moves off the wrong slide.
    bool tick(float dt) {
        if (phase == 0) return false;
        accum += dt * kRetraceHz;
        for (;;) {
            const int hold = phase == 1   ? kJokeLeadRetraces
                             : phase == 2 ? kJokeFaceRetraces
                                          : kJokeTailRetraces;
            if (accum < static_cast<float>(hold)) return false;
            accum -= static_cast<float>(hold);
            if (++phase > 3) {
                phase = 0;
                accum = 0.0f;
                return true;
            }
        }
    }
};

// ---------------------------------------------------------------------------
// The professor TALKS before he waves - `1b2e:0cd1`
// ---------------------------------------------------------------------------
//
// `TALK1..5.GFX` were loaded by `1000:aaba` and drawn by no arm anyone had
// found, which is what `PLAN.md` recorded as "a fourth behaviour somewhere".
// It is `1b2e:0cd1`, and it is not a fourth arm of `1b2e:0656` at all - it is
// the OTHER key wait. Every screen that waits calls two of them in order:
//
//     k := 1b2e:0cd1(bursts);                  { he talks }
//     if k = 3 then k := 1b2e:0e37(seconds);   { timed out - now he waves }
//
// The Instructions pass `(35, 30)` per slide and the briefing `(0x17, 0x1e)`.
// Both return the same codes `1b2e:0e37` does, 3 being "ran out".
//
// The talk loop draws a 12 x 8 sprite at (276, 133) - inside the professor's
// own 44-wide box at (267, 121), i.e. over his mouth - and the size is a
// literal in the call rather than the resource header, which is why the five
// records hold a pointer and nothing else. `TALK1..5.GFX` measure exactly
// 12 x 8, so the literal and the art agree independently.
constexpr const char* kTalkNames[5] = {"TALK1.GFX", "TALK2.GFX", "TALK3.GFX",
                                       "TALK4.GFX", "TALK5.GFX"};
constexpr int kTalkX = 276;              // 0x114
constexpr int kTalkY = 133;              // 0x85

// `DS:0xbbb`, array[1..12] of byte: which mouth each step of the cycle shows.
// Frames 1..5 are TALK1..5, and 4 never comes up - the script uses 3 half the
// time, which is what makes it read as speech rather than as a flicker.
constexpr int kTalkScriptLen = 12;
constexpr int kTalkScript[kTalkScriptLen] = {1, 2, 3, 3, 3, 5, 2, 3, 1, 2, 3, 3};
constexpr int kTalkRetraces = 8;         // `Delay(8)`, so 8/70 s a mouth
// `1b2e:0e0a`: whatever the loop was showing, it closes on TALK3 on the way
// out. That is also the script's most common frame.
constexpr int kTalkRestFrame = 3;

// The structure of the loop is two counters, and the outer one is NOT frames:
//
//     repeat
//       DS:0x20c6 := Random(12) + 1;         { where in the script to start }
//       DS:0x20c7 := Random(4)  + 4;         { 4..7 mouths in this burst }
//       repeat
//         Delay(8);  Draw(276, 133, TALK[script[DS:0x20c6]]);
//         DS:0x20c6 := DS:0x20c6 + 1;  if DS:0x20c6 > 12 then DS:0x20c6 := 1;
//         <poll the keyboard into k>
//         DS:0x20c7 := DS:0x20c7 - 1
//       until (DS:0x20c7 = 0) or (k <> 0);
//       bursts := bursts - 1;  if bursts = 0 then k := 3
//     until k <> 0;
//     Draw(276, 133, TALK3)
//
// so `bursts` counts BURSTS, each 4..7 mouths long. 35 bursts is 16..28 s.
constexpr int kTalkBurstBase = 4;        // Random(4) + 4
constexpr int kTalkBurstSpan = 4;

// The parameter at each call site, read off the call rather than guessed. The
// Continue screen is the one screen with no talk at all: `1000:8c38` runs
// `1b2e:0e37(2)` on its own, which is the two-second tick its countdown is
// made of.
constexpr int kTalkBurstsBriefing = 0x17;   // `1000:86b8`, then Wave(0x1e)
constexpr int kTalkBurstsStats = 10;        // `1000:8da5`, then Wave(0x1e)
constexpr int kTalkBurstsSlide = 35;        // `1b2e:2d63` / `411b`, then Wave(0x1e)

// The two waits, in the order every screen runs them. `talking` is the first
// phase and `wave` is `DS:0x20b0`, parked at 0 - the standing pose - until the
// talk times out, which is why he does not gesture while he is speaking.
struct ProfessorIdle {
    bool talking = false;
    int step = 1;            // DS:0x20c6, 1..12 into kTalkScript
    int burstLeft = 0;       // DS:0x20c7, counts down 4..7 to 0
    int burstsLeft = 0;      // the loop's own parameter, in bursts
    int wave = 0;            // DS:0x20b0, 0 standing, 1..5 the gesture
    float accum = 0.0f;

    // `bursts` is `1b2e:0cd1`'s parameter: 35 for a slide, 0x17 for a briefing.
    void restart(int bursts, PascalRandom& rng) {
        talking = bursts > 0;
        burstsLeft = bursts;
        wave = 0;
        accum = 0.0f;
        if (talking) newBurst(rng);
    }

    // 0 draws no mouth at all - the wave frames carry their own, and stamping
    // one over `POINTER1..3` would put a still mouth on a moving head.
    int mouthFrame() const {
        if (!talking) return 0;
        return kTalkScript[step - 1];
    }

    void tick(float dt, PascalRandom& rng) {
        const int hold = talking ? kTalkRetraces : kProfWaveRetraces;
        accum += dt * kRetraceHz;
        while (accum >= static_cast<float>(hold)) {
            accum -= static_cast<float>(hold);
            if (talking) {
                if (++step > kTalkScriptLen) step = 1;
                if (--burstLeft <= 0) {
                    // A burst ends; the parameter counts those, not mouths.
                    if (--burstsLeft <= 0) {
                        // `k := 3` - the talk timed out, so the wave starts.
                        talking = false;
                        wave = 1;
                        accum = 0.0f;
                        return;
                    }
                    newBurst(rng);
                }
            } else if (++wave > kProfWaveFrames) {
                wave = 1;
            }
        }
    }

private:
    void newBurst(PascalRandom& rng) {
        step = rng.next(kTalkScriptLen) + 1;
        burstLeft = rng.next(kTalkBurstSpan) + kTalkBurstBase;
    }
};

// ---------------------------------------------------------------------------
// The projector screen ROLLS DOWN - `1b2e:0510`
// ---------------------------------------------------------------------------
//
// This is the animation the plan had been looking for on `DS:0x210e`, and it
// was never there. `1b2e:0a11`'s gated six-frame wobble (`kSlideDrop`, above)
// moves the SLIDE; the screen behind it is rolled down by `1b2e:0510`, the
// routine that builds the scene from nothing - and that one is not gated on
// anything. It runs every time the scene is built: entering the briefing
// (`1000:60d8`, `1000:86b8`), the Instructions (`1b2e:2d63`) and the Credits
// (`1b2e:411b`) all `CALL 1b2e:0510` first.
//
// The loop runs `i := 1 to 15`, and the whole of it is three calls:
//
//     CopyRect(3, page, 57, 26, SLIDEBAR.w, 150)   { erase the bar's old row }
//     FillRect(62, 26, 196, kRollDown[i], 19)      { the screen, growing }
//     Draw(57, kRollDown[i] + 26, SLIDEBAR)        { the bar rides its edge }
//     Delay(3)
//
// so it is the same rect and the same bar `1b2e:0656` draws at rest, with the
// height stepped. The heights are a word table at `DS:0xb9c`, array[1..15]:
constexpr int kRollDownFrames = 15;
constexpr int kRollDown[kRollDownFrames] = {
    11, 22, 33, 44, 55, 66, 77, 88, 99, 110, 121, 132, 145, 150, 145,
};
// Twelve even steps of 11, then 145, an OVERSHOOT to 150, and back to 145 -
// the screen is yanked down and bounces once, which is what a roller blind
// does. The last entry is `DS:0xbba`, and `1b2e:0656` reads exactly that word
// when it redraws the scene at rest, so `kFrameH` and `kRollDown`'s tail are
// the same number by construction rather than by coincidence.
constexpr int kRollDownRetraces = 3;     // `Delay(3)`, so 15 * 3/70 = 0.64 s

// The bar rides the screen's lower edge - `Draw(57, h + 26, SLIDEBAR)` - and
// `kBarX`/`kBarDY` above are those two constants.
struct ScreenRoll {
    int frame = kRollDownFrames;    // == settled
    float accum = 0.0f;

    void restart() { frame = 0; accum = 0.0f; }
    bool rolling() const { return frame < kRollDownFrames; }
    // The height the screen is drawn at this instant. Settled is `kFrameH`,
    // which is `kRollDown`'s last entry, so nothing jumps at the hand-off.
    int height() const { return rolling() ? kRollDown[frame] : kFrameH; }

    void tick(float dt) {
        if (!rolling()) return;
        accum += dt * kRetraceHz;
        while (accum >= kRollDownRetraces) {
            accum -= kRollDownRetraces;
            if (++frame >= kRollDownFrames) {
                frame = kRollDownFrames;
                accum = 0.0f;
                return;
            }
        }
    }
};

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
