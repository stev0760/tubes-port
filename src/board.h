// The beaker: THREE parallel 30-byte planes, exactly as `1000:3a67` holds it.
//
// Transliterated from `1000:22a6` (the per-frame beaker update) and the four
// matchers it calls - `1000:1aae`, `1c9f`, `1e90` and `209b`. The previous
// version of this file was a behavioural reconstruction and said so; it is
// replaced rather than extended.
//
// The three planes live in 3a67's own frame, contiguous, 30 bytes apart:
//
//     [BP-0x25] + row*6 + col    cells      `type + 19 * fadeFrame`
//     [BP-0x43] + row*6 + col    marked     1 while the cell is clearing
//     [BP-0x61] + row*6 + col    objective  1 = a wave target, drawn MARKER
//
// with `row` 1..5 and `col` 1..6 - Turbo Pascal's `array[1..5, 1..6] of byte`,
// whose +7 bias on the base is the fingerprint that first identified it.

#pragma once

#include <cstdint>
#include <vector>

namespace tubes {

// Atom types, numbered exactly as the original numbers them. This is not an
// arbitrary internal encoding: a settled beaker cell holds the atom record's
// type byte (+0x0b) directly, so these values are what the original's own grid
// contains. Keeping them identical means a trace captured from the original
// can be compared against this engine's board without a translation table.
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
    kObstacle = 17,
    kCrystal = 18,
    // MYSTBALL is a RENDERING STATE, not a ball. 1000:3a67 draws it in place
    // of the real atom when a hidden-atom wave is active:
    //     if (hidden == 0) Draw(ball[type], x, y); else Draw(MYSTBALL, x, y);
    // so an atom keeps its true type underneath and is merely concealed.
    kMystery = 19,
    kTypeCount = 20,        // 0..19 inclusive
};

// A cell does not hold a type - it holds `type + 19 * fadeFrame`, so ONE table
// lookup draws a settled atom and a fading one alike and the drawing code
// never branches. The original's own sprite table is indexed by exactly this
// value: `ball[cell]` at `DS:0x1da6 + 4 * cell`.
//
// `1000:2729` extracts the type with `cell mod 19` when it needs one, which is
// where the stride is confirmed rather than inferred.
constexpr int kFadeStride = 19;

// A cell is a Pascal `byte`, and it must be unsigned here too: the composite
// reaches 152 at the end of a fade, which overflows a signed char. Storing it
// in int8_t compiled and passed every match test, and only the fade-frame
// assertion caught it - a reminder that "it builds and the game looks right"
// is not a type check.
using Cell = uint8_t;

// Fade sprites exist for frames 1..6 - the loop in `1000:9e53` that loads them
// ends on `CMP byte ptr [BP-0x2], 0x6`. Frames past that have null table
// entries and draw nothing, which is not a bug: see kCellClearAbove.
constexpr int kFadeFrames = 6;

// A marked cell gains 19 a frame and is emptied once it passes 152 - `ADD
// [BP-3], 0x13` then `CMP [BP-3], 0x98` in `1000:255a`. 152 is 8 * 19, so a
// cell runs through eight fade steps: six with sprites, then two invisible
// ones before the cell empties and gravity takes the column.
constexpr int kCellClearAbove = 152;

inline int8_t cellType(Cell raw) {
    return static_cast<int8_t>(raw % kFadeStride);
}
inline int cellFadeFrame(Cell raw) { return raw / kFadeStride; }

// Types 1..7 are the ordinary colours. Everything from 8 up is a special.
constexpr int8_t kFirstColour = kRedium;
constexpr int8_t kLastColour = kPinkium;
constexpr int kColourCount = 7;

// What can SEED a run, straight from `1000:1ae1`: `if (cell < 1) or (cell > 8)
// then exit`. Note this is applied to the raw cell, so a cell already fading
// has a value above 19 and cannot start or join a run - the encoding does that
// work on its own, with no separate "is clearing" test anywhere in the matcher.
inline bool canSeedRun(Cell raw) {
    return raw >= kRedium && raw <= kFlashium;
}

inline bool isWildcard(int8_t v) { return v == kFlashium; }

// The measured cell-to-pixel mapping. The column pitch is 18, not the 16 the
// sprite width would suggest.
constexpr int kColumnX[] = {107, 125, 143, 161, 179, 197};
constexpr int kRowY0 = 121;
constexpr int kRowPitchY = 13;

inline int playColumnX(int col) {
    if (col < 0) col = 0;
    if (col > 5) col = 5;
    return kColumnX[col];
}

