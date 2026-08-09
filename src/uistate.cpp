// The frame loop's own state. See uistate.h.

#include "uistate.h"

namespace tubes {

bool slideIsDropping(const Classroom& c) {
    return !c.slideDropped && c.slideFrame < tubes::kSlideDropFrames;
}

tubes::SlideFrame slidePos(const Classroom& c) {
    if (!slideIsDropping(c)) {
        return tubes::SlideFrame{tubes::kSlideX, tubes::kSlideY};
    }
    return tubes::kSlideDrop[c.slideFrame];
}

tubes::ScenePose poseOf(const Classroom& c) {
    tubes::ScenePose p;
    p.frameH = c.screenRoll.height();
    p.slideDropping = slideIsDropping(c);
    p.profFrame = tubes::pointerFrameFor(c.profIdle.wave);
    // `1b2e:084e` is a blocking routine called from inside `1b2e:0a11`, which
    // itself runs before the key wait - so while the gag is up the professor
    // is not talking, and no mouth is stamped over the face it replaces.
    p.mouthFrame = c.joke.active() ? 0 : c.profIdle.mouthFrame();
    p.jokeSlide = c.joke.showFlash();
    p.jokeFace = c.joke.showFace();
    const tubes::SlideFrame s = slidePos(c);
    p.slideX = s.x;
    p.slideY = s.y;
    return p;
}

void openDeck(Decks& d, const tubes::InstructionSlide* pages, int count,
              bool nav) {
    d.instrPages = pages;
    d.instrPageCount = count;
    d.instrNav = nav;
    d.instrSlide = 0;
}

}  // namespace tubes
