#include "board.h"

// Match detection here is a behavioural reconstruction, not ported code - see
// the provenance warning at the top of game.cpp. The wildcard rule in
// particular was wrong in two different ways before a player described what
// the game actually does.

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

    // Scan once per colour. A wildcard matches wherever a match is possible, so
    // asking "what run is this cell in" is the wrong question - the same
    // Flashium can complete a run of Redium and a run of Greenium at the same
    // time, and both clear. Asking instead, for each colour, "where are the
    // runs of that colour" gives each one independently and needs no tie-break.
    //
    // A single greedy pass got this wrong: walking `1 8 2 2` left to right let
    // the 1 claim the 8, leaving 1-8 and 2-2 and clearing nothing, when 8-2-2
    // is plainly a run of three.
    auto scan = [&](int8_t want, bool wildcardsOnly) {
        for (const Dir& d : kDirs) {
            for (int r = 0; r < rows_; ++r) {
                for (int c = 0; c < cols_; ++c) {
                    // Start only from a cell that begins a line in this
                    // direction, then cut that line into maximal runs.
                    if (inBounds(c - d.dc, r - d.dr)) continue;

                    int cc = c, rr = r;
                    while (inBounds(cc, rr)) {
                        const int sc = cc, sr = rr;
                        int len = 0;
                        bool hasElement = false;
                        while (inBounds(cc, rr)) {
                            const int8_t v = at(cc, rr);
                            const bool ok = wildcardsOnly
                                                ? isWildcard(v)
                                                : (isWildcard(v) || v == want);
                            if (!ok) break;
                            if (!isWildcard(v)) hasElement = true;
                            ++len;
                            cc += d.dc;
                            rr += d.dr;
                        }

                        // A colour's run needs at least one atom of that
                        // colour, or every colour would claim a row of
                        // wildcards. Those are handled by the wildcard pass.
                        if (len >= kMinRun && (wildcardsOnly || hasElement)) {
                            bool fresh = false;
                            for (int i = 0; i < len; ++i) {
                                const size_t k =
                                    static_cast<size_t>(sr + d.dr * i) * cols_ +
                                    (sc + d.dc * i);
                                if (!marked[k]) fresh = true;
                            }
                            // A pure-wildcard run only counts where no colour
                            // run already took those cells, so `8 8 8 2` clears
                            // once as Greenium rather than twice.
                            if (!wildcardsOnly || fresh) {
                                if (runs) runs->push_back(Run{d.kind, len});
                                for (int i = 0; i < len; ++i) {
                                    marked[static_cast<size_t>(sr + d.dr * i) *
                                               cols_ +
                                           (sc + d.dc * i)] = 1;
                                }
                            }
                        }
                        if (len == 0) {      // step over the blocking cell
                            cc += d.dc;
                            rr += d.dr;
                        }
                    }
                }
            }
        }
    };

    for (int8_t want = kFirstColour; want <= kLastColour; ++want) {
        scan(want, false);
    }
    scan(kEmpty, true);      // three Flashium match on their own

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
