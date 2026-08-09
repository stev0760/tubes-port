// The frame loop's own state. See uistate.h.

#include "uistate.h"

namespace tubes {

void openDeck(Decks& d, const tubes::InstructionSlide* pages, int count,
              bool nav) {
    d.instrPages = pages;
    d.instrPageCount = count;
    d.instrNav = nav;
    d.instrSlide = 0;
}

}  // namespace tubes
