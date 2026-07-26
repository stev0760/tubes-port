#include "board.h"

#include <algorithm>

namespace tubes {
namespace {

// Horizontal, vertical, and both diagonals. Each run is scanned once, from
// the end that has no same-coloured neighbour behind it.
struct Dir {
    int dc, dr;
    RunKind kind;
};
constexpr Dir kDirs[] = {{1, 0, RunKind::kHorizontal},
                         {0, 1, RunKind::kVertical},
                         {1, 1, RunKind::kDiagonal},
                         {1, -1, RunKind::kDiagonal}};

constexpr int kMinRun = 3;

}  // namespace

Board::Board(int cols, int rows)
    : cols_(cols), rows_(rows), cells_(static_cast<size_t>(cols) * rows, kEmpty) {}

bool Board::inBounds(int c, int r) const {
    return c >= 0 && c < cols_ && r >= 0 && r < rows_;
}

int8_t Board::at(int c, int r) const {
    if (!inBounds(c, r)) return kEmpty;
    return cells_[static_cast<size_t>(r) * cols_ + c];
}

void Board::set(int c, int r, int8_t v) {
    if (!inBounds(c, r)) return;
    cells_[static_cast<size_t>(r) * cols_ + c] = v;
}

void Board::clear() {
    std::fill(cells_.begin(), cells_.end(), static_cast<int8_t>(kEmpty));
}

int Board::dropRow(int c) const {
    for (int r = rows_ - 1; r >= 0; --r) {
        if (at(c, r) == kEmpty) return r;
    }
    return -1;
}

bool Board::drop(int c, int8_t colour) {
    int r = dropRow(c);
    if (r < 0) return false;
    set(c, r, colour);
    return true;
}

int Board::findMatches(std::vector<uint8_t>& marked,
                       std::vector<Run>* runs) const {
    marked.assign(cells_.size(), 0);
    if (runs) runs->clear();

    // Walk each line once and cut it into maximal runs, rather than testing
    // every cell as a possible run start. Flashium is a wildcard, so "is this
    // the beginning of a run" is no longer a local test - a wildcard can bridge
    // into a run that started earlier - and segmenting whole lines keeps each
    // run reported exactly once.
    for (const Dir& d : kDirs) {
        for (int r = 0; r < rows_; ++r) {
            for (int c = 0; c < cols_; ++c) {
                // Only start from a cell that begins a line in this direction.
                if (inBounds(c - d.dc, r - d.dr)) continue;

                int cc = c, rr = r;
                while (inBounds(cc, rr)) {
                    // Extend a run for as far as the cells stay compatible.
                    //
                    // AMBIGUITY, unresolved: a wildcard between two different
                    // colours could belong to either side. In `1 8 2 2` this
                    // greedy left-to-right walk gives the 8 to the 1, leaving
                    // 1-8 and 2-2 and so no match at all - where handing it to
                    // the 2s would have made a run of three. Which the original
                    // does has not been tested. It only matters when a wildcard
                    // sits exactly between two colours and one side is short.
                    int8_t runColour = kEmpty;   // first real colour seen
                    int len = 0;
                    int sc = cc, sr = rr;
                    while (inBounds(cc, rr)) {
                        const int8_t v = at(cc, rr);
                        if (!isMatchable(v)) break;
                        if (!isWildcard(v)) {
                            if (runColour == kEmpty) {
                                runColour = v;
                            } else if (v != runColour) {
                                break;
                            }
                        }
                        ++len;
                        cc += d.dc;
                        rr += d.dr;
                    }

                    if (len >= kMinRun) {
                        if (runs) runs->push_back(Run{d.kind, len});
                        for (int i = 0; i < len; ++i) {
                            marked[static_cast<size_t>(sr + d.dr * i) * cols_ +
                                   (sc + d.dc * i)] = 1;
                        }
                    }
                    if (len == 0) {          // step over the blocking cell
                        cc += d.dc;
                        rr += d.dr;
                    }
                }
            }
        }
    }

    return static_cast<int>(std::count(marked.begin(), marked.end(), 1));
}

void Board::removeMarked(const std::vector<uint8_t>& marked) {
    for (size_t i = 0; i < cells_.size() && i < marked.size(); ++i) {
        if (marked[i]) cells_[i] = kEmpty;
    }

    // Settle each column downward, preserving order.
    for (int c = 0; c < cols_; ++c) {
        int write = rows_ - 1;
        for (int r = rows_ - 1; r >= 0; --r) {
            int8_t v = at(c, r);
            if (v == kEmpty) continue;
            set(c, r, kEmpty);
            set(c, write, v);
            --write;
        }
    }
}

bool Board::overflowing() const {
    for (int c = 0; c < cols_; ++c) {
        if (at(c, 0) != kEmpty) return true;
    }
    return false;
}

int Board::count() const {
    return static_cast<int>(
        std::count_if(cells_.begin(), cells_.end(),
                      [](int8_t v) { return v != kEmpty; }));
}

}  // namespace tubes
