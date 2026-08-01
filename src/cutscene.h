// The opening cutscene, `1b2e:1651` - the story of Dr. Lanny B. Brilliant.
//
// `1000:b224` runs it ONCE, after the two splashes and immediately before the
// title screen is first shown. The main loop's own `JMP 1000:b236` goes back
// to the title call, so this is a boot-time screen and not part of the cycle;
// the second call at `1000:b28b` is the attract arm and is a different thing.
//
// Five pages of the game's own text over a blackboard, with Lanny writing at
// one side and a beaker at the other. The pages and their text are EXTRACTED,
// not transcribed - `tools/gen_cutscene.py` reads the disassembly and emits
// `cutscene.cpp`, the third screen through the same generator after the
// Instructions and the Credits.
#ifndef TUBES_CUTSCENE_H
#define TUBES_CUTSCENE_H

#include <cstdint>

namespace tubes {

// One thing drawn on a page.
//
//   kText  `2321:049b`, at colour 0x9a in TINY6X8, mode 1
//   kAtom  `2321:0905`, the masked sprite draw, indexing the game's own ball
//          table at `DS:0x1da6 + 4 * type` - so the eight elements are
//          introduced by name AND by their real sprite
//   kBar   `2321:0ac0`, the bevelled panel the text sits in
struct CutsceneItem {
    enum Kind : uint8_t { kText, kAtom, kBar };
    Kind kind;
    int x;
    int y;
    int a;              // kText: colour; kAtom: ball type; kBar: width
    int b;              // kText: the colour-walk mode; kBar: height
    const char* text;   // null unless kText
};

// One of `1b2e:0f46`'s two animation tracks.
//
// The player is a TWO-track sequencer: it runs both at once off one frame
// clock, `Delay(10)` a frame - seven frames a second - and either may be
// idle. Its two frame counters are GLOBALS, `[DS:0x1d6e]` and `[DS:0x1d6f]`,
// 1-based and wrapping back to 1, which is how a page can start a track part
// way in by seeding one before the call.
//
// `count` is also a flag. A track whose count is 25 (track A) or 16 (track B)
// STOPS when it wraps instead of looping - those two numbers are exactly the
// lengths of the two one-shot sequences, which is what makes the explosion
// run to its end and stay there.
struct CutsceneTrack {
    int x;
    int y;
    int w;              // the frame size, passed per call because the list
    int h;              // holds frames of two different sizes
    int count;          // 0 = idle
    int soundFrame;     // the frame the sound fires on; -1 = whenever idle
    const char* sound;  // null if the track is silent
};

struct CutscenePage {
    const CutsceneItem* items;
    int count;
    int seconds;            // `[BP+0x32]`: `n * 7` iterations of `Delay(10)`
    CutsceneTrack a;        // Lanny, writing
    CutsceneTrack b;        // the beaker, or the eighth element's atom
    const char* soundAfter; // played as the page turns
};

constexpr int kCutscenePageCount = 5;
extern const CutscenePage kCutscenePages[kCutscenePageCount];

// The two frame lists, recovered from the loads and the pointer copies in
// `1b2e:1651`. Ten `WRITE*.GFX` fill 26 slots and sixteen `EXPLOD*.GFX` fill
// 17, because the caller COPIES pointers - which is how a pose is held for
// seven ticks without any delay parameter existing.
//
// The counter indexes these directly and starts at 0, so element 0 is drawn
// once and the loop afterwards runs 1..count.
constexpr int kWriteFrameCount = 26;
constexpr int kBlowFrameCount = 17;
extern const char* const kWriteFrames[kWriteFrameCount];
extern const char* const kBlowFrames[kBlowFrameCount];

// `2321:0ac0`, the bevelled panel, transliterated: nine fills, a body in 7
// with highlights in 15 and shadows in 8. Extracted from the listing rather
// than eyeballed, because "a grey box with a border" is exactly the kind of
// thing that looks right and is three pixels wrong everywhere.
struct PanelFill {
    int8_t dx, dy;          // offsets from the corner named below
    int8_t dw, dh;          // added to the panel's w, h when useW/useH
    uint8_t fromRight;      // measure dx from x + w rather than from x
    uint8_t fromBottom;     // measure dy from y + h rather than from y
    uint8_t useW, useH;     // 0 = the side is one pixel thick
    uint8_t colour;
};

constexpr int kPanelFillCount = 9;
extern const PanelFill kPanelFills[kPanelFillCount];

// `1b2e:1a59`: the blackboard goes to (0, 12), the same held image and the
// same y the briefing and the stats screen use.
constexpr int kCutsceneBoardY = 12;

// `1b2e:1a80`: `WRITE0.GFX` is loaded into a slot of its OWN, outside both
// frame lists, and blitted once at (87, 150) through `2321:0711` - the
// transparent blit. It is Lanny's base pose, and the animated frames are
// drawn OVER his top half at (86, 122): 28x41 reaches y 163 and 28x66
// reaches 188, so his legs from 188 down are only ever this.
//
// That is the same two-draw arrangement the professor uses on the
// Instructions screen, and missing it was the port's entire disagreement
// with the original - 256 pixels, all of them his lower half.
constexpr int kCutsceneBaseX = 87;
constexpr int kCutsceneBaseY = 150;

// `1b2e:1a62` blits a second held image at (57, 26) from `DS:0x2060`. It
// leaves no mark on any captured page, so the pointer is nil in this
// context - `2321:0711` returns at once on a nil segment. Recorded because
// "there is a call the port does not make" should be written down even when
// the pixel diff says it does not matter.

// The cutscene's own music, `[DS:0x212c]`.
constexpr const char* kCutsceneMusic = "CLASS.MUS";

// `1b2e:1adb`: the hold between the fade-in and the first page.
constexpr int kCutsceneOpenDelay = 45;

// `1b2e:0fe9`: ten retraces a frame, so seven frames a second.
constexpr int kCutsceneFrameRetraces = 10;

// `1b2e:1e6b`: page 4 erases both animations when it ends, so page 5 is the
// text alone. The rectangles are the two tracks' own.
constexpr int kCutsceneClearPage = 4;

}  // namespace tubes

#endif  // TUBES_CUTSCENE_H
