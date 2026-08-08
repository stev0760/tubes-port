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
// That is not an optimisation: it is why a run of four pays twice, because two
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

void Board::setMarked(int c, int r, bool on) {
    if (!inBounds(c, r)) return;
    marked_[idx(c, r)] = on ? 1 : 0;
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
// Two things here are worth not smoothing over. The wildcard adopts: a
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

    // Count this as a distinct run only when the cell one step back does not
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

    // `1000:1c3b`, on the same path as the award below and with the same
    // per-seed frequency. The type passed is what the run resolved to after
    // any wildcard adoption, which is why an all-Flashium run arrives as 8.
    if (onRun_) onRun_(kind, static_cast<int8_t>(matchType));

    out.award += awardFor(kind);
    out.soundType = static_cast<int8_t>(matchType);   // the family sound follows what it matched as
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

            // `1000:24db`..`2533`. Wave mode 6 only: a clearing cell that also
            // carries an objective marker consumes the marker and ticks the
            // wave's counter down.
            //
            // The order is the original's and it matters at the end of a wave:
            // the marker is cleared unconditionally at `1000:251f`, and only
            // then is the counter tested (`CMP ...,0` / `JBE`) and decremented
            // at `2533`. So a marker cleared while the counter already reads
            // zero still disappears - it just cannot take the count below it.
            if (objectiveMode_ && objective_[idx(c, r)]) {
                objective_[idx(c, r)] = 0;
                if (onObjectiveCleared_) onObjectiveCleared_();
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

// `1000:25b3`. Each atom falls at most one row per frame, which is why the
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

            // `1000:2729`: `cell mod 19 = 18` is a Crystal in any fade frame.
            if (onCrystalFell_ && v % kFadeStride == kCrystal) {
                onCrystalFell_(c, r, dst);
            }
        }
    }
}

// `1000:0c82`. The original scans the 30-byte plane for a byte equal to `want`
// and turns the index back into (row, col) by dividing by 6, so this finds the
// first such cell in row-major order and only ever handles one per frame.
bool Board::findCell(int8_t want, int& c, int& r) const {
    for (int rr = 0; rr < rows_; ++rr) {
        for (int cc = 0; cc < cols_; ++cc) {
            if (cells_[idx(cc, rr)] == static_cast<Cell>(want)) {
                c = cc;
                r = rr;
                return true;
            }
        }
    }
    return false;
}

// `1000:0e08`. AntiMatter destroys the 3x3 block centred on itself, clipped at
// the edges - the original narrows the span rather than clamping both ends:
//
//     Dec(col); Dec(row);
//     colSpan := 3;  rowSpan := 3;
//     if (col < 1) or (col >= 5) then Dec(colSpan);
//     if (row < 1) or (row >= 4) then Dec(rowSpan);
//     if col < 1 then col := 1;   if row < 1 then row := 1;
//
// Every non-empty cell in the block is rewritten to type 9 and marked. That is
// the whole trick: because the cell becomes AntiMatter's own type, the single
// sprite-table lookup draws `AFADE` over all of them, so the blast animation
// needs no special case anywhere in the renderer. It is also why `AFADE` is a
// fade family without being a "can be cleared" marker - the notes had guessed
// that; this is the code doing it.
//
// The AntiMatter cell is the centre of its own block, so it destroys itself.
void Board::applyAntiMatter(BoardStep& out) {
    int c = 0, r = 0;
    if (!findCell(kAntiMatter, c, r)) return;

    int left = c - 1, top = r - 1;
    int colSpan = 3, rowSpan = 3;
    if (left < 0 || left >= cols_ - 2) --colSpan;
    if (top < 0 || top >= rows_ - 2) --rowSpan;
    if (left < 0) left = 0;
    if (top < 0) top = 0;

    for (int rr = top; rr < top + rowSpan && rr < rows_; ++rr) {
        for (int cc = left; cc < left + colSpan && cc < cols_; ++cc) {
            const Cell was = cells_[idx(cc, rr)];
            if (was == 0) continue;
            cells_[idx(cc, rr)] = kAntiMatter;
            marked_[idx(cc, rr)] = 1;
            // `1000:0f24`, and the original's own `mod 18` test.
            if (onCrystalBlasted_ && was % 18 == 0) onCrystalBlasted_(cc, rr);
        }
    }
    out.soundType = kAntiMatter;
    out.blast = true;
}

// `1000:0ce7`. The Blocker turns itself and every cell above it in its column
// into Xenon - "fills the beaker column it lands in with Xenons", now from
// code rather than from a published description.
void Board::applyBlocker() {
    int c = 0, r = 0;
    if (!findCell(kBlocker, c, r)) return;
    for (int rr = r; rr >= 0; --rr) cells_[idx(c, rr)] = kXenon;
}

// `1000:0d56`. The Convertor becomes Xenon, then looks at the one cell
// directly below it and turns every atom of that type anywhere on the board
// into Xenon.
//
// Note that this is stronger than the published description, which says it "turns
// the atoms it lands on into Xenons". The code converts board-wide, by type.
// The victim must be an ordinary type - `> 0` and `< 11` - so it will not
// chain off a Xenon or another special.
void Board::applyConvertor() {
    int c = 0, r = 0;
    if (!findCell(kConvertor, c, r)) return;
    cells_[idx(c, r)] = kXenon;
    if (r + 1 >= rows_) return;

    const Cell victim = cells_[idx(c, r + 1)];
    if (victim == 0 || victim >= kXenon) return;
    int vc = 0, vr = 0;
    while (findCell(static_cast<int8_t>(victim), vc, vr)) {
        cells_[idx(vc, vr)] = kXenon;
    }
}

// `1000:2790`, the tail of the beaker update. Everything here acts on atoms
// that have settled, so the trigger is simply "a cell of this type exists".
void Board::specialsPass(BoardStep& out) {
    applyAntiMatter(out);

    // Types that have no business sitting in the beaker become inert. Their
    // real effects happen when the test tube catches them - the router
    // dispatches Bonus, Multiplier, EvilMultiplier and Filler at `1000:180c` -
    // so anything of these types that reaches the glass is a leftover.
    for (int8_t t : {kBonus, kMultiplier, kEvilMultiplier, kFiller}) {
        int c = 0, r = 0;
        while (findCell(t, c, r)) cells_[idx(c, r)] = kXenon;
    }

    if (specialsEnabled_) applyBlocker();
    applyConvertor();
}

BoardStep Board::step() {
    BoardStep out;
    matchPass(out);
    fadePass();
    gravityPass(out);
    specialsPass(out);
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
