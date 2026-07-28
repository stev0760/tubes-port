#include "board.h"

// Transliterated from `1000:22a6` and the four matchers it calls. Read the
// header first for the three-plane layout.
//
// The previous version of this file was a behavioural reconstruction, and the
// difference is not cosmetic. It removed matched cells immediately, so no
// clear animation was possible; it compacted a column fully in one step, where
// the original moves each atom one row per frame; and its wildcard handling
// was rewritten twice from play reports. All three are settled here by code.

#include <algorithm>

namespace tubes {
namespace {

// Each matcher tests exactly two cells beyond the seed - a run of three - and
// longer runs fall out of the overlapping seeds rather than being scanned.
// That is not an optimisation: it is why a run of four pays TWICE, because two
// seed positions each add the orientation's award. "4 atom molecules count as
// 2 chains" in the Instructions is literally the number of seeds.
constexpr int kRunLength = 3;

int awardFor(RunKind k) {
    switch (k) {
        case RunKind::kVertical:   return 250;    // 1000:1c56  ADD ...,0xfa
        case RunKind::kHorizontal: return 500;    // 1000:1e47  ADD ...,0x1f4
        case RunKind::kDiagonal:   return 1000;   // 1000:2052  ADD ...,0x3e8
    }
    return 250;
}

}  // namespace

Board::Board(int cols, int rows)
    : cols_(cols),
      rows_(rows),
      cells_(static_cast<size_t>(cols) * rows, 0),
      marked_(static_cast<size_t>(cols) * rows, 0),
      objective_(static_cast<size_t>(cols) * rows, 0) {}

bool Board::inBounds(int c, int r) const {
    return c >= 0 && c < cols_ && r >= 0 && r < rows_;
}

Cell Board::at(int c, int r) const {
    if (!inBounds(c, r)) return kEmpty;
    return cells_[idx(c, r)];
}

int8_t Board::typeAt(int c, int r) const { return cellType(at(c, r)); }

bool Board::isMarked(int c, int r) const {
    return inBounds(c, r) && marked_[idx(c, r)] != 0;
}

bool Board::isObjective(int c, int r) const {
    return inBounds(c, r) && objective_[idx(c, r)] != 0;
}

void Board::set(int c, int r, Cell raw) {
    if (!inBounds(c, r)) return;
    cells_[idx(c, r)] = raw;
}

void Board::setObjective(int c, int r, bool on) {
    if (!inBounds(c, r)) return;
    objective_[idx(c, r)] = on ? 1 : 0;
}

void Board::clear() {
    std::fill(cells_.begin(), cells_.end(), Cell{0});
    std::fill(marked_.begin(), marked_.end(), 0);
    std::fill(objective_.begin(), objective_.end(), 0);
}

int Board::dropRow(int c) const {
    for (int r = rows_ - 1; r >= 0; --r) {
        if (at(c, r) == kEmpty) return r;
    }
    return -1;
}

bool Board::drop(int c, int8_t type) {
    int r = dropRow(c);
    if (r < 0) return false;
    set(c, r, type);
    return true;
}

// One seed of one matcher, from `1000:1aae`. The other three differ only in
// (dc, dr) and in the award; the body below is theirs line for line.
//
//     matchType := cells[r, c]
//     if (matchType < 1) or (matchType > 8) then exit
//     if matchType = disabled then exit
//     for i := 1 to 2 do
//         other := cells[r + i*dr, c + i*dc]
//         if other > 8 then exit
//         if (matchType = 8) and (other <> 0) then matchType := other
//         if matchType = disabled then exit
//         if (matchType <> other) and (other <> 8) then exit
//     for i := 0 to 2 do marked[r + i*dr, c + i*dc] := 1
//
// Two things here are worth not smoothing over. The wildcard ADOPTS: a
// Flashium seed becomes whatever the next non-empty cell is, and from then on
// the run is that colour - which is how one Flashium can complete runs of two
// different colours in two different directions. And the bail-out on `other >
// 8` is what stops a run crossing a cell that is already fading, because a
// fading cell holds `type + 19*frame` and is therefore above 8.
bool Board::matchAt(int c, int r, int dc, int dr, RunKind kind, BoardStep& out) {
    Cell matchType = at(c, r);
    if (!canSeedRun(matchType)) return false;
    if (matchType == disabledType_ && disabledType_ != kEmpty) return false;

    for (int i = 1; i < kRunLength; ++i) {
        const int cc = c + dc * i, rr = r + dr * i;
        if (!inBounds(cc, rr)) return false;
        const Cell other = at(cc, rr);
        if (other > kFlashium) return false;
        if (matchType == kFlashium && other != kEmpty) matchType = other;
        if (matchType == disabledType_ && disabledType_ != kEmpty) return false;
        if (matchType != other && other != kFlashium) return false;
    }

    for (int i = 0; i < kRunLength; ++i) {
        marked_[idx(c + dc * i, r + dr * i)] = 1;
    }

    // Count this as a DISTINCT run only when the cell one step back does not
    // continue it, so a run of four counts once however many seeds it has.
    // That count is the chain bonus multiplier, not the award.
    const int pc = c - dc, pr = r - dr;
    bool distinct = true;
    if (inBounds(pc, pr)) {
        const Cell before = at(pc, pr);
        distinct = (matchType != before && before != kFlashium);
    }
    if (distinct) {
        ++out.runs;
        switch (kind) {
            case RunKind::kVertical:   ++out.chainsVertical; break;
            case RunKind::kHorizontal: ++out.chainsHorizontal; break;
            case RunKind::kDiagonal:   ++out.chainsDiagonal; break;
        }
    }

    out.award += awardFor(kind);
    out.soundType = static_cast<int8_t>(matchType);   // the family sound follows what it matched AS
    return true;
}

// The four scans, with the original's own bounds. Those bounds are the whole
// reason each matcher exists separately: they are exactly the positions where
// a run of three fits, so no matcher ever tests an out-of-range cell.
//
//     1000:1e90   rows 1..3, cols 1..4   diagonal down-right
//     1000:209b   rows 1..3, cols 3..6   diagonal down-left
//     1000:1c9f   rows 1..5, cols 1..4   horizontal
//     1000:1aae   rows 1..3, cols 1..6   vertical
//
// and they run in that order, which matters only for which family's sound
// ends up playing.
void Board::matchPass(BoardStep& out) {
    for (int r = 0; r + 2 < rows_; ++r) {
        for (int c = 0; c + 2 < cols_; ++c) matchAt(c, r, 1, 1, RunKind::kDiagonal, out);
    }
    for (int r = 0; r + 2 < rows_; ++r) {
        for (int c = 2; c < cols_; ++c) matchAt(c, r, -1, 1, RunKind::kDiagonal, out);
    }
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c + 2 < cols_; ++c) matchAt(c, r, 1, 0, RunKind::kHorizontal, out);
    }
    for (int r = 0; r + 2 < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) matchAt(c, r, 0, 1, RunKind::kVertical, out);
    }
}

