// Tests for the beaker: matching, the fade, and settling.
//
// The scripted player in --auto rarely produces a match by chance with eleven
// atom types, so the rules need testing directly.
//
// These were rewritten when Board became the original's three planes. The
// difference is not cosmetic: a match no longer removes anything, it MARKS,
// and the marked cell then climbs one fade frame per step until it empties.
// So a test that wants a cleared board has to step the board, not call a
// "resolve" that runs to completion.

#include <cstdio>
#include <string>
#include <vector>

#include "board.h"
#include "font.h"
#include "game.h"
#include "screen.h"
#include "scr.h"
#include "sfx.h"
#include "menu.h"
#include "wave.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

// Builds a board from rows of characters, '.' meaning empty. Row 0 is the
// top, matching Board's own convention.
tubes::Board make(const std::vector<std::string>& rows) {
    int h = static_cast<int>(rows.size());
    int w = h ? static_cast<int>(rows[0].size()) : 0;
    tubes::Board b(w, h);
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            char ch = rows[r][c];
            if (ch != '.') b.set(c, r, static_cast<int8_t>(ch - '0'));
        }
    }
    return b;
}

// One step, then count what the match pass marked.
int markedAfterStep(tubes::Board& b) {
    b.step();
    int n = 0;
    for (int r = 0; r < b.rows(); ++r) {
        for (int c = 0; c < b.cols(); ++c) {
            if (b.isMarked(c, r)) ++n;
        }
    }
    return n;
}

// Run until nothing is marked and nothing moves. The cap is generous: a fade
// is 8 steps and gravity moves one row a step, so a full cascade is tens of
// frames, not hundreds.
tubes::BoardStep settle(tubes::Board& b, int cap = 400) {
    tubes::BoardStep total;
    for (int i = 0; i < cap; ++i) {
        tubes::BoardStep s = b.step();
        total.award += s.award;
        total.runs += s.runs;
        bool busy = s.award > 0 || s.settled;
        for (int r = 0; r < b.rows() && !busy; ++r) {
            for (int c = 0; c < b.cols() && !busy; ++c) {
                if (b.isMarked(c, r)) busy = true;
            }
        }
        if (!busy) break;
    }
    return total;
}

void testHorizontal() {
    tubes::Board b = make({"....", "....", "111."});
    check(markedAfterStep(b) == 3, "horizontal run of 3 detected");
    settle(b);
    check(b.count() == 0, "horizontal run cleared");
}

void testVertical() {
    tubes::Board b = make({"2...", "2...", "2..."});
    check(markedAfterStep(b) == 3, "vertical run of 3 detected");
    settle(b);
    check(b.count() == 0, "vertical run cleared");
}

void testDiagonalDown() {
    tubes::Board b = make({"3...", ".3..", "..3."});
    check(markedAfterStep(b) == 3, "descending diagonal detected");
}

void testDiagonalUp() {
    tubes::Board b = make({"..4.", ".4..", "4..."});
    check(markedAfterStep(b) == 3, "ascending diagonal detected");
}

void testRunOfTwoIgnored() {
    tubes::Board b = make({"....", "....", "55.."});
    check(markedAfterStep(b) == 0, "run of 2 is not a match");
}

void testLongRun() {
    tubes::Board b = make({"......", "......", "666666"});
    check(markedAfterStep(b) == 6, "run of 6 marks all six");
}

void testMixedRunBoundary() {
    // A 3-run must not absorb the differently coloured neighbour beside it.
    tubes::Board b = make({"....", "....", "7772"});
    check(markedAfterStep(b) == 3, "match stops at a colour change");
}

// --- the three planes -------------------------------------------------
//
// A cell holds `type + 19 * fadeFrame`, and `1000:255a` adds 19 a step and
// empties the cell once it passes 152.

void testFadeEncoding() {
    tubes::Board b = make({"....", "....", "111."});
    b.step();
    check(b.at(0, 2) == 1 + 19, "a marked cell advances one fade frame");
    check(b.typeAt(0, 2) == 1, "the type survives inside the composite");
    check(tubes::cellFadeFrame(b.at(0, 2)) == 1, "fade frame reads back as 1");

    b.step();
    check(b.at(0, 2) == 1 + 38, "and again on the next step");
    check(b.typeAt(0, 2) == 1, "still Redium underneath");
}

void testFadeClearsAfterEightSteps() {
    tubes::Board b = make({"....", "....", "111."});
    // 1 + 19*8 = 153, the first value above 152, so the eighth step empties it.
    for (int i = 0; i < 7; ++i) b.step();
    check(b.count() == 3, "still present after seven steps");
    check(b.at(0, 2) == 1 + 19 * 7, "at fade frame 7");
    b.step();
    check(b.count() == 0, "emptied on the eighth");
    check(!b.isMarked(0, 2), "and the mark is cleared with it");
}

void testFadingCellCannotRematch() {
    // A cell mid-fade holds a value above 8, and the matcher's seed test is
    // `1..8` while its neighbour test bails on anything above 8 - so a fading
    // run cannot be matched a second time. No separate "is clearing" flag
    // exists anywhere in the original, and none is needed.
    tubes::Board b = make({"....", "....", "111."});
    tubes::BoardStep first = b.step();
    check(first.award == 500, "a horizontal 3 pays 500 once");
    tubes::BoardStep second = b.step();
    check(second.award == 0, "the fading run does not pay again");
}

void testGravityMovesOneRowPerStep() {
    // `1000:25b3` walks the destination cursor and the source row down
    // together, so an atom falls at most one row a frame. That is what makes
    // the beaker visibly settle instead of snapping.
    tubes::Board b(3, 5);
    b.set(0, 0, tubes::kRedium);
    b.step();
    check(b.at(0, 1) == tubes::kRedium, "fell one row");
    check(b.at(0, 0) == tubes::kEmpty, "and left the row above");
    b.step();
    check(b.at(0, 2) == tubes::kRedium, "fell one more");
    settle(b);
    check(b.at(0, 4) == tubes::kRedium, "settles on the floor");
}

void testMarkerTravelsWithTheAtom() {
    // All three planes move together in the gravity pass, so a cell that is
    // mid-fade keeps fading as it falls.
    tubes::Board b(4, 5);
    for (int c = 0; c < 3; ++c) b.set(c, 4, tubes::kRedium);
    b.set(0, 3, tubes::kGreenium);
    b.step();                       // marks the row of Redium
    check(b.isMarked(0, 4), "the run is marked");
    check(b.typeAt(0, 3) == tubes::kGreenium, "the Greenium has not moved yet");
    settle(b);
    check(b.count() == 1, "only the Greenium survives");
    check(b.at(0, 4) == tubes::kGreenium, "and it settled to the floor");
}

void testObjectivePlane() {
    // Plane C is inert unless the wave enables it; then clearing a marked
    // objective cell consumes the marker and ticks the wave target down.
    tubes::Board b = make({"....", "....", "111."});
    b.setObjective(1, 2, true);
    check(b.isObjective(1, 2), "objective marker set");
    b.step();
    check(b.objectivesCleared() == 0, "inert while the wave mode is off");

    tubes::Board w = make({"....", "....", "111."});
    w.setObjective(1, 2, true);
    w.setObjectiveMode(true);
    w.step();
    check(w.objectivesCleared() == 1, "counted once the wave mode is on");
    check(!w.isObjective(1, 2), "and the marker is consumed");
}

void testDisabledElement() {
    // A wave modifier: an element that still spawns but cannot be cleared.
    tubes::Board b = make({"....", "....", "111."});
    b.setDisabledType(tubes::kRedium);
    check(markedAfterStep(b) == 0, "the disabled element does not match");

    tubes::Board other = make({"....", "....", "222."});
    other.setDisabledType(tubes::kRedium);
    check(markedAfterStep(other) == 3, "other elements still match");
}

// --- scoring ----------------------------------------------------------

void testAwardPerSeed() {
    // The award is added once per SEED position, so a run of four pays twice.
    // "4 atom molecules count as 2 chains" is a literal description of that.
    tubes::Board three = make({".....", ".....", "111.."});
    check(three.step().award == 500, "horizontal 3 pays 500");

    tubes::Board four = make({".....", ".....", "1111."});
    check(four.step().award == 1000, "horizontal 4 pays 500 twice");

    tubes::Board five = make({".....", ".....", "11111"});
    check(five.step().award == 1500, "horizontal 5 pays 500 three times");
}

void testAwardByOrientation() {
    tubes::Board v = make({"1...", "1...", "1..."});
    check(v.step().award == 250, "vertical pays 250");

    tubes::Board d = make({"1...", ".1..", "..1."});
    check(d.step().award == 1000, "diagonal pays 1000");
}

void testDistinctRunsAreTheMultiplier() {
    // The multiplier counts DISTINCT runs, not seeds, so a run of four is one
    // run however many seed positions it has.
    tubes::Board four = make({".....", ".....", "1111."});
    check(four.step().runs == 1, "a run of four counts as one run");

    // Two separated runs in the same frame count twice.
    tubes::Board two = make({"........", "........", "111.222."});
    check(two.step().runs == 2, "two separate runs count twice");
}

void testDropAndOverflow() {
    tubes::Board b(3, 3);
    check(b.dropRow(0) == 2, "first drop lands on the floor");
    b.drop(0, 1);
    check(b.dropRow(0) == 1, "second drop stacks above it");
    b.drop(0, 2);
    b.drop(0, 3);
    check(b.dropRow(0) == -1, "full column reports no free row");
    check(!b.drop(0, 4), "dropping into a full column fails");
    check(b.overflowing(), "column reaching the top overflows");
}

// Atoms are numbered as the original numbers them: 1..7 the ordinary colours,
// 8 Flashium, 9 AntiMatter, 11 Xenon. Only 1..8 take part in matching.
void testInertSpecialsDoNotMatch() {
    tubes::Board b(4, 3);
    for (int c = 0; c < 3; ++c) b.set(c, 2, tubes::kXenon);
    check(markedAfterStep(b) == 0, "three Xenon do not match");

    tubes::Board ok(4, 3);
    for (int c = 0; c < 3; ++c) ok.set(c, 2, tubes::kCyanium);
    check(markedAfterStep(ok) == 3, "three Cyanium in the same shape do match");

    tubes::Board split = make({"....", "....", "1111"});
    split.set(2, 2, tubes::kXenon);
    check(markedAfterStep(split) == 0, "Xenon breaks a run rather than joining it");
}

// Flashium (8) is a wildcard, and in the original it ADOPTS: a Flashium seed
// becomes whatever the next non-empty cell is, and the run is that colour from
// then on. That is `1000:1b53`, and it is why one Flashium can complete runs
// of two different colours at once.
void testFlashiumWildcard() {
    tubes::Board mixed = make({"....", "....", "181."});
    check(markedAfterStep(mixed) == 3, "Flashium completes a run of one colour");

    tubes::Board pure = make({"....", "....", "888."});
    check(markedAfterStep(pure) == 3, "three Flashium match on their own");

    tubes::Board bridge = make({"....", "....", "1833"});
    check(markedAfterStep(bridge) == 3, "Flashium adopts the colour it can complete");

    tubes::Board split = make({"....", "....", ".183"});
    check(markedAfterStep(split) == 0, "neither side reaches three");

    tubes::Board shared = make({".....", ".....", "11833"});
    check(markedAfterStep(shared) == 5, "a shared wildcard completes both runs");

    tubes::Board diag = make({"..3.", ".8..", "3...", "...."});
    check(markedAfterStep(diag) == 3, "Flashium completes a diagonal");
}

void testCascade() {
    // The 2s sit at staggered heights so they do not match initially. Once the
    // bottom row of 1s clears they settle onto the floor and form a second
    // match, which the per-frame update resolves on its own.
    tubes::Board b = make({"....", ".2..", "2.2.", "1111"});
    tubes::Board probe = b;
    check(markedAfterStep(probe) == 4, "only the bottom row matches to begin with");

    tubes::BoardStep total = settle(b);
    check(total.runs == 2, "cascade forms two runs in all");
    check(b.count() == 0, "cascade clears the board");
}

// --- the specials, from 1000:2790 -------------------------------------

