#pragma once

// Everything the port draws that does not need SDL.
//
// These functions lived in `main.cpp` until the file was split, for no better
// reason than that they were written there. Not one of them referenced an SDL
// type: they take a `tubes::Screen` and the art and fonts to put on it, and
// they were already written to take every input as an explicit parameter. That
// makes them portable, so they belong on the portable side of the line
// CLAUDE.md draws, and it makes them testable - `tubes-tests` links no SDL at
// all, which is why nothing in `main.cpp` had ever been covered by a check.
//
// The one function that did need SDL is `bindingLabel`, which asks
// `SDL_GetScancodeName` what a key is called. It stayed behind, and
// `drawRebindScreen` now takes the six finished label strings instead of the
// `Bindings` they come from. That is the only signature that changed in the
// move; every other function here is byte-for-byte what it was.

#include <string>
#include <vector>

#include "board.h"
#include "cutscene.h"
#include "ending.h"
#include "font.h"
#include "game.h"
#include "gfx.h"
#include "hiscore.h"
#include "input.h"
#include "instructions.h"
#include "menu.h"
#include "save.h"
#include "screen.h"
#include "session.h"
#include "textscreen.h"

namespace tubes {

// The atom table is indexed by a settled cell's value, which is
// `type + 19 * fadeFrame`, so it has to be big enough for the last fade frame
// of the last type. `main.cpp` sizes its sprite arrays from this too.
constexpr int kCellStates = tubes::kCellClearAbove + 1;

// The tube network furniture, decompiled out of 1000:3a67. It is not a
// backdrop: the network is built from individual segment sprites in layered
// passes, and the atoms are drawn between those passes so the solid pieces
// overpaint them. That is what makes the tubes read as hollow, with atoms
// visibly inside them and tubes overlapping one another.
enum Furn {
    kTubeH, kTubeHS, kTubeHR,
    kTubeV, kTubeVS, kTubeVR, kTubeVRS, kTubeVL, kTubeVLS,
    kBeakerFront, kBeakerShadow, kTestTubeShadow, kMarker,
    kFurnCount
};

// `1b2e:0a11`, the classroom scene. Called by the briefing `1000:86b8`, the
// stats screen `1000:8da5` and the Continue screen `1000:8c38` - one routine
// behind all three, which is why they share a look.
//
// `2321:060b(x, y, w, h, colour)` is a filled rect; the argument order comes
// off `1b2e:097a`, which passes its own two parameters into the same slots the
// fixed call fills with 0x4a and 0x1f. The blackboard is blitted at
// `(0, 12)`, which `2321:068d`'s `[BP+0xe]` (multiplied by 80, the Mode X
// plane pitch) settles - and `BLACKBRD.GFX` is 48644 bytes, exactly
// 320 x 152 plus a header, so it lands on rows 12..163.
struct SceneArt {
    const tubes::Image* corners = nullptr;   // UL, UR, DL, DR
    const bool* haveCorner = nullptr;
    const tubes::Image* pointer = nullptr;   // `POINTER0..3`
    const bool* havePointer = nullptr;
    const tubes::Image* books = nullptr;
    bool haveBooks = false;
    const tubes::Image* bar = nullptr;
    bool haveBar = false;
    // `TALK1..5.GFX`, the five mouths `1b2e:0cd1` cycles over his face.
    const tubes::Image* talk = nullptr;
    const bool* haveTalk = nullptr;
    // `1b2e:084e`'s joke slide: the wrong transparency and the face that
    // goes with it.
    const tubes::Image* flash = nullptr;
    bool haveFlash = false;
    const tubes::Image* pointerT = nullptr;
    bool havePointerT = false;
    // `JUMP1..3.GFX`, reached only by `1b2e:0656`'s `DS:0x20e3` arm - which
    // only the wave-75 ending ever sets.
    const tubes::Image* jump = nullptr;
    const bool* haveJump = nullptr;
};

// What the scene is doing right now, as opposed to what it is made of. Three
// of these move: the projector screen rolls down (`1b2e:0510`), the slide
// wobbles into place once per run (`1b2e:0a11`), and the professor waves
// (`1b2e:0e37`). Passing them as one struct keeps the four screens that share
// `drawScene` from each growing another argument every time one is found.
struct ScenePose {
    int frameH = tubes::kFrameH;      // the projector screen's rolled height
    int slideX = tubes::kSlideX;
    int slideY = tubes::kSlideY;
    int profFrame = 0;                // 0 standing, 1..3 `POINTER1..3`
    int mouthFrame = 0;               // 0 none, 1..5 `TALK1..5` - `1b2e:0cd1`
    bool jokeSlide = false;           // `FLASH.GFX` is up  - `1b2e:084e`
    bool jokeFace = false;            // and `POINTERT` with it
    // `DS:0x20fc`, 1..3, and 0 for "he is not jumping". `1b2e:0656`'s third
    // arm - the one `DS:0x20e3` selects - stands him on his books somewhere
    // else and hops him, and the ending is the only caller that reaches it.
    int jumpFrame = 0;

    // True while `1b2e:0a11`'s six-frame drop is still moving the slide.
    bool slideDropping = false;

