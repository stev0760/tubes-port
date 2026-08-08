#pragma once

// The shareware edition's "Ordering Info" deck, `1ac3:4889`.
//
// Four pages of text - the registration pitch, the phone/fax/BBS numbers, a
// blurb about the Software Creations BBS, and a closing nav slide. It shares
// the Instructions' item and page types because it is the same kind of screen
// drawn by the same routines. Only the text-unit entry points differ, and only
// because the shareware image's segments sit 0x120 bytes higher.
//
// It has no illustrations - the deck never calls the sprite routine, unlike
// the Instructions and the Credits.
//
// Reached two ways in the original, both from `entry`:
//
//     menu item 8       `1000:abad  CALLF 0x1000:f4b9`
//     Exit, item 10     `1000:ac01  CALLF 0x1000:f4b9`, then Halt
//
// so choosing Exit runs the whole deck before the program ends. `src/ordering.cpp`
// is generated; see its header for the command.

#include "instructions.h"

namespace tubes {

constexpr int kOrderingPageCount = 4;
extern const InstructionSlide kOrderingPages[kOrderingPageCount];

// The shareware's end-of-game screen, `1000:8df8` - one page, reached from
// `1000:9f56` when the wave reaches 25, where the registered build reaches its
// Nobel ending at `1000:9499` on wave 75.
//
// The two are not interchangeable. The shareware image has no `PRIZE.GFX` and
// none of the registered ending's text; the registered image has none of this
// screen's. Substituting either for the other would be inventing an ending for
// an edition that does not have one.
//
// It waits with `1ac3:0b8f` - the terminal wait the registered ending also
// uses - rather than the paging wait the decks use, which is what says it is
// one screen and not the first page of something.
constexpr int kRegistrationPageCount = 1;
extern const InstructionSlide kRegistrationPages[kRegistrationPageCount];

}  // namespace tubes