void testAntiMatterBlastsThreeByThree() {
    tubes::Board b(6, 5);
    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 6; ++c) b.set(c, r, tubes::kXenon);
    }
    b.set(2, 2, tubes::kAntiMatter);
    tubes::BoardStep s = b.step();
    check(s.blast, "the blast is reported");
    check(s.soundType == tubes::kAntiMatter, "and plays AFADE");

    // Every cell of the 3x3 centred on (2,2) is rewritten to AntiMatter's own
    // type and marked, which is what makes the blast animation need no case.
    int hit = 0;
    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 6; ++c) {
            if (b.isMarked(c, r)) {
                ++hit;
                check(b.typeAt(c, r) == tubes::kAntiMatter,
                      "a caught cell became AntiMatter");
            }
        }
    }
    check(hit == 9, "a centred blast catches nine cells");
    check(!b.isMarked(5, 2), "and nothing outside the block");
}

void testAntiMatterClipsAtTheEdges() {
    // The original narrows the span rather than clamping both ends, so a
    // corner blast is 2x2, not 3x3 shifted inward.
    tubes::Board b(6, 5);
    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 6; ++c) b.set(c, r, tubes::kXenon);
    }
    b.set(0, 0, tubes::kAntiMatter);
    b.step();
    int hit = 0;
    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 6; ++c) if (b.isMarked(c, r)) ++hit;
    }
    check(hit == 4, "a corner blast catches four cells");

    tubes::Board e(6, 5);
    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 6; ++c) e.set(c, r, tubes::kXenon);
    }
    e.set(5, 4, tubes::kAntiMatter);      // the opposite corner
    e.step();
    hit = 0;
    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 6; ++c) if (e.isMarked(c, r)) ++hit;
    }
    check(hit == 4, "the far corner too");
}

void testBlockerFillsItsColumnAbove() {
    tubes::Board b(6, 5);
    for (int c = 0; c < 6; ++c) b.set(c, 4, tubes::kRedium);
    b.set(2, 2, tubes::kRedium);
    b.set(2, 3, tubes::kGreenium);
    b.set(2, 4, tubes::kBlocker);
    b.step();
    check(b.typeAt(2, 4) == tubes::kXenon, "the Blocker itself became Xenon");
    check(b.typeAt(2, 3) == tubes::kXenon, "the cell above did too");
    check(b.typeAt(2, 2) == tubes::kXenon, "and the one above that");
    check(b.typeAt(1, 4) == tubes::kRedium, "neighbouring columns untouched");
}

void testBlockerIsGated() {
    tubes::Board b(6, 5);
    b.set(2, 4, tubes::kBlocker);
    b.set(2, 3, tubes::kRedium);
    b.setSpecialsEnabled(false);
    b.step();
    check(b.typeAt(2, 3) == tubes::kRedium, "DS:0x1d48 off leaves the column alone");
}

void testConvertorConvertsByTypeBoardWide() {
    // Stronger than the published description: it takes the type of the ONE
    // cell below it and converts every atom of that type anywhere.
    tubes::Board b(6, 5);
    b.set(0, 4, tubes::kGreenium);
    b.set(0, 3, tubes::kConvertor);
    b.set(3, 4, tubes::kGreenium);      // far away, same colour
    b.set(4, 4, tubes::kRedium);        // different colour
    b.step();
    check(b.typeAt(0, 3) == tubes::kXenon, "the Convertor became Xenon");
    check(b.typeAt(0, 4) == tubes::kXenon, "the cell it landed on converted");
    check(b.typeAt(3, 4) == tubes::kXenon, "and every other Greenium, board-wide");
    check(b.typeAt(4, 4) == tubes::kRedium, "other colours untouched");
}

void testConvertorNeedsAnOrdinaryVictim() {
    tubes::Board b(6, 5);
    b.set(0, 4, tubes::kXenon);
    b.set(0, 3, tubes::kConvertor);
    b.set(3, 4, tubes::kXenon);
    b.step();
    check(b.typeAt(0, 3) == tubes::kXenon, "it still becomes Xenon itself");
    check(b.typeAt(3, 4) == tubes::kXenon, "but does not chain off a Xenon");
}

void testSettledConsumablesGoInert() {
    // Bonus, Multiplier, EvilMultiplier and Filler do their work when the tube
    // CATCHES them. Any that reach the glass are leftovers and turn to Xenon.
    tubes::Board b(6, 5);
    b.set(0, 4, tubes::kBonus);
    b.set(1, 4, tubes::kMultiplier);
    b.set(2, 4, tubes::kEvilMultiplier);
    b.set(3, 4, tubes::kFiller);
    b.set(4, 4, tubes::kCrystal);
    b.step();
    for (int c = 0; c < 4; ++c) {
        check(b.typeAt(c, 4) == tubes::kXenon, "a settled consumable went inert");
    }
    check(b.typeAt(4, 4) == tubes::kCrystal, "the Crystal is left alone");
}

// --- regressions, both reported from play -----------------------------

void testFullBeakerDoesNotEndTheGame() {
    // The port used to end the game the instant a column reached the top,
    // which froze it solid with no message - update() returns immediately once
    // gameOver_ is set. It reads as a crash, and it was invented: `1000:3a67`
    // sets its game-over flag only when the drop counter wraps past zero or
    // the wave objective is met.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 99);
    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 6; ++c) g.boardMutable().set(c, r, tubes::kXenon);
    }
    for (int i = 0; i < 600; ++i) g.update(0, 1.0f / 60.0f);
    check(g.board().overflowing(), "the beaker really is full");
    check(!g.gameOver(), "a full beaker does not end the game");
}

void testSpeedBoostNeedsHolding() {
    // `1000:1906` reloads every atom's velocity from the session base on every
    // frame, so the Down/B boost lasts exactly one frame and has to be held.
    // Column 6 lands at x=161, which is the tube's stop index 3; filling the
    // tube stops the atom being caught so it just descends.
    //
    // It has to be measured ABOVE y = 50. Below that `1000:13ed` ignores the
    // velocity entirely and falls a flat 9 px a frame, so a boost test down
    // there measures nothing - which is what the previous version of this test
    // did, at y = 70, and it passed by coincidence: the "boosted" 9 px a frame
    // it was checking for is what an unboosted atom does there anyway.
    auto descend = [](uint8_t btn, int frames) {
        tubes::Game h(6, 5, tubes::Difficulty::k101, 5);
        h.setTubeColumn(3);
        h.setTubeAtoms(std::vector<int8_t>(5, tubes::kRedium));
        tubes::Falling a;
        a.state = tubes::atomstate::kDescend;
        a.column = 6;
        a.colour = tubes::kRedium;
        a.x = tubes::kAtomColumnX[6];
        a.y = 5;
        a.velocity = 0x100;
        h.setAtom(6, a);
        for (int f = 0; f < frames; ++f) h.update(btn, 1.0f / tubes::kFrameHz);
        return h.atom(6).y - 5;
    };
    check(descend(0, 5) == 10, "released: 2 px a frame, the Tubes 101 base");
    check(descend(tubes::button::kDown, 5) == 45, "Down held: 9 px a frame");
    check(descend(tubes::button::kB, 5) == 45, "B does the same as Down");
}

void testBonusAtomIsFastByType() {
    // The same reload gives type 10 the fast velocity unconditionally, which
    // is why GOLDBALL visibly outruns everything else.
    tubes::Game h(6, 5, tubes::Difficulty::k101, 5);
    h.setTubeAtoms(std::vector<int8_t>(5, tubes::kRedium));
    tubes::Falling a;
    a.state = tubes::atomstate::kDescend;
    a.column = 1;
    a.colour = tubes::kBonus;
    a.x = tubes::kAtomColumnX[1];
    a.y = 5;
    a.velocity = 0x100;
    h.setAtom(1, a);
    for (int f = 0; f < 5; ++f) h.update(0, 1.0f / tubes::kFrameHz);
    // 2 + 9*4: the reload happens at the END of the router, so the first frame
    // still runs at whatever the record held and every frame after is fast.
    // The same one-frame lag is in the original and is not worth hiding.
    check(h.atom(1).y - 5 == 38, "a Bonus atom runs at 9 px a frame unprompted");
}

// --- the specials at CATCH time, 1000:180c ----------------------------

// Drives one atom of `type` into the test tube and runs it far enough to reach
// its slot. Column 6 descends to x=161, which is the tube's stop index 3, so
// parking the tube there is what makes the catch fire.
//
// The frames matter now. A catch puts the atom at the MOUTH and it slides down
// at 9 px a frame; the special does not fire until it arrives, and the arrival
// flag is read on the frame after it is set. Fifteen frames covers the longest
// slide (mouth to slot 1) comfortably. Nothing spawns in that window - the
// Tubes 101 period is 70 frames and the timer starts full.
//
// y = 55 is chosen so the first descent step lands inside the catch window:
// below y = 50 an atom falls a flat 9 px a frame, so 55 becomes 64, and 60..70
// is the whole of the window `1000:1423` allows.
void catchOne(tubes::Game& g, int8_t type,
              const std::vector<int8_t>& start) {
    g.setTubeColumn(3);
    g.setTubeAtoms(start);
    tubes::Falling a;
    a.state = tubes::atomstate::kDescend;
    a.column = 6;
    a.colour = type;
    a.x = tubes::kAtomColumnX[6];
    a.y = 55;
    a.velocity = 0x100;
    g.setAtom(6, a);
    for (int i = 0; i < 15; ++i) g.update(0, 1.0f / tubes::kFrameHz);
}

// Holds A long enough for one whole tipping animation - six frames, two per
// phase. A is level-triggered, so holding it just tips again.
void tipOnce(tubes::Game& g) {
    for (int i = 0; i < 6; ++i) g.update(tubes::button::kA, 1.0f / tubes::kFrameHz);
}

void testBonusCatchPaysAndGrows() {
    // 1000:07db. The caught atom becomes Flashium, the drop pool gains one,
    // and the award is a word of its own that GAINS 1000 each time and is then
    // paid whole - so the second Bonus of a session is worth 2000.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    catchOne(g, tubes::kBonus, {});
    check(g.tubeTypes() == std::vector<int8_t>{tubes::kFlashium},
          "a caught Bonus turns into Flashium");
    check(g.dropsRemaining() == 10, "and grants a drop - 9 seeded, 10 now");

    // The award ramps in over six frames; catchOne already ran ten, so it is
    // paid. One drop and one award, not one per frame the atom sat there -
    // the special re-enters every frame and the type rewrite is what stops it.
    for (int i = 0; i < 15; ++i) g.update(0, 1.0f / tubes::kFrameHz);
    check(g.score() == 1000, "the first Bonus pays exactly 1000");
    check(g.dropsRemaining() == 10, "and exactly one drop, not one a frame");

    // 1000:0846 adds 1000 to a word of its own and pays the whole word, so the
    // second Bonus of a session is worth 2000.
    const int before = g.score();
    catchOne(g, tubes::kBonus, g.tubeTypes());
    for (int i = 0; i < 15; ++i) g.update(0, 1.0f / tubes::kFrameHz);
    check(g.score() - before == 2000, "the second Bonus pays 2000");
}

void testMultiplierFillsTheTube() {
    // 1000:08d2 tops the tube up to five with Random(8)+1 - which is 1..8, so
    // Flashium is in the distribution.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    catchOne(g, tubes::kMultiplier, {tubes::kRedium});
    const std::vector<int8_t> t = g.tubeTypes();
    check(static_cast<int>(t.size()) == tubes::kTubeSlots,
          "a Multiplier fills the tube to five");
    check(t[0] == tubes::kRedium, "what was already in it stays put");
    bool ordinary = true;
    for (size_t i = 1; i < t.size(); ++i) {
        if (t[i] < tubes::kRedium || t[i] > tubes::kFlashium) ordinary = false;
    }
    check(ordinary, "and everything it adds is a 1..8 roll");
}

void testEvilMultiplierFillsWithXenon() {
    // 1000:0a27 is the same routine with the roll replaced by a literal 11,
    // and the Move copies that record upward - so every slot is Xenon.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    catchOne(g, tubes::kEvilMultiplier, {tubes::kRedium, tubes::kGreenium});
    const std::vector<int8_t> t = g.tubeTypes();
    check(t == std::vector<int8_t>({tubes::kRedium, tubes::kGreenium,
                                    tubes::kXenon, tubes::kXenon,
                                    tubes::kXenon}),
          "an Evil Multiplier fills the rest of the tube with Xenon");
}