    // Nothing a screen wants to write belongs on the slide in any of these.
    // `1b2e:0510` returns before its caller writes a word; `1b2e:084e` and the
    // slide drop both run inside `1b2e:0a11`, which is itself blocking and
    // returns before the caller is reached. So a screen's text arrives only
    // once the slide has stopped moving - the port used to draw the briefing's
    // text over a slide still wobbling under it.
    bool slideIsBusy() const {
        return frameH < tubes::kFrameH || jokeSlide || slideDropping;
    }
};

void drawHud(tubes::Screen& screen, const tubes::Game& game,
             const tubes::Font& big, const tubes::Font& small, bool haveBig,
             bool haveSmall);

void drawBriefing(tubes::Screen& screen, const tubes::Game& game,
                  const tubes::Image* bg, bool haveBg,
                  const tubes::Font& big, const tubes::Font& small,
                  bool haveBigF, bool haveSmallF,
                  const tubes::Sprite* atoms, const bool* haveAtom,
                  const tubes::Sprite* furn, const bool* haveFurn,
                  int8_t decorBall, const SceneArt& art,
                  const ScenePose& pose);

void drawBanner(tubes::Screen& screen, tubes::Banner banner,
                const tubes::Font& heading, bool haveHeading,
                const tubes::Font& small, bool haveSmall, bool showHint);

void drawStats(tubes::Screen& screen, const std::vector<tubes::StatsRow>& rows,
               const tubes::Image* board, bool haveBoard, const SceneArt& art,
               const tubes::Font& heading, const tubes::Font& label,
               const tubes::Font& number, bool haveHeading, bool haveLabel,
               bool haveNumber, const ScenePose& pose);

void drawEnding(tubes::Screen& screen, int page, const tubes::Image* board,
                bool haveBoard, const SceneArt& art, const ScenePose& pose,
                const tubes::Font& heading, bool haveHeading,
                const tubes::Font& small, bool haveSmall,
                const tubes::Image* prize, bool havePrize);

void drawContinue(tubes::Screen& screen, int ticksLeft,
                  const tubes::Font& heading, bool haveHeading,
                  const tubes::Font& number, bool haveNumber);

void drawHiScores(tubes::Screen& screen, const tubes::HiScoreBankData& bank,
                  const tubes::Font& heading, bool haveHeading,
                  const tubes::Font& body, bool haveBody, int editRow,
                  const std::string& editName, int cursorPhase);

void drawHiScoreViewer(tubes::Screen& screen, const tubes::HiScoreBankData& bank,
                       const char* title, const tubes::Image* board,
                       bool haveBoard, const tubes::Image* bar, bool haveBar,
                       const tubes::Font& heading, bool haveHeading,
                       const tubes::Font& script, bool haveScript);

void drawSaveScreen(tubes::Screen& screen, const tubes::SaveBankData& bank,
                    tubes::SaveBank which, int selected, bool typing,
                    const std::string& editText,
                    const tubes::Font& heading, bool haveHeading,
                    const tubes::Font& script, bool haveScript,
                    const tubes::Sprite* marker, bool haveMarker);

void drawInstructionSlide(tubes::Screen& screen,
                          const tubes::InstructionSlide* pages, int count,
                          int slide, bool hasNav,
                          const tubes::Image* board, bool haveBoard,
                          const SceneArt& art, const ScenePose& pose,
                          const tubes::Font& small, bool haveSmall,
                          const tubes::Font& big, bool haveBig,
                          const tubes::Sprite* atoms, const bool* haveAtom,
                          const tubes::Sprite* tube, const bool* haveTube,
                          const tubes::Sprite* furn, const bool* haveFurn);

void drawRebindScreen(tubes::Screen& screen, const std::string* labels,
                      int row, bool waiting, const tubes::Image* board,
                      bool haveBoard, const SceneArt& art,
                      const tubes::Font& big, bool haveBig,
                      const tubes::Font& script, bool haveScript,
                      const tubes::Font& small, bool haveSmall, int profFrame);

void drawGraphicsScreen(tubes::Screen& screen, const tubes::GraphicsOptions& g,
                        int row, const tubes::Image* board, bool haveBoard,
                        const SceneArt& art, const tubes::Font& big,
                        bool haveBig, const tubes::Font& script,
                        bool haveScript, const tubes::Font& small,
                        bool haveSmall, int profFrame);

void drawPrompt(tubes::TextScreen& ts, const char* prompt);

void drawPanel(tubes::Screen& screen, int x, int y, int w, int h);

void drawPaused(tubes::Screen& screen, const tubes::Font& heading,
                bool haveHeading);

void drawHelpScreen(tubes::Screen& screen, int mode,
                    const tubes::Font& heading, bool haveHeading,
                    const tubes::Font& small, bool haveSmall);

void drawTitle(tubes::Screen& screen, const tubes::Image& bg,
               const tubes::Screen& fgScene, bool haveArt,
               const tubes::Menu& menu, const tubes::TitleAtom& atom,
               const tubes::Sprite* atoms, const bool* haveAtom, int atomBall,
               const tubes::Image* stars, const bool* haveStar,
               const tubes::Font& big, bool haveBig,
               const tubes::Image& fg, const tubes::Font& small,
               bool haveSmall);

void drawTaskDisplay(tubes::Screen& screen, const tubes::Game& game,
                     const tubes::Font& big, bool haveBig,
                     const tubes::Sprite* atoms, const bool* haveAtom,
                     const tubes::Sprite* furn, const bool* haveFurn,
                     const tubes::Sprite* smallBall, const bool* haveSmallBall);

}  // namespace tubes
