// The beaker: a grid of atoms, with match detection and settling.
//
// The grid is 6 x 5, measured off the loop bounds in 1000:3a67, and the
// cell-to-pixel mapping is measured too - see docs/reversing-notes.md.
// Scoring is only partly recovered; awardForRun() in game.cpp says exactly
// which part is measured and which is fitted.

#pragma once

#include <cstdint>
#include <vector>

namespace tubes {

// Atom types, numbered exactly as the original numbers them. This is not an
// arbitrary internal encoding: a settled beaker cell holds the atom record's
// type byte (+0x0b) directly, confirmed at 96.2% over 79 settle events, so
// these values are what the original's own grid contains. Keeping them
// identical means a trace captured from the original can be compared against
// this engine's board without a translation table.
//
// Note the ordering is NOT the sprite-file order that an earlier version of
// this enum used (Red, Blue, Green, Yellow, Purple, Cyan, Pink): green and
// blue are transposed, as are yellow and cyan/purple.
enum Atom : int8_t {
    kEmpty = 0,
    kRedium = 1,
    kGreenium = 2,
    kBluium = 3,
    kCyanium = 4,
    kPurplium = 5,
    kYellowium = 6,
    kPinkium = 7,
    kFlashium = 8,          // wildcard; has no sprite, cycles the 7 colours
    kAntiMatter = 9,
    kBonus = 10,            // becomes Flashium when caught, awards a drop
    kXenon = 11,            // inert
    kMultiplier = 12,
    kEvilMultiplier = 13,
    kConvertor = 14,
    kBlocker = 15,
    kFiller = 16,
    kTypeCount = 17,        // 0..16 inclusive
};

// Types 1..7 are the ordinary colours: the ones that spawn freely and match
// each other. Everything from 8 up is a special.
constexpr int8_t kFirstColour = kRedium;
constexpr int8_t kLastColour = kPinkium;
constexpr int kColourCount = 7;

// Only the ordinary colours and Flashium take part in matching. Xenon is inert
// - it settles in the beaker and simply sits there - and the remaining
// specials either never settle as themselves (Bonus becomes Flashium when
// caught) or have behaviours of their own.
inline bool isMatchable(int8_t v) {
    return v >= kRedium && v <= kFlashium;
}

// NOT YET IMPLEMENTED: Flashium is a wildcard. The Instructions say chains form
// "of the same element **or in combination with Flashium atoms**", so a run may
// mix one real colour with Flashium. It is left out deliberately rather than
// guessed at, for two reasons: whether a run of three Flashium and no element
// matches at all is unknown from any evidence gathered so far, and the board
// tests encode atoms as digits - `testCascade` uses 8s, which now means
// Flashium - so turning it into a wildcard silently changes what those tests
// assert. Both need settling before the mechanic goes in.

// The measured cell-to-pixel mapping. The column pitch is 18, not the 16 the
// sprite width would suggest, and the x values are a six-entry table rather
// than an arithmetic run in the original - they happen to be evenly spaced.
constexpr int kColumnX[] = {107, 125, 143, 161, 179, 197};
constexpr int kRowY0 = 121;
constexpr int kRowPitchY = 13;

inline int playColumnX(int col) {
    if (col < 0) col = 0;
    if (col > 5) col = 5;
    return kColumnX[col];
}

// How a run of matching atoms is oriented. The original scores these
// differently - see awardForRun().
enum class RunKind : uint8_t { kHorizontal, kVertical, kDiagonal };

struct Run {
    RunKind kind = RunKind::kHorizontal;
    int length = 0;
};

class Board {
public:
    Board(int cols, int rows);

    int cols() const { return cols_; }
    int rows() const { return rows_; }

    int8_t at(int c, int r) const;
    void set(int c, int r, int8_t v);
    bool inBounds(int c, int r) const;

    void clear();

    // Lowest free row in a column, or -1 when the column is full.
    int dropRow(int c) const;

    // Places an atom at the bottom of a column. Returns false if full.
    bool drop(int c, int8_t colour);

    // Marks every atom belonging to a run of 3 or more - horizontal,
    // vertical, or either diagonal. Returns how many cells were marked.
    // When `runs` is given it also reports each run's orientation and length,
    // which scoring needs; the original pays a diagonal differently from a
    // line.
    int findMatches(std::vector<uint8_t>& marked,
                    std::vector<Run>* runs = nullptr) const;

    // Removes marked cells and lets the atoms above settle downward.
    void removeMarked(const std::vector<uint8_t>& marked);

    // True when any column has reached the top.
    bool overflowing() const;

    int count() const;

private:
    int cols_;
    int rows_;
    std::vector<int8_t> cells_;   // row 0 is the top
};

}  // namespace tubes