void testFillerParksAnImmovableAtom() {
    // 1000:0b55 shifts slots 5 downto 2 up by one and writes type 17 into slot
    // 1, without touching the count. The Filler itself is at slot[count] and is
    // what falls off the top, so the tube keeps its depth and loses a slot.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    catchOne(g, tubes::kFiller, {tubes::kRedium, tubes::kGreenium});
    check(g.tubeTypes() == std::vector<int8_t>({tubes::kObstacle,
                                                tubes::kRedium,
                                                tubes::kGreenium}),
          "the Filler parks a 17 underneath and discards itself");

    // 1000:472a refuses to tip a 17, which is the whole of "permanently
    // reduces your capacity": the slot is dead for the rest of the session.
    // A is NOT edge-detected - `1000:4511` only gates it on the tube being
    // idle - so holding it tips once every six frames.
    for (int i = 0; i < 3; ++i) tipOnce(g);
    check(g.tubeTypes() == std::vector<int8_t>{tubes::kObstacle},
          "everything above it tips out and the 17 will not");
}

void testCatchSpecialsAreGated() {
    // `DS:0x1d48` guards the Multiplier, the Evil Multiplier and the Filler -
    // but not the Bonus, which sits outside the test at 1000:1821.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    g.boardMutable().setSpecialsEnabled(false);
    catchOne(g, tubes::kMultiplier, {});
    check(g.tubeTypes() == std::vector<int8_t>{tubes::kMultiplier},
          "a gated Multiplier just sits in the tube");

    tubes::Game h(6, 5, tubes::Difficulty::k101, 5);
    h.boardMutable().setSpecialsEnabled(false);
    catchOne(h, tubes::kBonus, {});
    check(h.tubeTypes() == std::vector<int8_t>{tubes::kFlashium},
          "the Bonus is not gated");
}

void testTheTubeIsAStack() {
    // 1000:4715 tips slot[count], the one caught last. The port used to tip
    // slot 1 and emptied the tube oldest-first.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    g.setTubeColumn(3);
    g.setTubeAtoms({tubes::kRedium, tubes::kGreenium});
    check(g.heldAtom() == tubes::kGreenium, "the mouth is the last one caught");
    tipOnce(g);
    check(g.tubeTypes() == std::vector<int8_t>{tubes::kRedium},
          "and that is the one an A press tips out");
    // It is now a record 7..12 falling into the beaker rather than a cell
    // written on the spot - 9 px a frame from the tube down to row 5 at y=186.
    check(g.atom(7).state == tubes::atomstate::kTipped,
          "the tipped atom is in the spare-record pool");
    for (int i = 0; i < 20; ++i) g.update(0, 1.0f / tubes::kFrameHz);
    check(g.board().typeAt(3, 4) == tubes::kGreenium, "it lands in the beaker");
}

// --- the tipping animation, 1000:463a ---------------------------------

void testTipRunsFourPhasesOverSixFrames() {
    // A 2-frame divider drives the phases, so the whole tip is six frames and
    // phase 4 never survives to a draw - it resets to 1 in the same body that
    // releases the atom, which is why three sprites cover four phases.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    g.setTubeColumn(3);
    g.setTubeAtoms({tubes::kRedium, tubes::kGreenium});

    // Sampled AFTER each step, which is where the original's draw sits - the
    // animation runs in the input section and the frame is drawn below it.
    const uint8_t want[6] = {1, 2, 2, 3, 3, 1};
    bool phasesOk = true;
    for (int f = 0; f < 6; ++f) {
        g.update(tubes::button::kA, 1.0f / tubes::kFrameHz);
        if (g.tubePhase() != want[f]) phasesOk = false;
    }
    check(phasesOk, "the tip renders phases 1,2,2,3,3 and never 4");
    check(g.tubeTypes() == std::vector<int8_t>{tubes::kRedium},
          "and the mouth has left the tube by the sixth frame");
}

void testTipMovesTheContents() {
    // Phase 2 bunches them to 99/93/87/81/73 and steps every slot one pixel
    // left; phase 3 lines them all up at 82. Both are per-slot literals.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    g.setTubeColumn(3);
    g.setTubeAtoms({tubes::kRedium, tubes::kGreenium, tubes::kBluium});
    const int restX = g.tubeAtoms()[0].x;
    check(g.tubeAtoms()[0].y == 120 && g.tubeAtoms()[2].y == 94,
          "at rest the slots sit at the tube's y plus 52, 39, 26");

    for (int f = 0; f < 3; ++f) g.update(tubes::button::kA, 1.0f / tubes::kFrameHz);
    check(g.tubeAtoms()[0].y == 99 && g.tubeAtoms()[1].y == 93 &&
          g.tubeAtoms()[2].y == 87, "phase 2 bunches them toward the middle");
    check(g.tubeAtoms()[0].x == restX - 1, "and steps every slot 1 px left");

    for (int f = 0; f < 2; ++f) g.update(tubes::button::kA, 1.0f / tubes::kFrameHz);
    check(g.tubeAtoms()[0].y == 82 && g.tubeAtoms()[2].y == 82,
          "phase 3 lines all five up as the tube pours");
}

void testCaughtAtomSlidesToItsSlot() {
    // A catch lands at the MOUTH and descends 9 px a frame; the special does
    // not fire until it arrives. The port used to teleport it into place.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    g.setTubeColumn(3);
    tubes::Falling a;
    a.state = tubes::atomstate::kDescend;
    a.column = 6;
    a.colour = tubes::kRedium;
    a.x = tubes::kAtomColumnX[6];
    a.y = 55;
    a.velocity = 0x100;
    g.setAtom(6, a);

    g.update(0, 1.0f / tubes::kFrameHz);
    check(g.tubeAtoms().size() == 1, "the atom is caught");
    // 55 + 9 = 64, inside the 60..70 window, and the slide then takes it on to
    // 73 on the very same frame - the tube's slot loop runs after the network's.
    check(g.tubeAtoms()[0].y == 73 && !g.tubeAtoms()[0].arrived,
          "and starts sliding rather than appearing in its slot");
    for (int f = 0; f < 6; ++f) g.update(0, 1.0f / tubes::kFrameHz);
    check(g.tubeAtoms()[0].y == 120 && g.tubeAtoms()[0].arrived,
          "it settles on slot 1 at the tube's y plus 52");
}

void testTippedAtomFallsIntoAFullColumnAndIsLost() {
    // 1000:16d3: a column with no free row destroys the atom and spends a
    // drop. It does not sit on top and it does not bounce.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    g.setTubeColumn(3);
    for (int r = 0; r < 5; ++r) g.boardMutable().set(3, r, tubes::kXenon);
    g.setTubeAtoms({tubes::kRedium});
    tipOnce(g);
    for (int f = 0; f < 30; ++f) g.update(0, 1.0f / tubes::kFrameHz);
    check(g.board().typeAt(3, 0) == tubes::kXenon, "the column is untouched");
    check(g.dropsRemaining() == 8, "and the tip cost a drop");
    check(!g.atom(7).active(), "the record is released either way");
}

// --- text, 2000:35ec and 2000:36ab -------------------------------------

// A 4-row font with one glyph: 'A' is a solid 8-pixel bar on every row, and
// ' ' is blank. Enough to see the colour walk and the shadow.
tubes::Font barFont() {
    tubes::Font f;
    f.glyphs.assign(256 * 4, 0);
    for (int r = 0; r < 4; ++r) f.glyphs['A' * 4 + r] = 0xff;
    f.cellH = 4;
    f.advance = 8;
    f.peakRow = 3;
    return f;
}

uint8_t pixelAt(const tubes::Screen& s, int x, int y) {
    return s.pixels()[static_cast<size_t>(y) * tubes::kScreenWidth + x];
}

void testTextColourWalksDownTheCell() {
    // `2000:35ec` adjusts the colour after every scanline, so a glyph is a
    // vertical gradient off one index. Mode 1 counts DOWN, which is what the
    // HUD uses - the palette holds a cyan ramp at 112..127 and 127 is its
    // darkest end, so the digits brighten toward the bottom.
    tubes::Screen s;
    const tubes::Font f = barFont();
    tubes::drawText(s, f, 10, 5, 127, tubes::textmode::kFadeDown, "A");
    check(pixelAt(s, 10, 5) == 127 && pixelAt(s, 10, 6) == 126 &&
          pixelAt(s, 10, 7) == 125 && pixelAt(s, 10, 8) == 124,
          "mode 1 steps the colour down one index a scanline");

    tubes::Screen up;
    tubes::drawText(up, f, 10, 5, 100, tubes::textmode::kFadeUp, "A");
    check(pixelAt(up, 10, 5) == 100 && pixelAt(up, 10, 8) == 103,
          "mode 2 steps it up");

    tubes::Screen flat;
    tubes::drawText(flat, f, 10, 5, 60, tubes::textmode::kFlat, "A");
    check(pixelAt(flat, 10, 5) == 60 && pixelAt(flat, 10, 8) == 60,
          "mode 0 does not move at all");
}

void testTextPeakMode() {
    // Mode 3 compares the DOWN-counting row index against the turning point,
    // so the peak is measured from the bottom of the cell. With cellH 4 and a
    // turning point of 3, rows 4 counts as above it and 3, 2, 1 below.
    tubes::Screen s;
    const tubes::Font f = barFont();
    tubes::drawText(s, f, 10, 5, 100, tubes::textmode::kPeak, "A");
    check(pixelAt(s, 10, 5) == 100 && pixelAt(s, 10, 6) == 98 &&
          pixelAt(s, 10, 7) == 100 && pixelAt(s, 10, 8) == 102,
          "mode 3 dips then climbs, two indices at a time");
}

void testTextShadowAndSpaces() {
    tubes::Screen s;
    const tubes::Font f = barFont();
    tubes::drawText(s, f, 10, 5, 127,
                    tubes::textmode::kShadow | tubes::textmode::kFlat, "A");
    check(pixelAt(s, 18, 9) == tubes::kShadowColour + 0 &&
          pixelAt(s, 18, 9) == 0, "the shadow lands one down and one right");
    check(pixelAt(s, 10, 5) == 127, "and the glyph is drawn over it");

    // 2000:36e8 skips a space entirely, so a padded number lays down no
    // shadow in its blank columns.
    tubes::Screen sp;
    tubes::drawText(sp, f, 10, 5, 127,
                    tubes::textmode::kShadow | tubes::textmode::kFlat, "  A");
    check(pixelAt(sp, 10, 5) == 0 && pixelAt(sp, 11, 6) == 0,
          "a space draws nothing, not even a shadow");
    check(pixelAt(sp, 26, 5) == 127, "and still advances the cursor");
}

void testTextCentring() {
    // 2321:05e8 adds the two bounds and halves, so (0, 319) centres on 159.
    tubes::Screen s;
    const tubes::Font f = barFont();
    check(tubes::textWidth(f, "AAA") == 24, "width is one advance a character");
    tubes::drawTextCentred(s, f, 0, 319, 0, 127, tubes::textmode::kFlat, "AAA");
    check(pixelAt(s, (319 - 24) / 2, 0) == 127,
          "a centred string starts at (x0 + x1 - width) / 2");
}

// --- the descent, 1000:13ea -------------------------------------------

// Drops one atom down column 6 from `startY` and returns the game, with the
// tube parked wherever `tubeCol` says. Column 6 lands at x = 161.
void descendColumn6(tubes::Game& g, int startY, int8_t type) {
    tubes::Falling a;
    a.state = tubes::atomstate::kDescend;
    a.column = 6;
    a.colour = type;
    a.x = tubes::kAtomColumnX[6];
    a.y = startY;
    a.velocity = 0x100;
    g.setAtom(6, a);
}

