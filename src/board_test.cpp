// Tests for beaker match detection and settling.
//
// The scripted player in --auto rarely produces a match by chance with eight
// atom colours, so the matching rules need testing directly.

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

int matchCount(const tubes::Board& b) {
    std::vector<uint8_t> marked;
    return b.findMatches(marked);
}

void clearOnce(tubes::Board& b) {
    std::vector<uint8_t> marked;
    b.findMatches(marked);
    b.removeMarked(marked);
}

void testHorizontal() {
    tubes::Board b = make({"....", "....", "111."});
    check(matchCount(b) == 3, "horizontal run of 3 detected");
    clearOnce(b);
    check(b.count() == 0, "horizontal run cleared");
}

void testVertical() {
    tubes::Board b = make({"2...", "2...", "2..."});
    check(matchCount(b) == 3, "vertical run of 3 detected");
    clearOnce(b);
    check(b.count() == 0, "vertical run cleared");
}

void testDiagonalDown() {
    tubes::Board b = make({"3...", ".3..", "..3."});
    check(matchCount(b) == 3, "descending diagonal detected");
}

void testDiagonalUp() {
    tubes::Board b = make({"..4.", ".4..", "4..."});
    check(matchCount(b) == 3, "ascending diagonal detected");
}

void testRunOfTwoIgnored() {
    tubes::Board b = make({"....", "....", "55.."});
    check(matchCount(b) == 0, "run of 2 is not a match");
}

void testLongRun() {
    tubes::Board b = make({"......", "......", "666666"});
    check(matchCount(b) == 6, "run of 6 marks all six");
}

void testMixedRunBoundary() {
    // A 3-run must not absorb the differently coloured neighbour beside it.
    tubes::Board b = make({"....", "....", "7772"});
    check(matchCount(b) == 3, "match stops at a colour change");
}

void testGravityAfterClear() {
    // Clearing the bottom row should let the atom above settle down.
    tubes::Board b = make({"....", "9...", "111."});
    clearOnce(b);
    check(b.count() == 1, "one atom survives the clear");
    check(b.at(0, 2) == 9, "surviving atom settled to the floor");
}

void testCascade() {
    // The 8s sit at staggered heights so they do not match initially. Once
    // the bottom row of 1s clears they all settle onto the floor, forming a
    // second match that must resolve in its own round.
    tubes::Board b = make({"....", ".8..", "8.8.", "1111"});
    check(matchCount(b) == 4, "only the bottom row matches to begin with");

    std::vector<uint8_t> marked;
    int rounds = 0;
    while (b.findMatches(marked) > 0) {
        b.removeMarked(marked);
        ++rounds;
    }
    check(rounds == 2, "cascade resolves in two rounds");
    check(b.count() == 0, "cascade clears the board");
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

}  // namespace

int main() {
    testHorizontal();
    testVertical();
    testDiagonalDown();
    testDiagonalUp();
    testRunOfTwoIgnored();
    testLongRun();
    testMixedRunBoundary();
    testGravityAfterClear();
    testCascade();
    testDropAndOverflow();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