// `1000:2538`. A marked cell climbs by one fade frame a tick and empties once
// it passes 152.
void Board::fadePass() {
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            if (!marked_[idx(c, r)]) continue;

            // Wave mode 6: clearing a marked objective cell ticks the wave's
            // target down and consumes the marker.
            if (objectiveMode_ && objective_[idx(c, r)]) {
                objective_[idx(c, r)] = 0;
                ++objectivesCleared_;
            }

            int v = cells_[idx(c, r)] + kFadeStride;
            if (v > kCellClearAbove) {
                v = 0;
                marked_[idx(c, r)] = 0;
            }
            cells_[idx(c, r)] = static_cast<Cell>(v);
        }
    }
}

// `1000:25b3`. Each atom falls at most ONE row per frame, which is why the
// beaker visibly settles instead of snapping. The destination cursor and the
// source row walk down together, so the destination is always the row below
// the source; scanning bottom-up lets a whole column shift in one pass.
//
// All three planes move together - `marked` and `objective` travel with the
// atom, so a cell that is mid-fade keeps fading as it falls.
void Board::gravityPass(BoardStep& out) {
    for (int c = 0; c < cols_; ++c) {
        for (int r = rows_ - 2; r >= 0; --r) {
            const int dst = r + 1;
            if (cells_[idx(c, dst)] != 0) continue;
            const Cell v = cells_[idx(c, r)];
            if (v != 0) out.settled = true;

            cells_[idx(c, dst)] = v;
            cells_[idx(c, r)] = 0;
            marked_[idx(c, dst)] = marked_[idx(c, r)];
            marked_[idx(c, r)] = 0;
            objective_[idx(c, dst)] = objective_[idx(c, r)];
            objective_[idx(c, r)] = 0;
        }
    }
}

BoardStep Board::step() {
    BoardStep out;
    matchPass(out);
    fadePass();
    gravityPass(out);
    return out;
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
                      [](Cell v) { return v != 0; }));
}

}  // namespace tubes