void testDescentAcceleratesBelowFifty() {
    // `1000:13ed` picks the increment by height: the difficulty's 2 px a frame
    // above y = 50, a flat 9 below it. A missed ball drops away much faster
    // than it travelled the network, which the port used not to do.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    g.setTubeColumn(0);                       // out of column 6's way
    descendColumn6(g, 30, tubes::kRedium);
    g.update(0, 1.0f / tubes::kFrameHz);
    check(g.atom(6).y == 32, "above 50 it falls at the difficulty's 2 px");

    tubes::Game h(6, 5, tubes::Difficulty::k101, 5);
    h.setTubeColumn(0);
    descendColumn6(h, 80, tubes::kRedium);
    h.update(0, 1.0f / tubes::kFrameHz);
    check(h.atom(6).y == 89, "at or below 50 it falls a flat 9");
}

void testCatchIsAWindow() {
    // 60..70, `1000:1423`. Below it is too early and past it is too late; the
    // port used to catch anything at or past the mouth, which meant an atom
    // could be scooped up most of the way to the floor.
    tubes::Game late(6, 5, tubes::Difficulty::k101, 5);
    late.setTubeColumn(3);                    // stop 3 is column 6's x, 161
    descendColumn6(late, 75, tubes::kRedium);
    for (int f = 0; f < 3; ++f) late.update(0, 1.0f / tubes::kFrameHz);
    check(late.tubeAtoms().empty(), "an atom already past 70 is not caught");

    tubes::Game hit(6, 5, tubes::Difficulty::k101, 5);
    hit.setTubeColumn(3);
    descendColumn6(hit, 55, tubes::kRedium);
    hit.update(0, 1.0f / tubes::kFrameHz);
    check(hit.tubeAtoms().size() == 1, "one landing inside the window is");
}

void testTippingTubeCannotCatch() {
    // `1000:1463` - the tube will not catch while it is tipping.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    g.setTubeColumn(3);
    g.setTubeAtoms({tubes::kRedium});
    descendColumn6(g, 55, tubes::kGreenium);
    g.update(tubes::button::kA, 1.0f / tubes::kFrameHz);      // starts the tip
    check(g.tubeAtoms().size() == 1, "nothing is caught mid-tip");
    check(g.atom(6).y == 64, "and the atom carries on falling");
}

void testMissedBonusCostsNothing() {
    // `1000:153e`. The same exemption state 9 makes for a full column - the
    // port had it in one place and not the other.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    g.setTubeColumn(0);
    descendColumn6(g, 180, tubes::kBonus);
    for (int f = 0; f < 3; ++f) g.update(0, 1.0f / tubes::kFrameHz);
    check(!g.atom(6).drawn(), "the Bonus fell past and was lost");
    check(g.dropsRemaining() == 9, "and it cost nothing");

    tubes::Game h(6, 5, tubes::Difficulty::k101, 5);
    h.setTubeColumn(0);
    descendColumn6(h, 180, tubes::kRedium);
    for (int f = 0; f < 3; ++f) h.update(0, 1.0f / tubes::kFrameHz);
    check(h.dropsRemaining() == 8, "an ordinary atom does cost one");
}

void testFlashiumCyclesEveryFourFrames() {
    // Type 8 has NO SPRITE. `1000:486b` rewrites its slot in the ball table
    // from one of the seven colours instead, advancing every fourth frame -
    // the counter starts at 1 and fires when it reaches 5. Drawn as itself a
    // Flashium is invisible, which is what the phantom ball in the test tube
    // was: a Multiplier fills with Random(8) + 1 and 8 is in that range.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    std::string seen;
    for (int f = 0; f < 32; ++f) {
        seen += static_cast<char>('0' + g.flashColour());
        g.update(0, 1.0f / tubes::kFrameHz);
    }
    check(seen == "11112222333344445555666677771111",
          "Flashium holds each of the seven colours for four frames, then wraps");
}

// --- sound, the .SFX header and the single voice ------------------------

// Builds a .SFX the way the files are laid out, so the header offsets are
// exercised rather than assumed.
tubes::Bytes makeSfx(const std::string& name, int rate,
                     const std::vector<uint8_t>& pcm, uint8_t flag = 0) {
    tubes::Bytes b(0x27, 0);
    b[0] = 0xf1;
    b[1] = static_cast<uint8_t>(name.size());
    for (size_t i = 0; i < name.size(); ++i) b[2 + i] = name[i];
    b[0x20] = static_cast<uint8_t>(rate & 0xff);
    b[0x21] = static_cast<uint8_t>(rate >> 8);
    b[0x22] = flag;
    b[0x25] = static_cast<uint8_t>(pcm.size() & 0xff);
    b[0x26] = static_cast<uint8_t>(pcm.size() >> 8);
    b.insert(b.end(), pcm.begin(), pcm.end());
    return b;
}

void testSfxHeader() {
    tubes::Sound s;
    std::string err;
    check(tubes::decodeSfx(makeSfx("Smack!", 8000, {1, 2, 3}), s, err),
          "a well-formed .SFX decodes");
    check(s.name == "Smack!" && s.rate == 8000 &&
          s.pcm == std::vector<uint8_t>({1, 2, 3}),
          "name, rate and PCM all come off the right offsets");

    // SBSOUND.DRV bails on a bad marker before it reads anything else.
    tubes::Bytes bad = makeSfx("x", 8000, {1});
    bad[0] = 0;
    check(!tubes::decodeSfx(bad, s, err), "a missing 0xf1 marker is rejected");

    // The rate is a WORD at 0x20 - the driver loads CX from there and divides
    // 1000000 by it. A longword reading cannot be told apart in the shipped
    // files, which are all 8000 Hz, but 0x22 is the flag and not the rate's
    // third byte: set it and the driver takes a path this port cannot follow.
    check(!tubes::decodeSfx(makeSfx("x", 8000, {1}, 1), s, err),
          "the flag at 0x22 is refused rather than guessed at");
}

void testSfxVoiceResamples() {
    tubes::Sound s;
    std::string err;
    tubes::decodeSfx(makeSfx("t", 8000, {0x80, 0xc0, 0x40, 0x80}), s, err);

    // At a matching device rate the step is exactly one, so the samples come
    // out untouched - centred on 0x80 and scaled.
    tubes::SfxVoice v;
    v.play(&s, 8000);
    std::vector<int16_t> buf(4 * 2, 0);
    v.mix(buf.data(), 4);
    check(buf[0] == 0 && buf[2] == 0x40 * 96 && buf[4] == -0x40 * 96,
          "at a matched rate the PCM plays through unchanged");
    check(buf[0] == buf[1] && buf[2] == buf[3], "and to both channels");

    // Twice the device rate holds each source sample for two output frames.
    tubes::SfxVoice up;
    up.play(&s, 16000);
    std::vector<int16_t> wide(8 * 2, 0);
    up.mix(wide.data(), 8);
    check(wide[2] == 0 && wide[4] == 0x40 * 96 && wide[6] == 0x40 * 96,
          "at double the rate each sample lasts two frames");
}

void testSfxVoiceIsSingle() {
    // `SBSOUND.DRV`'s play entry calls its own stop routine first, so a second
    // sound cuts off the first rather than mixing with it.
    tubes::Sound a, b;
    std::string err;
    tubes::decodeSfx(makeSfx("a", 8000, {0xff, 0xff, 0xff, 0xff}), a, err);
    tubes::decodeSfx(makeSfx("b", 8000, {0x80, 0x80}), b, err);

    tubes::SfxVoice v;
    v.play(&a, 8000);
    std::vector<int16_t> buf(2 * 2, 0);
    v.mix(buf.data(), 2);
    check(buf[0] == 0x7f * 96, "the first sound is playing");
    v.play(&b, 8000);
    std::vector<int16_t> two(2 * 2, 0);
    v.mix(two.data(), 2);
    check(two[0] == 0, "starting another replaces it outright");

    // And it releases the voice at the end rather than looping or hanging.
    v.mix(two.data(), 2);
    check(!v.busy(), "a finished sound frees the voice");
}

// --- the RNG and the recording ----------------------------------------

void testTurboPascalRandom() {
    // `2000:75bb` is `RandSeed := RandSeed * $08088405 + 1`, and `2000:755e`
    // takes the top 32 bits of the 48-bit product `RandSeed * n`. That is a
    // scaled fraction of the range, NOT a modulus, so it is a different
    // sequence from the same seed - which is exactly why the port's old
    // xorshift could never have replayed a demo.
    //
    // The expected values are computed from the algorithm above, independently
    // of the implementation under test.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 1);
    // The constructor spends one roll on the tube's starting column
    // (`1000:43d6`), so the sequence here starts at the second value.
    check(g.tubeColumn() == 0, "seed 1 puts the tube in column 0");

    uint32_t s = 1;
    auto expect = [&s](int n) {
        s = s * 0x08088405u + 1u;
        return static_cast<int>((static_cast<uint64_t>(s) * n) >> 32);
    };
    check(expect(6) == 0, "the reference generator agrees with the binary");
    bool ok = true;
    for (int i = 0; i < 64; ++i) {
        if (g.rollForTest(6) != expect(6)) ok = false;
    }
    check(ok, "and the port matches it for 64 consecutive rolls");
}

void testScrHeader() {
    // u16 count covering everything after itself, then the u32 seed that goes
    // straight into RandSeed, then one input byte per frame.
    tubes::Bytes raw = {0x08, 0x00,                    // count = 8
                        0x5e, 0x38, 0x2d, 0x32,        // seed
                        0x02, 0x06, 0x00, 0x10};       // four frames
    tubes::Demo d;
    std::string err;
    check(tubes::decodeScr(raw, d, err), "a .SCR decodes");
    check(d.seed == 0x322d385eu, "the seed is little-endian at offset 2");
    check(d.input == std::vector<uint8_t>({0x02, 0x06, 0x00, 0x10}),
          "and the input runs from offset 6 for `count - 4` bytes");

    check(!tubes::decodeScr(tubes::Bytes{0x00, 0x00}, d, err),
          "a header with no room for a seed is rejected");
}

void testFirstDispenseIsImmediate() {
    // `1000:3be0` seeds the dispenser countdown with 1, not with the interval,
    // as the last act of the session setup. The tick is `Dec; if = 0 then
    // dispense`, so the first atom appears on frame ZERO.
    //
    // Seeding it with the interval instead delayed the first dispense by a
    // whole period, which slid a replayed recording out of step with the game
    // for the rest of the session.
    tubes::Game g(6, 5, tubes::Difficulty::k301, 0x322d385eu);
    bool any = false;
    for (int c = 1; c <= tubes::kAtomSlots; ++c) any |= g.atom(c).active();
    check(!any, "nothing is in flight before the first step");
    g.stepOnce(0);
    for (int c = 1; c <= tubes::kAtomSlots; ++c) any |= g.atom(c).active();
    check(any, "the first atom is dispensed on frame 0, not after a period");
}

void testInputIsReadOnlyWhileTheTubeIsIdle() {
    // `1000:44f0` jumps past the whole input block when the tube's state is not
    // 0, so the input driver is never CALLED on a frame where the tube is
    // sliding or tipping. In demo playback that driver IS the recording, so a
    // `.SCR` holds one byte per IDLE frame - which is what `acceptsInput`
    // exists to let the replay honour.
    tubes::Game g(6, 5, tubes::Difficulty::k301, 0x322d385eu);
    check(g.acceptsInput(), "a parked tube accepts input");

    const int before = g.tubeColumn();
    check(before > 0, "the seeded tube has room to move left");
    g.stepOnce(tubes::button::kLeft);
    check(g.tubeColumn() == before - 1, "Left moves the stop immediately");
    check(!g.acceptsInput(), "and the tube stops accepting while it slides");

    // The slide is 6 px a frame over an 18 px pitch (`1000:45f3`), so it is
    // busy for two more frames and a Left arriving in them is not seen.
    g.stepOnce(tubes::button::kLeft);
    check(!g.acceptsInput(), "still sliding one frame later");
    check(g.tubeColumn() == before - 1, "and the second Left is ignored");
    g.stepOnce(tubes::button::kLeft);
    check(g.acceptsInput(), "the tube is idle again after three frames");
}