// The orientation code the matchers pass to the wave-objective checker at
// `1000:192f`, and the award each one adds. Both are literals in the four
// matchers, so this table is transcribed, not derived.
//
//     1000:1aae  vertical     code 2   250    counter A
//     1000:1c9f  horizontal   code 1   500    counter B
//     1000:1e90  diagonal ->  code 0  1000    counter C
//     1000:209b  diagonal <-  code 0  1000    counter C
//
// Both diagonals share a counter, which is what the Instructions mean by
// "Diagonal (either direction)".
enum class RunKind : uint8_t { kHorizontal, kVertical, kDiagonal };

// What one frame of the beaker update did, for the caller to score and sound.
struct BoardStep {
    int award = 0;          // sum of the per-seed awards this frame
    int runs = 0;           // distinct runs; the original's chain multiplier
    int chainsVertical = 0;
    int chainsHorizontal = 0;
    int chainsDiagonal = 0;
    int8_t soundType = kEmpty;   // the family whose sound to play, 0 for none
    bool settled = false;        // an atom moved down; the original plays a sound
    bool blast = false;          // AntiMatter went off; it also arms the clear timer
};

class Board {
public:
    Board(int cols, int rows);

    int cols() const { return cols_; }
    int rows() const { return rows_; }

    bool inBounds(int c, int r) const;

    // The raw composite, which is what the renderer wants - it indexes the
    // sprite table directly.
    Cell at(int c, int r) const;
    // The type alone, 0 when empty.
    int8_t typeAt(int c, int r) const;
    bool isMarked(int c, int r) const;
    bool isObjective(int c, int r) const;

    void set(int c, int r, Cell raw);
    void setObjective(int c, int r, bool on);

    void clear();

    // Lowest free row in a column, or -1 when the column is full.
    int dropRow(int c) const;

    // Places an atom at the bottom of a column. Returns false if full.
    bool drop(int c, int8_t type);

    // One frame of `1000:22a6`: match, advance the fades, then settle. This is
    // the whole beaker update and it runs every frame, not only when something
    // changes - which is what makes clearing and falling animate at all.
    BoardStep step();

    // True when any column has reached the top.
    bool overflowing() const;

    // Settled atoms only; a cell mid-fade still counts until it empties.
    int count() const;

    // A wave modifier: an element that still spawns but cannot be cleared.
    // `1000:1afe` compares every candidate type against it and bails out.
    void setDisabledType(int8_t t) { disabledType_ = t; }

    // Wave mode 6 gates the objective plane; outside it the plane is inert.
    void setObjectiveMode(bool on) { objectiveMode_ = on; }
    int objectivesCleared() const { return objectivesCleared_; }

    // `DS:0x1d48` gates the Blocker here and the Multiplier, EvilMultiplier
    // and Filler at catch time in `1000:0f80`. The Bonus atom is NOT gated by
    // it. Nothing in `1000:9e53` writes it, so it is set further out - the
    // wave or mode setup - and the port defaults it on.
    void setSpecialsEnabled(bool on) { specialsEnabled_ = on; }
    bool specialsEnabled() const { return specialsEnabled_; }

    // The first cell holding exactly `want`, in row-major order - `1000:0c82`,
    // which scans the 30-byte plane with a byte search and divides the index
    // by 6. Matching is on the RAW cell, so a fading atom is never found.
    bool findCell(int8_t want, int& c, int& r) const;

private:
    size_t idx(int c, int r) const {
        return static_cast<size_t>(r) * cols_ + c;
    }
    // One seed position of one matcher. `dc`/`dr` step along the run.
    bool matchAt(int c, int r, int dc, int dr, RunKind kind, BoardStep& out);
    void matchPass(BoardStep& out);
    void fadePass();
    void gravityPass(BoardStep& out);
    // `1000:2790`: the specials, applied to atoms that have SETTLED in the
    // beaker. Each one scans for its type and acts on the first it finds.
    void specialsPass(BoardStep& out);
    void applyAntiMatter(BoardStep& out);
    void applyBlocker();
    void applyConvertor();

    int cols_;
    int rows_;
    std::vector<Cell> cells_;         // plane A: type + 19 * fadeFrame
    std::vector<uint8_t> marked_;     // plane B
    std::vector<uint8_t> objective_;  // plane C

    int8_t disabledType_ = kEmpty;
    bool objectiveMode_ = false;
    bool specialsEnabled_ = true;
    int objectivesCleared_ = 0;
};

}  // namespace tubes
