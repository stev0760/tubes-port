#pragma once

// The frame loop's own state, grouped.
//
// `main()` carries around 148 loose locals. Most of them genuinely belong to
// one screen or one mechanism and only ever move together, so they are
// gathered here - a straight regrouping, with no behaviour attached to it.
//
// Only the clusters that are SELF-CONTAINED live here. The session state
// (`sstage`, the banner, the stats rows) is deliberately absent, because the
// lambdas that drive it - `endSession` above all - touch six clusters at once
// and mutate five, and because the banner, stats, Continue and pause screens
// have no capture flag, so `tools/screen_sweep.sh` cannot prove a change to
// them is harmless. The art and audio clusters are absent for a sharper
// reason: `SceneArt` holds bare pointers into a dozen art locals and the audio
// callback holds one into `sounds`, so moving either into a struct that is
// ever copied is a use-after-free that nothing here would catch.
//
// Everything in this file is portable, so it is compiled into `tubes-tests`
// and the pure functions at the bottom are covered by checks.

#include <string>

#include "hiscore.h"
#include "instructions.h"
#include "screens.h"
#include "session.h"

namespace tubes {

// Entering a name into the table, `1000:96db`. `1000:a6c1` runs it when the
// wave loop falls out and the session was not aborted, the mode is not
// attract, and `DS:0x1d4b` is clear.
//
// Not `hiscore.h`'s `HiScoreEntry`, which is a row of the table itself. This
// is the screen that fills one in.
struct NameEntry {
    bool active = false;
    std::string name;
    int row = 0;                  // 1-based, as the original's display loop is
    int cursor = tubes::kHsCursorMin;
    int cursorDir = 1;
    float cursorAccum = 0.0f;
    // Counts out the applause between finishing the name and committing it.
    float hold = 0.0f;
    tubes::HiScoreBank bank = tubes::HiScoreBank::kWave;
};

// The standalone viewer the menu opens, `1b2e:61b6`. Page 0 is Endurance and
// page 1 is Wave; the original pre-renders both onto the two video pages and
// flips between them, and redrawing gives the same picture.
struct HiScoreViewer {
    bool viewing = false;
    int page = 0;
    float timer = tubes::kHsViewSeconds;   // the thirty-second give-up
};

// The F2 save screen, `1000:2dd0`'s save arm. It blocks the frame loop the
// same way Pause does - the original calls it from inside the loop body and
// does not come back until it is done.
struct SaveScreen {
    bool open = false;
    int slotSel = 1;              // `DS:0x1d4d`
    bool typing = false;
    std::string desc;
    float written = 0.0f;         // `1000:3722`'s 20-retrace hold
};


// The three slide decks, the rebinding screen and the graphics screen. They
// are grouped because they are one code path: Instructions `1b2e:2d63`,
// Credits `1b2e:411b` and the shareware's Ordering Info `1ac3:4889` differ
// only in their page table, and the two port-only screens sit beside them as
// flags rather than as `Menu::Page` values - the page machine is the
// original's and there is no page 8 in it.
struct Decks {
    bool instrOpen = false;
    int instrSlide = 0;
    const tubes::InstructionSlide* instrPages = tubes::kInstructionSlides;
    int instrPageCount = tubes::kInstructionSlideCount;
    bool instrNav = true;

    bool rebindOpen = false;
    int rebindRow = 0;                 // 0..5, the control being pointed at
    bool rebindWaiting = false;        // armed, waiting for the press

    bool graphicsOpen = false;         // the port's display options
    int graphicsRow = 0;               // 0..kGraphicsRows-1

    // The F1 help overlay, `1000:2e1c`. Blocks the loop like the save screen
    // and Pause do, and leaves on any key at all.
    bool helpScreen = false;

    // `1000:ac01`: the shareware's Exit does not exit. It runs the Ordering
    // Info deck first and only then Halts, so a quit that has been asked for
    // waits for the deck to finish.
    bool quitAfterOrdering = false;
};

// Swap in a deck's page table and go to its first slide.
void openDeck(Decks& d, const tubes::InstructionSlide* pages, int count,
              bool nav = true);


// Everything about the classroom that moves. `1b2e:0a11` is the same routine
// on every screen that shows the projector, so the slide's position belongs
// with the professor and the roll rather than to whichever screen is up.
struct Classroom {
    int8_t briefDecor = 1;
    int lastBackdrop = tubes::kNoLastBackdrop;   // DS:0x2056, seeded 0xff
    bool backdropPinned = false;
    tubes::ScreenRoll screenRoll;
    tubes::ProfessorIdle profIdle;
    tubes::PascalRandom sceneRng{1u};
    tubes::JokeSlide joke;
    bool slideDropped = false;
    int slideFrame = 0;          // index into kSlideDrop while dropping
    float slideAccum = 0.0f;
};

// The slide is still on its way down.
bool slideIsDropping(const Classroom& c);

// Where the slide sits this frame: its rest position, or a step of the drop.
tubes::SlideFrame slidePos(const Classroom& c);

// The classroom's moving parts, gathered once a frame.
tubes::ScenePose poseOf(const Classroom& c);

}  // namespace tubes