void testEnduranceRampStepsOnMatches() {
    // `1000:235c`. The game speeds up as you CLEAR - not with time, and not
    // with atoms dispensed. Every fifth match drops the dispense interval by
    // five frames; every tenth adds 0x20 to the network velocity and hands the
    // five frames back, so the net shape is -5 frames per ten matches.
    //
    // This is what made the demo replay fall apart. The original's spawns land
    // on 50-frame centres for 29 atoms and then switch to 45, on the frame its
    // fifth match landed; the port dispensed at 50 forever, so its atoms
    // arrived progressively late against a tube that was in the right place.
    tubes::Game g(6, 5, tubes::Difficulty::k301, 0x322d385eu);
    check(g.spawnIntervalForTest() == 50, "Tubes 301 seeds a 50-frame interval");
    const int vel0 = g.networkVelForTest();

    for (int i = 0; i < 4; ++i) g.stepRampForTest(1);
    check(g.spawnIntervalForTest() == 50, "four matches do not move it");
    g.stepRampForTest(1);
    check(g.spawnIntervalForTest() == 45, "the fifth match drops it to 45");
    check(g.networkVelForTest() == vel0, "and leaves the velocity alone");

    // A frame with no runs must not re-fire the crossing, and must not clear
    // the latch either - the latch is cleared by the counter MOVING OFF a
    // multiple of five, which a runless frame does not do.
    g.stepRampForTest(0);
    check(g.spawnIntervalForTest() == 45, "a runless frame does not step it");

    for (int i = 0; i < 4; ++i) g.stepRampForTest(1);
    check(g.spawnIntervalForTest() == 45, "still 45 at nine matches");
    g.stepRampForTest(1);
    // The tenth hits both arms: -5 then +5, and the velocity climbs.
    check(g.spawnIntervalForTest() == 45, "the tenth cancels back to 45");
    check(g.networkVelForTest() == vel0 + 0x20, "and adds 0x20 to the velocity");

    for (int i = 0; i < 5; ++i) g.stepRampForTest(1);
    check(g.spawnIntervalForTest() == 40, "the fifteenth takes it to 40");
}

void testEnduranceRampCountsPerRunNotPerFrame() {
    // The ramp body is a LOOP over the run count: `1000:240a` decrements it and
    // `1000:240d` jumps back to the `runs >= 1` test, falling through to the
    // score multiplier only at zero. So a frame that forms five runs steps the
    // counter five times, not once.
    //
    // That is easy to get wrong - the first transliteration hung the body off
    // `if runs >= 1` - and the cost is invisible for thousands of frames. Runs
    // are counted once per SEED, so a line of four pays twice and simultaneous
    // runs in different orientations each count, which means one good clear can
    // walk the counter through a crossing on its own.
    tubes::Game one(6, 5, tubes::Difficulty::k301, 0x322d385eu);
    one.stepRampForTest(5);
    check(one.spawnIntervalForTest() == 45,
          "five runs in ONE frame cross the mod-5 boundary");

    tubes::Game five(6, 5, tubes::Difficulty::k301, 0x322d385eu);
    for (int i = 0; i < 5; ++i) five.stepRampForTest(1);
    check(five.spawnIntervalForTest() == one.spawnIntervalForTest(),
          "and land where five single-run frames do");

    // Ten runs at once must hit BOTH arms, cancelling on the interval and
    // leaving the velocity raised - a counter that only advanced once would
    // reach neither.
    tubes::Game ten(6, 5, tubes::Difficulty::k301, 0x322d385eu);
    const int vel0 = ten.networkVelForTest();
    ten.stepRampForTest(10);
    check(ten.spawnIntervalForTest() == 45, "ten at once cancel back to 45");
    check(ten.networkVelForTest() == vel0 + 0x20,
          "and still raise the velocity once");
}

void testTipSkipsFiveFramesOfInput() {
    // How long the tube is busy is how many bytes of a recording get skipped,
    // so the tip's length is load-bearing for the replay and not just for the
    // animation. `1000:463a`: a 2-frame divider steps a phase that starts at 1,
    // and `1000:4701` hands the state back on the frame the phase reaches 4.
    //
    // That is six frames in state 3 - but only FIVE frames of skipped input.
    // The press frame still reads a byte, because the input block runs before
    // the state machine that puts the tube into state 3. Counting the press
    // frame as skipped is an easy off-by-one and it is the wrong number.
    tubes::Game g(6, 5, tubes::Difficulty::k301, 0x322d385eu);
    g.setTubeAtoms({1, 2});
    check(g.acceptsInput(), "the tube reads the byte that presses A");
    g.stepOnce(tubes::button::kA);

    int skipped = 0;
    for (int i = 0; i < 20 && !g.acceptsInput(); ++i) {
        ++skipped;
        g.stepOnce(0);
    }
    check(skipped == 5, "a tip swallows exactly five frames of input");
}


// ---------------------------------------------------------------------------
// Wave mode: the table, the seeds and the objective hook.
//
// The oracle here is the old level-warp sweep, which read nine briefings out
// of the running game before any of this was decompiled. It warped ONE save,
// so every sample saw that save's counters - and since wave 6 said 30 atoms,
// wave 20 said 3 marked and wave 50 said 1 crystal, those counters were
// sitting at exactly the new-game seeds. That is what makes the comparison
// below legitimate rather than a coincidence: the sweep and `1000:a4cd` are
// independent readings of the same six numbers.
// ---------------------------------------------------------------------------

// A roller that hands out a fixed sequence, so a briefing's randomisation can
// be pinned. Anything past the end repeats the last value.
struct FixedRolls {
    std::vector<int> values;
    mutable size_t at = 0;
    int operator()(int n) const {
        int v = values.empty() ? 0 : values[at < values.size() ? at : values.size() - 1];
        ++at;
        return n > 0 ? v % n : 0;
    }
};

void testWaveTableReproducesTheSampledBriefings() {
    using namespace tubes;
    struct Sample { int wave; Objective o; };
    const Sample samples[] = {
        {6,  Objective::kSurviveDisabled},   // live through 30, Yellowium disabled
        {10, Objective::kVerticalColour},    // 2 vertical chains using Cyanium
        {11, Objective::kAnyAtomMorph},      // 2 chains, beaker morphs every 45 s
        {15, Objective::kVerticalColour},    // the same objective as wave 10
        {20, Objective::kMarked},            // Marked Atoms: 3
        {25, Objective::kTaskColour},        // colour from the Task Display
        {30, Objective::kSurviveHidden},     // 30 atoms, hidden until they leave
        {40, Objective::kHorizontalColour},  // 2 horizontal chains using Purplium
        {50, Objective::kCrystals},          // remove the Mischief Crystals
    };
    for (const Sample& s : samples) {
        check(objectiveForWave(s.wave) == s.o,
              "wave " + std::to_string(s.wave) + " picks the sampled objective");
    }
    // Waves 10 and 15 really do share one, which the sweep could only call a
    // coincidence.
    check(objectiveForWave(10) == objectiveForWave(15),
          "waves 10 and 15 share an objective, as sampled");
}

void testSeedsProduceTheSampledCounts() {
    using namespace tubes;
    FixedRolls roll{{0}};
    auto brief = [&](int wave) {
        WaveProgress p;            // the new-game seeds: 3, 30, 2, 0, 3, 8
        p.wave = wave;
        WaveObjective obj;
        applyBriefing(objectiveForWave(wave), p, obj, std::ref(roll), false);
        return std::make_pair(obj, p);
    };

    check(brief(6).first.counter == 30, "wave 6 asks for 30 atoms");
    check(brief(6).first.mode == WaveMode::kSurvive, "wave 6 is mode 4");
    check(brief(6).first.disabledColour != 0, "wave 6 disables an element");

    check(brief(10).first.counter == 2, "wave 10 asks for 2 chains");
    check(brief(10).first.reqChain == chaincode::kVertical, "wave 10 wants vertical");
    check(brief(11).first.counter == 2, "wave 11 asks for 2 chains");
    check(brief(11).first.morphBeaker, "wave 11 morphs the beaker");

    check(brief(20).first.counter == 3, "wave 20 asks for 3 marked atoms");
    check(brief(20).first.mode == WaveMode::kMarked, "wave 20 is mode 6");

    check(brief(30).first.counter == 30, "wave 30 asks for 30 atoms");
    check(brief(30).first.hiddenAtoms, "wave 30 hides atoms in the tubes");

    check(brief(40).first.counter == 2, "wave 40 asks for 2 chains");
    check(brief(40).first.reqChain == chaincode::kHorizontal, "wave 40 wants horizontal");

    // Seeded at 0 and incremented by the briefing itself, so the first crystal
    // wave asks for one.
    auto w50 = brief(50);
    check(w50.first.counter == 1, "wave 50 asks for 1 Mischief Crystal");
    check(w50.second.crystals == 1, "the crystal count is stepped by the briefing");

    // The orientation-only waves read the OTHER chain target, seeded at 3.
    check(brief(13).first.counter == 3, "wave 13 asks for 3 horizontal chains");
    check(brief(13).first.mode == WaveMode::kOrientation, "wave 13 is mode 2");
}

void testWaveProgressionStepsOnFifteensAndTwenties() {
    using namespace tubes;
    WaveProgress p;
    p.interval = 70;
    p.velocity = 0x100;

    p.wave = 1; p.advance();
    check(p.interval == 69 && p.wave == 2, "a cleared wave costs one frame of interval");
    check(p.velocity == 0x100, "and leaves the velocity alone");

    p = WaveProgress(); p.interval = 70; p.velocity = 0x100; p.wave = 15;
    p.advance();
    check(p.interval == 70 - 1 + 12, "every fifteenth wave refunds twelve frames");
    check(p.velocity == 0x120, "and adds 0x20 to the velocity");
    check(p.atomTarget == 30, "but does not touch the objective counters");

    p = WaveProgress(); p.wave = 20;
    p.advance();
    check(p.atomTarget == 40, "every twentieth wave adds ten atoms");
    check(p.chainTargetColour == 3 && p.chainTargetChain == 4, "and a chain to both targets");
    check(p.marked == 4, "and one more marked atom");
    check(p.crystals == 0, "the crystal count is NOT stepped here");
}

void testCreditRunHonoursColourAndOrientation() {
    using namespace tubes;
    WaveObjective obj;
    obj.mode = WaveMode::kColour;
    obj.counter = 2;
    obj.reqColour = kCyanium;
    obj.reqChain = chaincode::kVertical;
    TaskDisplay task = seedTaskDisplay(obj);

    check(!creditRun(obj, RunKind::kHorizontal, kCyanium, task),
          "the right colour in the wrong chain does not count");
    check(!creditRun(obj, RunKind::kVertical, kRedium, task),
          "the wrong colour in the right chain does not count");
    check(creditRun(obj, RunKind::kVertical, kCyanium, task) && obj.counter == 1,
          "both right counts once");
    // The third arm of `1000:192f`'s colour test, and the one no amount of
    // watching would have produced.
    check(creditRun(obj, RunKind::kVertical, kFlashium, task) && obj.counter == 0,
          "an all-Flashium run satisfies any colour");
    check(!creditRun(obj, RunKind::kVertical, kCyanium, task),
          "a finished objective does not go negative");

    // Mode 2 ignores the colour entirely.
    WaveObjective any;
    any.mode = WaveMode::kOrientation;
    any.counter = 1;
    any.reqChain = chaincode::kDiagonal;
    any.reqColour = kGreenium;
    check(creditRun(any, RunKind::kDiagonal, kPinkium, task),
          "mode 2 counts any colour in the right chain");
}

void testTaskRotationWrapsAndPicksItsClock() {
    using namespace tubes;
    WaveObjective obj;
    obj.mode = WaveMode::kColour;
    obj.counter = 9;
    obj.anyOrientation = true;
    obj.reqColour = kPinkium;
    obj.rotateColour = true;
    TaskDisplay task = seedTaskDisplay(obj);

    creditRun(obj, RunKind::kVertical, kPinkium, task);
    check(obj.reqColour == kRedium, "the colour wraps 7 -> 1 after a task");
    check(task.colour == kRedium, "and the Task Display follows it");

    // The same wave on the 45-second clock rotates on the timer INSTEAD, not
    // as well - that is the only difference between the two template pairs.
    obj.rotateOnTimer = true;
    obj.reqColour = kRedium;
    creditRun(obj, RunKind::kVertical, kRedium, task);
    check(obj.reqColour == kRedium, "a timed wave does not rotate on a task");
    check(taskTimerExpired(obj, task) && obj.reqColour == kGreenium,
          "it rotates when the 720-frame timer expires");

    WaveObjective chain;
    chain.mode = WaveMode::kColour;
    chain.counter = 9;
    chain.anyOrientation = true;
    chain.rotateChain = true;
    chain.reqChain = chaincode::kVertical;
    TaskDisplay t2 = seedTaskDisplay(chain);
    creditRun(chain, RunKind::kVertical, kRedium, t2);
    check(chain.reqChain == chaincode::kDiagonal, "the chain wraps 2 -> 0");
}

