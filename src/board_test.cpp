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

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
