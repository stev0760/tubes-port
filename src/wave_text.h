// The briefing screen's text and layout, `1000:86b8` and the 25 objective
// routines it dispatches to.
//
// This file is the one place in `src/` that holds the game's own prose. It is
// deliberately isolated: the strings are Pascal ShortStrings in the code image
// around `0x62a0`..`0x8600`, and if this project ever reads them out of the
// user's `TUBES.EXE` at runtime instead, only this file changes.
//
// Everything here is transcribed from the binary, not retyped from a
// screenshot: the literals come from the image at the offsets the routines
// push, and every x, y, colour and mode is the literal the routine pushes.
// The lines are 28 columns wide because the original's are - the double spaces
// inside them are the game's own justification, not typos.

#pragma once

#include <cstdint>

#include "wave.h"

namespace tubes {

// How a line is built. `1000:86b8`'s routines compose at most one number and
// one colour name into a line, always in this order.
enum class BriefFmt : uint8_t {
    kLiteral,     // a() alone
    kCount,       // a() + Str(count, 2) + b()
    kCountName,   // a() + Str(count, 2) + b() + Name(colour, 9) + c()
    kName,        // a() + Name(colour, 9) + b()
    kNameWide,    // a() + Name(colour, 10) + b()   - the disabled element
    kNameOnly,    // the colour name, nothing else
};

struct BriefLine {
    int16_t x;        // -1 draws centred across 0..319
    int16_t y;
    uint8_t colour;   // 155 for the objective, 169 for the modifier
    uint8_t mode;
    BriefFmt fmt;
    const char* a;
    const char* b;
    const char* c;
};

// The illustration under the text. All full-size, from the ordinary ball
// table - unlike the Task Display's, which are half-size.
enum class BriefBallKind : uint8_t {
    kRequired,   // the wave's colour
    kCrystal,    // type 18
    kRandom,     // `Random(8) + 1`, rolled at draw time - `1000:632f`
    kMarker,     // `MARKER.CSP` over the ball before it
};

struct BriefBall {
    int16_t x;
    int16_t y;
    BriefBallKind kind;
};

struct Briefing {
    const BriefLine* lines;
    int lineCount;
    const BriefBall* balls;
    int ballCount;
};

// The screen's own furniture, `1000:86b8`.
constexpr int kBriefTitleY = 45;      // 'Wave <n>', centred, colour 159 mode 3
constexpr int kBriefRuleY = 48;       // '____________'
constexpr int kBriefDropsX = 76;
constexpr int kBriefDropsY = 150;     // 'You are allowed <n> drops.'
constexpr uint8_t kBriefTitleColour = 159;
constexpr uint8_t kBriefTitleMode = 3;
constexpr uint8_t kBriefBodyColour = 155;
constexpr uint8_t kBriefModifierColour = 169;

extern const char* const kBriefTitle;      // 'Wave '
extern const char* const kBriefRule;       // the underline
extern const char* const kBriefDropsA;     // 'You are allowed '
extern const char* const kBriefDropsB;     // ' drops.'

const Briefing& briefingFor(Objective o);

// The seven element names, `DS:0xbbc` at stride 16, indexed 1..7. Index 0 is
// not a name - the table's element 0 overlaps other data in `DGROUP`.
extern const char* const kElementNames[8];

}  // namespace tubes