void testContinueReplaysTheSameObjective() {
    using namespace tubes;
    FixedRolls roll{{3, 5, 1, 6, 2, 4}};
    WaveProgress p;
    p.wave = 10;                       // 2 vertical chains of a rolled colour
    WaveObjective obj;
    applyBriefing(objectiveForWave(10), p, obj, std::ref(roll), false);
    const int8_t first = obj.reqColour;

    obj.counter = 0;                   // the player got part way and died
    applyBriefing(objectiveForWave(10), p, obj, std::ref(roll), true);
    check(obj.reqColour == first, "a Continue keeps the wave's colour");
    check(obj.counter == 2, "and resets the counter");

    // A crystal wave must not charge the count twice for one wave either.
    WaveProgress q;
    q.wave = 50;
    WaveObjective c;
    applyBriefing(Objective::kCrystals, q, c, std::ref(roll), false);
    applyBriefing(Objective::kCrystals, q, c, std::ref(roll), true);
    check(q.crystals == 1, "a replayed crystal wave does not add another");
}

void testMysteryWaveHidesOneOfFour() {
    using namespace tubes;
    const tubes::Objective expect[4] = {
        Objective::kShownAtom, Objective::kVerticalAny,
        Objective::kHorizontalAny, Objective::kDiagonalAny};
    for (int i = 0; i < 4; ++i) {
        FixedRolls roll{{i, 0}};
        WaveProgress p;
        p.wave = 46;
        WaveObjective obj;
        applyBriefing(Objective::kMystery, p, obj, std::ref(roll), false);

        WaveProgress q;
        q.wave = 46;
        WaveObjective plain;
        FixedRolls roll2{{0}};
        applyBriefing(expect[i], q, plain, std::ref(roll2), false);

        check(obj.mode == plain.mode && obj.reqChain == plain.reqChain,
              "Mystery Wave runs one of the four outright");
        check(obj.mysteryHidden, "and blanks the Task Display until the first task");
    }
    // The reveal is the first credited run, not the end of the wave.
    FixedRolls roll{{1, 0}};
    WaveProgress p;
    WaveObjective obj;
    applyBriefing(Objective::kMystery, p, obj, std::ref(roll), false);
    TaskDisplay task = seedTaskDisplay(obj);
    creditRun(obj, RunKind::kVertical, kRedium, task);
    check(!obj.mysteryHidden, "the first task reveals a Mystery Wave");
}

void testEveryWaveHasAnArm() {
    using namespace tubes;
    FixedRolls roll{{0}};
    for (int w = 1; w <= kWaveCount; ++w) {
        WaveProgress p;
        p.wave = w;
        WaveObjective obj;
        applyBriefing(objectiveForWave(w), p, obj, std::ref(roll), false);
        check(isWaveMode(obj.mode) && obj.counter > 0,
              "wave " + std::to_string(w) + " sets a mode and a counter");
    }
}


void testAWaveCountsDownThroughTheGame() {
    using namespace tubes;
    // Wave 5 is "form 3 vertical chains using any atoms" - mode 2, the
    // orientation-only arm, whose target is the OTHER chain counter, seeded 3.
    Game g(6, 5, Difficulty::k101, 12345u);
    g.startWave();                     // progress starts on wave 1
    check(g.waveMode() == WaveMode::kColour, "wave 1 is a colour-mode wave");

    while (g.progress().wave < 5) g.advanceWave();
    g.startWave();
    check(g.waveMode() == WaveMode::kOrientation, "wave 5 is mode 2");
    check(g.objective().counter == 3, "and asks for three chains");
    check(g.objective().reqChain == chaincode::kVertical, "vertical ones");

    // A vertical three in the bottom-left corner, stepped until it clears.
    Board& b = g.boardMutable();
    b.set(0, 2, kRedium);
    b.set(0, 3, kRedium);
    b.set(0, 4, kRedium);
    g.stepOnce(0);
    check(g.objective().counter == 2, "a vertical three ticks the objective down");
    check(!g.waveComplete(), "and does not finish it");

    // A horizontal three must not count in a vertical wave.
    b.clear();
    b.set(1, 4, kGreenium);
    b.set(2, 4, kGreenium);
    b.set(3, 4, kGreenium);
    g.stepOnce(0);
    check(g.objective().counter == 2, "a horizontal three counts for nothing");
}

void testRunOfFourTicksTheObjectiveTwice() {
    using namespace tubes;
    // `1000:192f` is called from the same unconditional path as the award, so
    // it fires once per SEED. Two of this project's worst bugs were "once per
    // event" assumptions, so this is the check that says which it is.
    Game g(6, 5, Difficulty::k101, 999u);
    while (g.progress().wave < 5) g.advanceWave();
    g.startWave();
    check(g.objective().counter == 3, "wave 5 asks for three");

    Board& b = g.boardMutable();
    b.set(0, 1, kBluium);
    b.set(0, 2, kBluium);
    b.set(0, 3, kBluium);
    b.set(0, 4, kBluium);
    g.stepOnce(0);
    check(g.objective().counter == 1, "a vertical FOUR has two seeds and pays twice");
}

void testSurviveWaveCountsAtomsDispensed() {
    using namespace tubes;
    Game g(6, 5, Difficulty::k101, 7u);
    while (g.progress().wave < 2) g.advanceWave();
    g.startWave();
    check(g.waveMode() == WaveMode::kSurvive, "wave 2 is mode 4");
    check(g.objective().counter == 30, "and asks for 30 atoms");
    check(g.taskDisplay().count == 30, "which the Task Display shows");

    const int before = g.objective().counter;
    // The first dispense is on frame zero, so one step is one atom.
    g.stepOnce(0);
    check(g.objective().counter == before - 1, "an atom dispensed ticks it down");
    check(g.taskDisplay().count == g.objective().counter, "the display follows");
}

void testWaveCompletesOnlyOnceNothingIsClearing() {
    using namespace tubes;
    Game g(6, 5, Difficulty::k101, 31337u);
    while (g.progress().wave < 5) g.advanceWave();
    g.startWave();

    Board& b = g.boardMutable();
    for (int c = 0; c < 3; ++c) {
        b.set(c, 2, kCyanium);
        b.set(c, 3, kCyanium);
        b.set(c, 4, kCyanium);
    }
    g.stepOnce(0);
    check(g.objective().counter == 0, "three vertical threes spend the objective");
    // `1000:5cff` will not call a wave complete while the clear timer runs -
    // that is what stops a cascade being cut off mid-animation.
    check(!g.waveComplete(), "but the clear timer holds the wave open");
    for (int i = 0; i < 12; ++i) g.stepOnce(0);
    check(g.waveComplete(), "and it completes once the timer runs out");
}

void testEnduranceIgnoresAllOfIt() {
    using namespace tubes;
    Game g(6, 5, Difficulty::k301, 4242u);
    check(g.waveMode() == WaveMode::kEndurance, "a Game starts in Endurance");
    Board& b = g.boardMutable();
    b.set(0, 2, kYellowium);
    b.set(0, 3, kYellowium);
    b.set(0, 4, kYellowium);
    g.stepOnce(0);
    check(g.score() > 0, "a match still scores");
    check(!g.waveComplete(), "and no wave is ever complete");
}


void testPreFilledBeakerIsEightRoundRobin() {
    using namespace tubes;
    // `1000:035e`. Eight atoms, one per column round robin from a random
    // start, coloured `n mod 7 + 1` counting DOWN from 8: 2 1 7 6 5 4 3 2.
    Board b(6, 5);
    FixedRolls roll{{0}};                    // start at column 0
    seedPreFilledBeaker(b, 8, std::ref(roll));
    check(b.count() == 8, "the pre-fill places exactly eight");
    // Six columns, eight atoms: columns 0 and 1 get two, the rest one.
    check(b.typeAt(0, 4) == kGreenium, "the first is n mod 7 + 1 with n = 8");
    check(b.typeAt(1, 4) == kRedium, "then n = 7 gives Redium");
    check(b.typeAt(2, 4) == kPinkium, "then n = 6 wraps to Pinkium");
    check(b.typeAt(0, 3) == kBluium, "the seventh lands on top of the first");
    check(b.typeAt(1, 3) == kGreenium, "and the eighth on the second");
}

void testMarkedAtomsAreFlaggedAndCanBeCovered() {
    using namespace tubes;
    // `1000:0000`. Three marked atoms, then eight ordinary ones on top.
    Board b(6, 5);
    FixedRolls roll{{1}};                    // every roll returns 1 mod n
    placeMarkedAtoms(b, 3, false, false, std::ref(roll));
    int flagged = 0;
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 6; ++c) if (b.isObjective(c, r)) ++flagged;
    check(flagged == 3, "three marked atoms carry the MARKER flag");
    check(b.count() == 3, "and nothing else is placed");

    Board covered(6, 5);
    FixedRolls roll2{{1}};
    placeMarkedAtoms(covered, 3, true, false, std::ref(roll2));
    check(covered.count() == 11, "the covered variant adds eight on top");

    Board xenon(6, 5);
    FixedRolls roll3{{1}};
    placeMarkedAtoms(xenon, 3, false, true, std::ref(roll3));
    int xenons = 0;
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 6; ++c) if (xenon.typeAt(c, r) == kXenon) ++xenons;
    check(xenons == 8, "the Xenon variant rings them with eight Xenons");

    // The two modifier loops SHARE one counter seeded 8, so asking for both
    // gets eight in total, not sixteen. That is the original's own bug and it
    // is transliterated rather than tidied.
    Board both(6, 5);
    FixedRolls roll4{{1}};
    placeMarkedAtoms(both, 3, true, true, std::ref(roll4));
    check(both.count() == 11, "both flags share one counter of eight");
}

void testMorphIsARotationNotARoll() {
    using namespace tubes;
    // `1000:4bf6`. Every ordinary atom steps to the next colour and 7 wraps to
    // 1; specials and Flashium are untouched.
    Board b = make({
        "......",
        "......",
        "......",
        "1278..",     // Redium Greenium Pinkium Flashium
        "34567.",
    });
    b.set(4, 3, kXenon);          // make() only understands single digits
    rotateBeakerColours(b);
    check(b.typeAt(0, 3) == kGreenium, "Redium becomes Greenium");
    check(b.typeAt(1, 3) == kBluium, "Greenium becomes Bluium");
    check(b.typeAt(2, 3) == kRedium, "Pinkium wraps to Redium");
    check(b.typeAt(3, 3) == kFlashium, "Flashium is left alone");
    check(b.typeAt(4, 3) == kXenon, "and so is a Xenon");
    check(b.typeAt(4, 4) == kRedium, "the bottom row rotates too");

    // The point of it being a rotation: it is a permutation, so a chain that
    // existed before still exists after. A re-roll would not have that.
    Board chain = make({
        "......",
        "......",
        "5.....",
        "5.....",
        "5.....",
    });
    rotateBeakerColours(chain);
    check(chain.typeAt(0, 2) == kYellowium && chain.typeAt(0, 3) == kYellowium &&
              chain.typeAt(0, 4) == kYellowium,
          "a vertical three is still a vertical three, one colour along");
}

void testMorphSkipsAClearingCell() {
    using namespace tubes;
    // A cell mid-fade holds `type + 19*frame`, so the `< 8` test excludes it
    // for free - and the marked plane excludes it again.
    Board b = make({
        "......",
        "......",
        "3.....",
        "3.....",
        "3.....",
    });
    b.step();                       // marks the run and starts the fade
    const Cell before = b.at(0, 4);
    rotateBeakerColours(b);
    check(b.at(0, 4) == before, "a clearing cell does not morph");
}


