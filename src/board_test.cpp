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
        for (int f = 0; f < frames; ++f) h.update(btn, 1.0f / 18.2f);
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
    for (int f = 0; f < 5; ++f) h.update(0, 1.0f / 18.2f);
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
    for (int i = 0; i < 15; ++i) g.update(0, 1.0f / 18.2f);
}

// Holds A long enough for one whole tipping animation - six frames, two per
// phase. A is level-triggered, so holding it just tips again.
void tipOnce(tubes::Game& g) {
    for (int i = 0; i < 6; ++i) g.update(tubes::button::kA, 1.0f / 18.2f);
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
    for (int i = 0; i < 15; ++i) g.update(0, 1.0f / 18.2f);
    check(g.score() == 1000, "the first Bonus pays exactly 1000");
    check(g.dropsRemaining() == 10, "and exactly one drop, not one a frame");

    // 1000:0846 adds 1000 to a word of its own and pays the whole word, so the
    // second Bonus of a session is worth 2000.
    const int before = g.score();
    catchOne(g, tubes::kBonus, g.tubeTypes());
    for (int i = 0; i < 15; ++i) g.update(0, 1.0f / 18.2f);
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
    for (int i = 0; i < 20; ++i) g.update(0, 1.0f / 18.2f);
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
        g.update(tubes::button::kA, 1.0f / 18.2f);
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

    for (int f = 0; f < 3; ++f) g.update(tubes::button::kA, 1.0f / 18.2f);
    check(g.tubeAtoms()[0].y == 99 && g.tubeAtoms()[1].y == 93 &&
          g.tubeAtoms()[2].y == 87, "phase 2 bunches them toward the middle");
    check(g.tubeAtoms()[0].x == restX - 1, "and steps every slot 1 px left");

    for (int f = 0; f < 2; ++f) g.update(tubes::button::kA, 1.0f / 18.2f);
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

    g.update(0, 1.0f / 18.2f);
    check(g.tubeAtoms().size() == 1, "the atom is caught");
    // 55 + 9 = 64, inside the 60..70 window, and the slide then takes it on to
    // 73 on the very same frame - the tube's slot loop runs after the network's.
    check(g.tubeAtoms()[0].y == 73 && !g.tubeAtoms()[0].arrived,
          "and starts sliding rather than appearing in its slot");
    for (int f = 0; f < 6; ++f) g.update(0, 1.0f / 18.2f);
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
    for (int f = 0; f < 30; ++f) g.update(0, 1.0f / 18.2f);
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
    g.update(0, 1.0f / 18.2f);
    check(g.atom(6).y == 32, "above 50 it falls at the difficulty's 2 px");

    tubes::Game h(6, 5, tubes::Difficulty::k101, 5);
    h.setTubeColumn(0);
    descendColumn6(h, 80, tubes::kRedium);
    h.update(0, 1.0f / 18.2f);
    check(h.atom(6).y == 89, "at or below 50 it falls a flat 9");
}

void testCatchIsAWindow() {
    // 60..70, `1000:1423`. Below it is too early and past it is too late; the
    // port used to catch anything at or past the mouth, which meant an atom
    // could be scooped up most of the way to the floor.
    tubes::Game late(6, 5, tubes::Difficulty::k101, 5);
    late.setTubeColumn(3);                    // stop 3 is column 6's x, 161
    descendColumn6(late, 75, tubes::kRedium);
    for (int f = 0; f < 3; ++f) late.update(0, 1.0f / 18.2f);
    check(late.tubeAtoms().empty(), "an atom already past 70 is not caught");

    tubes::Game hit(6, 5, tubes::Difficulty::k101, 5);
    hit.setTubeColumn(3);
    descendColumn6(hit, 55, tubes::kRedium);
    hit.update(0, 1.0f / 18.2f);
    check(hit.tubeAtoms().size() == 1, "one landing inside the window is");
}

void testTippingTubeCannotCatch() {
    // `1000:1463` - the tube will not catch while it is tipping.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    g.setTubeColumn(3);
    g.setTubeAtoms({tubes::kRedium});
    descendColumn6(g, 55, tubes::kGreenium);
    g.update(tubes::button::kA, 1.0f / 18.2f);      // starts the tip
    check(g.tubeAtoms().size() == 1, "nothing is caught mid-tip");
    check(g.atom(6).y == 64, "and the atom carries on falling");
}

void testMissedBonusCostsNothing() {
    // `1000:153e`. The same exemption state 9 makes for a full column - the
    // port had it in one place and not the other.
    tubes::Game g(6, 5, tubes::Difficulty::k101, 5);
    g.setTubeColumn(0);
    descendColumn6(g, 180, tubes::kBonus);
    for (int f = 0; f < 3; ++f) g.update(0, 1.0f / 18.2f);
    check(!g.atom(6).drawn(), "the Bonus fell past and was lost");
    check(g.dropsRemaining() == 9, "and it cost nothing");

    tubes::Game h(6, 5, tubes::Difficulty::k101, 5);
    h.setTubeColumn(0);
    descendColumn6(h, 180, tubes::kRedium);
    for (int f = 0; f < 3; ++f) h.update(0, 1.0f / 18.2f);
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
        g.update(0, 1.0f / 18.2f);
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

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
