#pragma once

// The shareware edition's "Ordering Info" deck, `1ac3:4889`.
//
// Four pages of text - the registration pitch, the phone/fax/BBS numbers, a
// blurb about the Software Creations BBS, and a closing nav slide. It shares
// the Instructions' item and page types because it is the same kind of screen
// drawn by the same routines; only the text-unit entry points differ, and only
// because the shareware image's segments sit 0x120 bytes higher.
//
// It has NO illustrations - the deck never calls the sprite routine, unlike
// the Instructions and the Credits.
//
// Reached two ways in the original, both from `entry`:
//
//     menu item 8       `1000:abad  CALLF 0x1000:f4b9`
//     Exit, item 10     `1000:ac01  CALLF 0x1000:f4b9`, then Halt
//
// so choosing Exit runs the whole deck before the program ends. `src/ordering.cpp`
// is GENERATED; see its header for the command.

#include "instructions.h"

namespace tubes {

constexpr int kOrderingPageCount = 4;
extern const InstructionSlide kOrderingPages[kOrderingPageCount];

}  // namespace tubes
