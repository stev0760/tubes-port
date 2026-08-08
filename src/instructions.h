// The Instructions slideshow, `1b2e:2d63`.
//
// Twenty-one slides of the game's own documentation. This project twice found
// here an answer it was deriving the hard way: the test tube holding five, and
// Flashium's wildcard.
//
// The slides are data, extracted rather than transcribed:
// `tools/gen_instructions.py` reads the disassembly and emits
// `instructions.cpp`. 152 strings typed by hand would be 152 chances to
// mistype a line of the original's documentation and never notice.
//
// The structure is a straight run, not a dispatch. Each slide draws, then
// waits at `1b2e:0e37(30)` - the same professor's key-wait the briefing uses,
// with a thirty-second give-up - and branches:
//
//     key = 2   leave the slideshow          { ESC }
//     key = 5   the previous slide           { Up }
//     otherwise the next one                 { Down, or anything else }
//
// Slide 1's "previous" arm jumps to its own wait rather than anywhere else,
// because there is no slide before it. That is the original's own handling of
// the edge and it is why the port clamps instead of wrapping.
#ifndef TUBES_INSTRUCTIONS_H
#define TUBES_INSTRUCTIONS_H

#include <cstdint>

namespace tubes {

// One thing drawn on a slide. `kText` and `kCentred` are `2321:049b` and
// `2321:05da`; `kAtom` is `2321:0905`, the masked sprite draw, indexing the
// game's own ball table at `DS:0x1da6 + 4 * type`.
struct InstructionItem {
    enum Kind : uint8_t { kText, kCentred, kAtom };
    Kind kind;
    int x;              // kCentred: the left edge of the span, which is 0
    int y;
    // kText/kCentred: the colour. kAtom: the atom type, or one of the two
    // negatives below for the slideshow's own sprites.
    int colour;
    uint8_t mode;       // `2000:35ec`'s colour walk - see font.h
    // 0 is TINY6X8 and 1 the heading font. The slideshow never changes font;
    // the credits alternate, names in the big one and roles in the small.
    uint8_t font;
    const char* text;   // null for kAtom
};

// `1b2e:1f64` and `1b2e:1f71`: the two sprites the slideshow loads for itself,
// both drawn at (75, 50) on the "Detailed Instructions" slide.
constexpr int kInstrTestTube1 = -1;   // TESTUBE1.CSP
constexpr int kInstrTestTubeS = -2;   // TESTUBES.CSP

struct InstructionSlide {
    const InstructionItem* items;
    int count;
};

constexpr int kInstructionSlideCount = 21;
extern const InstructionSlide kInstructionSlides[kInstructionSlideCount];

// The two navigation lines. `1b2e:2e12` draws them once, before the first
// slide, and nothing ever clears them - so they are on screen for every slide
// and belong to the screen rather than to any one of them. The credits at
// `1b2e:4191` do exactly the same thing with the same two strings.
// `0x76` is the cyan, and 184 is below the blackboard - the board is 152 tall
// at y 12, so it ends at 163 and these sit on the black floor under it. That
// is the game's own convention for "how to work this screen", and it reads far
// better than chalk on green.
constexpr uint8_t kInstrNavColour = 118;   // 0x76
constexpr int kInstrNavY = 184;
constexpr int kInstrNavY2 = 192;

constexpr InstructionItem kInstructionNav[2] = {
    {InstructionItem::kCentred, 0, kInstrNavY, kInstrNavColour, 2, 0,
     "Up - Previous Slide"},
    {InstructionItem::kCentred, 0, kInstrNavY2, kInstrNavColour, 2, 0,
     "Down - Next Slide"},
};

// ---------------------------------------------------------------------------
// The Credits, `1b2e:411b`
// ---------------------------------------------------------------------------
//
// Four pages on the same classroom scene, with the same navigation and the
// same key rules. Two people and a technical note. The note is worth
// reading, because the game says of itself:
//
//     "Tubes was written in Borland Pascal v7, and uses a planar
//      320x200x256 for the multiple pages."
//
// Borland Pascal 7 and Mode X, from the program's own mouth. This project
// assumed both from the first session and neither was ever confirmed by the
// game until now.
constexpr int kCreditPageCount = 4;
extern const InstructionSlide kCreditPages[kCreditPageCount];

// `1b2e:2db0` sets TINY6X8 for the whole slideshow - advance 6, peak 4 - and
// never changes it. Every x in the table is measured against that.
constexpr int kInstrFontAdvance = 6;
constexpr int kInstrFontPeak = 4;

// The give-up, `1b2e:2f21`'s argument to `1b2e:0e37`: thirty seconds a slide.
constexpr int kInstrWaitSeconds = 30;

}  // namespace tubes

#endif  // TUBES_INSTRUCTIONS_H