void testCrystalsArePlacedWithStaggeredClocks() {
    using namespace tubes;
    Board b(6, 5);
    std::vector<Crystal> xs;
    FixedRolls roll{{1}};
    placeCrystals(b, xs, 2, 50, std::ref(roll));
    check(xs.size() == 2, "two crystals are placed");
    check(b.count() == 2, "and both are in the beaker");
    for (const Crystal& x : xs) {
        check(b.typeAt(x.col, x.row) == kCrystal, "each record points at its cell");
        check(x.active && !x.arriving && !x.departing, "and starts settled");
    }
    // `interval * 10 * i div n` - evenly spread over one period.
    check(xs[0].timer == 250 && xs[1].timer == 500,
          "the two clocks are staggered across one period");
}

void testCrystalTeleportsOutAndBackIn() {
    using namespace tubes;
    Board b = make({
        "......",
        "......",
        "......",
        "......",
        "12345.",
    });
    std::vector<Crystal> xs(1);
    Crystal& x = xs[0];
    x.active = true;
    x.col = 0; x.row = 4;
    b.set(0, 4, static_cast<Cell>(kCrystal));
    x.timer = 0;                       // due now

    FixedRolls roll{{2}};              // destination column 2, row offset 2
    check(stepCrystals(b, xs, 50, std::ref(roll)), "the clock fires and plays CRFADE");
    check(x.departing && x.step == 7, "it leaves over seven steps");
    check(b.isMarked(0, 4), "and marks its own cell so the fade pass runs it out");
    check(x.timer == 500, "the clock reloads to interval * 10");

    for (int i = 0; i < 6; ++i) {
        stepCrystals(b, xs, 50, std::ref(roll));
        check(x.departing, "still leaving");
    }
    check(stepCrystals(b, xs, 50, std::ref(roll)), "the seventh step lands it");
    check(!x.departing && x.arriving, "and turns the fade around");
    check(x.col == x.destCol && x.row == x.destRow, "the record moved with it");
    // 151 = 18 + 19*7, the LAST frame of CRFADE - it walks back from there.
    check(b.at(x.col, x.row) == kCrystal + kFadeStride * 7,
          "it arrives on the last fade frame");
    check(!b.isMarked(x.col, x.row), "with the destination unmarked");

    // Seven steps of 19 walk 151 back to 18, and the flag clears on the frame
    // AFTER that - the test is `if cell = 18 then arriving := false`, so it
    // costs one more tick to notice.
    for (int i = 0; i < 7; ++i) stepCrystals(b, xs, 50, std::ref(roll));
    check(b.typeAt(x.col, x.row) == kCrystal, "and walks back to the static sprite");
    check(x.arriving, "the flag is still set on the frame it arrives");
    stepCrystals(b, xs, 50, std::ref(roll));
    check(!x.arriving, "and clears on the next one");
}

void testCrystalGoesOnlyToAntiMatter() {
    using namespace tubes;
    Game g(6, 5, Difficulty::k101, 5150u);
    while (g.progress().wave < 50) g.advanceWave();
    g.startWave();
    check(g.waveMode() == WaveMode::kCrystals, "wave 50 is mode 5");
    check(g.objective().counter == 1, "and asks for one crystal");
    check(g.crystals().size() == 1, "which is in the beaker");

    const int col = g.crystals()[0].col;
    const int row = g.crystals()[0].row;
    Board& b = g.boardMutable();

    // Clearing a chain right beside it does nothing at all.
    const int other = (col + 1) % 6;
    b.set(other, 2, kRedium);
    b.set(other, 3, kRedium);
    b.set(other, 4, kRedium);
    for (int i = 0; i < 12; ++i) g.stepOnce(0);
    check(g.objective().counter == 1, "a chain beside a crystal does not remove it");

    // AntiMatter landing on it does. `1000:0e08` blasts 3x3 and tells
    // `1000:041c` about every cell it consumes.
    b.set(col, row, static_cast<Cell>(kCrystal));
    b.set(col, row - 1 < 0 ? row + 1 : row - 1, kAntiMatter);
    for (int i = 0; i < 4; ++i) g.stepOnce(0);
    check(g.objective().counter == 0, "AntiMatter is the only thing that removes one");
}

void testCrystalRecordFollowsItsCellDown() {
    using namespace tubes;
    // `1000:04ca`. Without it a crystal that settled a row would be
    // invulnerable, because the removal would look where it no longer is.
    Board b(6, 5);
    std::vector<Crystal> xs(1);
    xs[0].active = true;
    xs[0].col = 3;
    xs[0].row = 1;
    b.set(3, 1, static_cast<Cell>(kCrystal));
    b.setCrystalFellObserver([&xs](int c, int from, int to) {
        crystalCellFell(xs, c, from, to);
    });
    b.step();
    check(b.typeAt(3, 2) == kCrystal, "the cell fell one row");
    check(xs[0].row == 2, "and the record followed it");
}


void testTaskDisplayCyclesWhenNothingIsRequired() {
    using namespace tubes;
    // `1000:48ab`, on the four-frame Flashium tick. A wave that names no
    // colour points its Task Display at the SAME cycling value Flashium uses,
    // which is the rotating counter the wave 6 sampling measured.
    WaveObjective any;
    any.mode = WaveMode::kColour;
    any.reqColour = 0;
    any.anyOrientation = true;
    TaskDisplay t = seedTaskDisplay(any);
    tickTaskDisplay(any, t, kBluium);
    check(t.colour == kBluium, "a colourless wave follows the flash colour");
    check(t.chain == chaincode::kHorizontal, "and its chain picture cycles too");
    tickTaskDisplay(any, t, kCyanium);
    check(t.chain == chaincode::kVertical, "0 -> 1 -> 2");
    tickTaskDisplay(any, t, kCyanium);
    check(t.chain == chaincode::kDiagonal, "and wraps back to diagonal");

    // A wave that DOES name one keeps it, and its chain picture holds still.
    WaveObjective named;
    named.mode = WaveMode::kColour;
    named.reqColour = kPurplium;
    named.reqChain = chaincode::kVertical;
    TaskDisplay t2 = seedTaskDisplay(named);
    tickTaskDisplay(named, t2, kBluium);
    check(t2.colour == kPurplium, "a named colour is not overwritten");
    check(t2.chain == chaincode::kVertical, "and a required chain does not cycle");

    // Mode 2 names a colour in its record but does not require one, so it
    // cycles anyway - which is why the mode is in the original's test.
    WaveObjective orient;
    orient.mode = WaveMode::kOrientation;
    orient.reqColour = kGreenium;
    TaskDisplay t3 = seedTaskDisplay(orient);
    tickTaskDisplay(orient, t3, kYellowium);
    check(t3.colour == kYellowium, "mode 2 cycles despite holding a colour");
    check(t3.chain == orient.reqChain, "but shows the chain it wants");

    // Both diagonals count, so the picture alternates between them.
    check(t3.diagonalFlip, "the diagonal illustration flips every tick");
    tickTaskDisplay(orient, t3, kYellowium);
    check(!t3.diagonalFlip, "and flips back");
}


void testHiddenAtomsConcealButDoNotChangeAnything() {
    using namespace tubes;
    // `-0x189`. Six draw sites in `1000:3a67` swap MYSTBALL in for the real
    // ball, and they are the six NETWORK records - not the tube's contents,
    // not records 7..12, not the beaker. So it is pure presentation: the atom
    // keeps its type all the way through and the concealment ends on the
    // catch, which is what "hidden until they leave a tube" means.
    Game g(6, 5, Difficulty::k101, 20250730u);
    while (g.progress().wave < 30) g.advanceWave();
    g.startWave();
    check(g.objective().hiddenAtoms, "wave 30 hides the atoms in the tubes");
    check(g.waveMode() == WaveMode::kSurvive, "and is still a survive wave");

    // Run until something is in flight, then catch it and check the type came
    // through untouched.
    int col = 0;
    for (int i = 0; i < 200 && col == 0; ++i) {
        g.stepOnce(0);
        for (int c = 1; c <= kAtomSlots; ++c) {
            if (g.atom(c).drawn()) { col = c; break; }
        }
    }
    check(col != 0, "an atom is dispensed");
    const int8_t type = g.atom(col).colour;
    check(type >= kRedium && type <= kMystery, "and carries a real type");
    check(type != kMystery, "which is never MYSTBALL - that is a render state");

    Game plain(6, 5, Difficulty::k101, 20250730u);
    while (plain.progress().wave < 2) plain.advanceWave();
    plain.startWave();
    check(!plain.objective().hiddenAtoms, "wave 2 hides nothing");
}


// ---------------------------------------------------------------------------
// The title screen and the menu, 1b2e:52bf / 1b2e:4d80
// ---------------------------------------------------------------------------

// The circuit has to close: leg 25 ends where leg 1 begins. A path table read
// at the wrong stride would not, which is what makes this a real check on the
// data rather than a restatement of it.
void testTitlePathClosesAndVisitsEveryLeg() {
    tubes::TitleAtom a;
    bool seen[tubes::kTitleLegs + 1] = {};
    int guard = 0;
    // Walk until leg 1 comes round again having passed through all 25.
    while (guard++ < 20000) {
        seen[a.leg] = true;
        a.step();
        if (a.leg == 1 && seen[tubes::kTitleLegs]) break;
    }
    check(guard < 20000, "title path completes a circuit");
    int missed = 0;
    for (int i = 1; i <= tubes::kTitleLegs; ++i) if (!seen[i]) ++missed;
    check(missed == 0, "title path walks all 25 legs");
    // Leg 1 descends from x = 61, and leg 25 leaves the atom there.
    check(a.x == 61, "title path closes on x = 61");
}

// Leg 23's dirV byte is neither 'F' nor 'B', so that leg must not round its
// corner - it is the T's stem walked down and straight back up.
void testTitleLegTwentyThreeDoesNotCurve() {
    check(tubes::titleLegDirV(23) == '?', "leg 23 has neither F nor B");
    tubes::TitleAtom a;
    a.leg = 23;
    a.x = tubes::titleLegLimitX(23);
    a.y = tubes::titleLegLimitY(23) - 8;   // inside the 10 px curve window
    const int x0 = a.x;
    a.step();
    check(a.x == x0, "leg 23 leaves the cross-axis alone");

    // ...where an ordinary leg in the same position does move it.
    tubes::TitleAtom b;
    b.leg = 13;                            // 'D', 'B'
    b.x = tubes::titleLegLimitX(13);
    b.y = tubes::titleLegLimitY(13) - 8;
    const int bx0 = b.x;
    b.step();
    check(b.x != bx0, "an ordinary leg does curve");
}

// The layout numbers, checked against captures of the original. The +1 on the
// count and the +2 on the star row were both wrong until a capture caught
// them, so they are pinned here.
void testMenuLayoutMatchesTheCaptures() {
    using tubes::Page;
    // Main menu, 8 items: yBase 18, "Start Game" starred at 100/203, y 36.
    check(tubes::menuYBase(Page::kMain) == 18, "main menu yBase is 18");
    tubes::StarPlacement s = tubes::placeStars(Page::kMain, 1);
    check(s.xLeft == 100 && s.xRight == 203, "Start Game stars at 100/203");
    check(s.y == 36, "Start Game star row is 36");

    // Game Mode, 3 items: "Endurace Mode" is 13 long.
    s = tubes::placeStars(Page::kGameMode, 1);
    check(s.xLeft == 88 && s.xRight == 215, "Endurace Mode stars at 88/215");
    check(s.y == 76, "Endurace Mode star row is 76");

    // Difficulty, 4 items, three selections down the page.
    check(tubes::menuYBase(Page::kDifficulty) == 50, "difficulty yBase is 50");
    check(tubes::placeStars(Page::kDifficulty, 1).y == 68, "Tubes 101 row");
    check(tubes::placeStars(Page::kDifficulty, 2).y == 84, "Tubes 201 row");
    check(tubes::placeStars(Page::kDifficulty, 3).y == 100, "Tubes 301 row");

    // The text row is two above the star row.
    check(tubes::menuItemY(Page::kDifficulty, 1) == 66, "text row is star - 2");

    // Game Options is the one page on a 26 px pitch.
    check(tubes::menuItemY(Page::kOptions, 2) -
          tubes::menuItemY(Page::kOptions, 1) == 26, "options pitch is 26");
}

