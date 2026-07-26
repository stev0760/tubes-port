#include "board.h"

#include <algorithm>

namespace tubes {
namespace {

// Horizontal, vertical, and both diagonals. Each run is scanned once, from
// the end that has no same-coloured neighbour behind it.
struct Dir {
    int dc, dr;
};
constexpr Dir kDirs[] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};

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

int Board::findMatches(std::vector<uint8_t>& marked) const {
    marked.assign(cells_.size(), 0);

    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            int8_t colour = at(c, r);
            if (colour == kEmpty) continue;

            for (const Dir& d : kDirs) {
                // Only start scanning at the beginning of a run, so each run
                // is considered exactly once.
                if (at(c - d.dc, r - d.dr) == colour) continue;

                int len = 0;
                while (at(c + d.dc * len, r + d.dr * len) == colour) ++len;
                if (len < kMinRun) continue;

                for (int i = 0; i < len; ++i) {
                    int cc = c + d.dc * i;
                    int rr = r + d.dr * i;
                    marked[static_cast<size_t>(rr) * cols_ + cc] = 1;
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
