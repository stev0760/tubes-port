// The beaker: a grid of atoms, with match detection and settling.
//
// Grid dimensions and scoring are NOT reverse engineered - see
// docs/reversing-notes.md. They are chosen to play sensibly against the
// original artwork and should be replaced once the playfield renderer at
// 1000:9e53 is decompiled.

#pragma once

#include <cstdint>
#include <vector>

namespace tubes {

// The eight elements of the story. Sprite names in the same order.
enum Atom : int8_t {
    kEmpty = -1,
    kRed = 0,
    kBlue,
    kGreen,
    kYellow,
    kPurple,
    kCyan,
    kPink,
    kGold,
    kAtomCount
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
    int findMatches(std::vector<uint8_t>& marked) const;

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