// The rule under a page title is length(title) - 2 underscores.
void testMenuRuleIsTwoShortOfTheTitle() {
    check(tubes::menuRule(tubes::Page::kDifficulty) == "________",
          "Difficulty rules with 8 underscores");
    check(tubes::menuRule(tubes::Page::kMain).empty(),
          "the main menu has no title and no rule");
}

// Start Game and Continue Saved Game both land on Game Mode, and the flag they
// set is what decides whether Game Mode then goes to Difficulty or to a slot
// list. That flag is DS:0x1d4c.
void testStartAndContinueDivergeAtGameMode() {
    tubes::Menu m;
    m.raise();
    check(m.page() == tubes::Page::kMain, "menu opens on the main page");
    check(m.select() == tubes::MenuResult::kNone, "Start Game does not leave");
    check(m.page() == tubes::Page::kGameMode, "Start Game -> Game Mode");
    check(m.choice().newGame, "Start Game sets newGame");
    m.select();                                    // Endurace Mode
    check(m.page() == tubes::Page::kDifficulty, "new game -> Difficulty");

    tubes::Menu c;
    c.raise();
    c.moveDown();                                  // Continue Saved Game
    c.select();
    check(!c.choice().newGame, "Continue clears newGame");
    c.select();                                    // Endurace Mode
    check(c.page() == tubes::Page::kSavesEndurance, "load -> endurance slots");
}

// Difficulty is the only place DS:0x1d4f is set, and it leaves the title
// screen. Tubes 101/201/301 are 0/1/2.
void testDifficultyLeavesWithTheChoice() {
    for (int i = 0; i < 3; ++i) {
        tubes::Menu m;
        m.raise();
        m.select();                     // Start Game
        m.select();                     // Endurace Mode
        for (int k = 0; k < i; ++k) m.moveDown();
        check(m.select() == tubes::MenuResult::kPlay, "difficulty starts play");
        check(m.choice().difficulty == i, "difficulty index");
        check(m.choice().mode == 1, "mode is endurance");
    }
}

// Wave Mode is DS:0x1d4e = 2.
void testWaveModeSetsModeTwo() {
    tubes::Menu m;
    m.raise();
    m.select();                         // Start Game
    m.moveDown();                       // Wave Mode
    m.select();
    m.select();                         // Tubes 101
    check(m.choice().mode == 2, "Wave Mode sets mode 2");
}

// An empty save slot is ignored rather than accepted - the original re-tests
// the record's first byte after copying it.
void testEmptySaveSlotDoesNotLeaveTheMenu() {
    tubes::Menu m;
    m.raise();
    m.moveDown();                       // Continue Saved Game
    m.select();
    m.select();                         // Endurace Mode -> slot list
    check(m.page() == tubes::Page::kSavesEndurance, "on the slot list");
    check(m.select() == tubes::MenuResult::kNone, "empty slot is ignored");
    m.setSaveSlotLive(1, 1, true);
    check(m.select() == tubes::MenuResult::kLoad, "a live slot loads");
    check(m.choice().slot == 1, "the slot is recorded");
}

// Arriving back at the main menu restores the row you left from - DS:0x1d43 -
// while every other page starts at its first item.
void testMainMenuRemembersItsRow() {
    tubes::Menu m;
    m.raise();
    m.moveDown();
    m.moveDown();                       // Game Options
    check(m.item() == 3, "moved to Game Options");
    m.select();
    check(m.page() == tubes::Page::kOptions, "on Game Options");
    check(m.item() == 1, "a submenu starts at item 1");
    for (int i = 0; i < 3; ++i) m.moveDown();
    m.select();                         // Exit
    check(m.page() == tubes::Page::kMain, "back on the main menu");
    check(m.item() == 3, "and back on the row we left from");
}

// The selection wraps, which the rig notes rely on for navigation.
void testMenuSelectionWraps() {
    tubes::Menu m;
    m.raise();
    m.moveUp();
    check(m.item() == 8, "up from the first item wraps to the last");
    m.moveDown();
    check(m.item() == 1, "and down again wraps back");
}

// Four frames on a three-frame divider: a 12-frame cycle.
void testStarTurnsEveryThreeFrames() {
    tubes::Menu m;
    check(m.starFrame() == 1, "star starts on frame 1");
    for (int i = 0; i < 3; ++i) m.tick();
    check(m.starFrame() == 2, "advances after three frames");
    for (int i = 0; i < 9; ++i) m.tick();
    check(m.starFrame() == 1, "and wraps after twelve");
}

// Exit Tubes is a confirm page, and No returns rather than quitting.
void testQuitNeedsConfirming() {
    tubes::Menu m;
    m.raise();
    for (int i = 0; i < 7; ++i) m.moveDown();
    check(m.item() == 8, "on Exit Tubes");
    check(m.select() == tubes::MenuResult::kNone, "Exit Tubes asks first");
    check(m.page() == tubes::Page::kQuit, "on the confirm page");
    m.moveDown();
    check(m.select() == tubes::MenuResult::kNone, "No does not quit");
    check(m.page() == tubes::Page::kMain, "and returns to the main menu");

    tubes::Menu y;
    y.raise();
    for (int i = 0; i < 7; ++i) y.moveDown();
    y.select();
    check(y.select() == tubes::MenuResult::kQuit, "Yes quits");
}

// The four items that leave the title screen return their own item number,
// which is how 1b2e:52bf reports them to its caller.
void testInformationalItemsReturnTheirNumber() {
    const int items[] = {4, 5, 6, 7};
    const tubes::MenuResult want[] = {
        tubes::MenuResult::kHighScores, tubes::MenuResult::kInstructions,
        tubes::MenuResult::kCredits, tubes::MenuResult::kCredits};
    for (int k = 0; k < 4; ++k) {
        tubes::Menu m;
        m.raise();
        for (int i = 1; i < items[k]; ++i) m.moveDown();
        tubes::MenuResult r = m.select();
        check(static_cast<int>(r) == items[k], "item leaves with its number");
        (void)want;
    }
}


// The corner curve exists to lead the atom into the next leg. So at the moment
// a leg hands over, the cross-axis must be displaced TOWARD the way the next
// leg travels - and that is a property of the path as a whole, so it catches a
// sign error on any single leg.
//
// It was written because one existed: 'U' and 'D' displace the opposite way to
// 'L' and 'R', and applying one sign to all four made the atom curve outward at
// every corner entered on a vertical, clipping outside the pipe.
void testCornersCurveTowardTheNextLeg() {
    tubes::TitleAtom a;
    int endX[tubes::kTitleLegs + 1] = {}, endY[tubes::kTitleLegs + 1] = {};
    bool got[tubes::kTitleLegs + 1] = {};
    for (int i = 0; i < 4000; ++i) {
        const int leg = a.leg;
        a.step();
        if (a.leg != leg) { endX[leg] = a.x; endY[leg] = a.y; got[leg] = true; }
    }

    int seen = 0, bad = 0;
    for (int n = 1; n <= tubes::kTitleLegs; ++n) {
        // Leg 23 is the T's stem and hands over to another vertical, so there
        // is no cross-axis to lead with. It is covered by its own test.
        if (!got[n] || n == 23) continue;
        const int next = (n % tubes::kTitleLegs) + 1;
        const char dh = tubes::titleLegDirH(n);
        const char nd = tubes::titleLegDirH(next);
        ++seen;
        if (dh == 'U' || dh == 'D') {
            const int d = endX[n] - tubes::titleLegLimitX(n);
            if ((nd == 'R' && d <= 0) || (nd == 'L' && d >= 0)) ++bad;
        } else {
            const int d = endY[n] - tubes::titleLegLimitY(n);
            if ((nd == 'D' && d <= 0) || (nd == 'U' && d >= 0)) ++bad;
        }
    }
    check(seen == tubes::kTitleLegs - 1, "every leg but 23 was measured");
    check(bad == 0, "every corner curves toward the next leg");
}

}  // namespace

int main() {
    testHorizontal();
    testInertSpecialsDoNotMatch();
    testFlashiumWildcard();
    testVertical();
    testDiagonalDown();
    testDiagonalUp();
    testRunOfTwoIgnored();
    testLongRun();
    testMixedRunBoundary();
    testFadeEncoding();
    testFadeClearsAfterEightSteps();
    testFadingCellCannotRematch();
    testGravityMovesOneRowPerStep();
    testMarkerTravelsWithTheAtom();
    testObjectivePlane();
    testDisabledElement();
    testAwardPerSeed();
    testAwardByOrientation();
    testDistinctRunsAreTheMultiplier();
    testCascade();
    testDropAndOverflow();
    testFullBeakerDoesNotEndTheGame();
    testSpeedBoostNeedsHolding();
    testBonusAtomIsFastByType();
    testAntiMatterBlastsThreeByThree();
    testAntiMatterClipsAtTheEdges();
    testBlockerFillsItsColumnAbove();
    testBlockerIsGated();
    testConvertorConvertsByTypeBoardWide();
    testConvertorNeedsAnOrdinaryVictim();
    testSettledConsumablesGoInert();
    testBonusCatchPaysAndGrows();
    testMultiplierFillsTheTube();
    testEvilMultiplierFillsWithXenon();
    testFillerParksAnImmovableAtom();
    testCatchSpecialsAreGated();
    testTheTubeIsAStack();
    testTipRunsFourPhasesOverSixFrames();
    testTipMovesTheContents();
    testCaughtAtomSlidesToItsSlot();
    testTippedAtomFallsIntoAFullColumnAndIsLost();
    testTextColourWalksDownTheCell();
    testTextPeakMode();
    testTextShadowAndSpaces();
    testTextCentring();
    testDescentAcceleratesBelowFifty();
    testCatchIsAWindow();
    testTippingTubeCannotCatch();
    testMissedBonusCostsNothing();
    testFlashiumCyclesEveryFourFrames();
    testSfxHeader();
    testSfxVoiceResamples();
    testSfxVoiceIsSingle();
    testTurboPascalRandom();
    testScrHeader();
    testFirstDispenseIsImmediate();
    testInputIsReadOnlyWhileTheTubeIsIdle();
    testTipSkipsFiveFramesOfInput();
    testEnduranceRampStepsOnMatches();
    testEnduranceRampCountsPerRunNotPerFrame();
    testWaveTableReproducesTheSampledBriefings();
    testSeedsProduceTheSampledCounts();
    testWaveProgressionStepsOnFifteensAndTwenties();
    testCreditRunHonoursColourAndOrientation();
    testTaskRotationWrapsAndPicksItsClock();
    testContinueReplaysTheSameObjective();
    testMysteryWaveHidesOneOfFour();
    testEveryWaveHasAnArm();
    testAWaveCountsDownThroughTheGame();
    testRunOfFourTicksTheObjectiveTwice();
    testSurviveWaveCountsAtomsDispensed();
    testWaveCompletesOnlyOnceNothingIsClearing();
    testEnduranceIgnoresAllOfIt();
    testPreFilledBeakerIsEightRoundRobin();
    testMarkedAtomsAreFlaggedAndCanBeCovered();
    testMorphIsARotationNotARoll();
    testMorphSkipsAClearingCell();
    testCrystalsArePlacedWithStaggeredClocks();
    testCrystalTeleportsOutAndBackIn();
    testCrystalGoesOnlyToAntiMatter();
    testCrystalRecordFollowsItsCellDown();
    testTaskDisplayCyclesWhenNothingIsRequired();
    testHiddenAtomsConcealButDoNotChangeAnything();
    testTitlePathClosesAndVisitsEveryLeg();
    testTitleLegTwentyThreeDoesNotCurve();
    testCornersCurveTowardTheNextLeg();
    testMenuLayoutMatchesTheCaptures();
    testMenuRuleIsTwoShortOfTheTitle();
    testStartAndContinueDivergeAtGameMode();
    testDifficultyLeavesWithTheChoice();
    testWaveModeSetsModeTwo();
    testEmptySaveSlotDoesNotLeaveTheMenu();
    testMainMenuRemembersItsRow();
    testMenuSelectionWraps();
    testStarTurnsEveryThreeFrames();
    testQuitNeedsConfirming();
    testInformationalItemsReturnTheirNumber();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
