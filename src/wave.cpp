#include "wave.h"

// Transliterated from `1000:86b8` and the twenty-five objective routines it
// dispatches to. Read wave.h first for what each address is.
//
// PROVENANCE. All of it is decompiled: the 75-arm table is `86b8`'s body, the
// per-routine field writes are those routines, the six seeds are immediates at
// `1000:a4cd`, the progression is `1000:a616`, and `creditRun` is `1000:192f`
// line for line. Nothing here was inferred from a briefing screenshot. The
// reading still reproduces all nine that the old level-warp sweep captured,
// counts included. That is how we checked it.
//
// What is deliberately not here: the drawing. `86b8` also picks a background
// with a `Random(10)` retry loop, and several routines roll once more for a
// decorative sprite. This port marks those rolls at each site, because a
// wave-mode recording cannot replay without them.

namespace tubes {
namespace {

using O = Objective;

// `1000:86b8`, arm by arm. This is data in the binary and is transcribed, not
// generated - waves 10 and 15 really do share an objective, which the sweep
// that sampled them had to treat as coincidence.
constexpr Objective kWaveTable[kWaveCount] = {
    O::kAnyAtom, O::kSurvive, O::kVerticalColour, O::kAnyAtomMorph, O::kVerticalAny,                  // 1..5
    O::kSurviveDisabled, O::kShownAtom, O::kMarked, O::kAnyAtom, O::kVerticalColour,                  // 6..10
    O::kAnyAtomMorph, O::kShownAtom, O::kHorizontalAny, O::kTaskChain, O::kVerticalColour,            // 11..15
    O::kMarked, O::kShownAtom, O::kDiagonalAny, O::kAnyAtomPrefill, O::kMarked,                       // 16..20
    O::kDiagonalColour, O::kHorizontalAny, O::kAnyAtomMorph, O::kSurvive, O::kTaskColour,             // 21..25
    O::kSurvive, O::kDiagonalColour, O::kMarkedCovered, O::kTaskChain, O::kSurviveHidden,             // 26..30
    O::kFlashium, O::kTaskColourTimed, O::kAnyAtomPrefill, O::kVerticalAny, O::kTaskBothTimed,        // 31..35
    O::kSurvive, O::kHorizontalColour, O::kMarkedCovered, O::kTaskChain, O::kHorizontalColour,        // 36..40
    O::kMarkedXenon, O::kDiagonalColour, O::kSurviveDisabled, O::kTaskBoth, O::kDiagonalAny,          // 41..45
    O::kMystery, O::kAnyAtomPrefill, O::kFlashium, O::kHorizontalColour, O::kCrystals,                // 46..50
    O::kMarkedXenon, O::kTaskChainTimed, O::kSurviveDisabled, O::kHorizontalAny, O::kMystery,         // 51..55
    O::kTaskBothTimed, O::kDiagonalAny, O::kMarkedXenon, O::kVerticalAny, O::kTaskChainTimed,         // 56..60
    O::kFlashium, O::kSurviveHidden, O::kTaskBothTimed, O::kMarkedCovered, O::kTaskBoth,              // 61..65
    O::kTaskColourTimed, O::kAnyAtom, O::kTaskColour, O::kCrystals, O::kTaskColourTimed,              // 66..70
    O::kMystery, O::kTaskBoth, O::kCrystals, O::kTaskChainTimed, O::kSurviveHidden,                   // 71..75
};

// The shareware Preview's wave list - the `else` side of `1000:7fc7`'s
// dispatch, five arms against the normal twenty-five. Each shareware arm is
// identified by the registered routine carrying the same briefing, so these are
// addresses rather than descriptions:
//
//     Preview 1   sw 1000:6591  ->  reg 1000:66cb   Mischief Crystals
//     Preview 2   sw 1000:6458  ->  reg 1000:6592   marked atoms in Xenon rings
//     Preview 3   sw 1000:6c32  ->  reg 1000:6fd5   hidden atoms
//     Preview 4   sw 1000:729e  ->  reg 1000:7b72   colour AND chain, timed
//     Preview 5   sw 1000:7cad  ->  reg 1000:8581   Mystery Wave
//
// Arm 4 is the timed variant, not `kTaskBoth`: its briefing says the
// requirement "will change every 45 seconds" where the untimed one says "will
// change after completing each task".
//
// None of these five appears anywhere in the shareware's own 25-arm chain -
// checked per function, zero hits each - so they are registered-only waves
// carried in the shareware binary and reachable only through the Preview. That
// is what "play some of the new waves" means, and waves 1 and 2 needing
// AntiMatter is why the Preview turns the special-atom rates back on.
constexpr Objective kPreviewTable[kPreviewWaveCount] = {
    O::kCrystals, O::kMarkedXenon, O::kSurviveHidden, O::kTaskBothTimed, O::kMystery,
};

// `repeat colour := Random(8) + 1 until colour <= 7`, which is how six of the
// routines pick a colour. The rejection loop is the original's - it rolls over
// eight and throws Flashium away rather than rolling over seven.
int8_t rollColour(const RollFn& roll) {
    int8_t c;
    do {
        c = static_cast<int8_t>(roll(8) + 1);
    } while (c > 7);
    return c;
}

// `Random(6) + 2`, the other form. The mode 2 routines use it for the balls
// they illustrate the briefing with, and the Task-Display-chain ones use it as
// the colour the wave actually requires.
int8_t rollColour2to7(const RollFn& roll) {
    return static_cast<int8_t>(roll(6) + 2);
}

// The idiom the three setup routines share. `1000:3a67` holds the beaker as
// `array[1..5, 1..6]`, so `cells[1, col]` - the top of a column - is one test,
// and a full column is exactly that cell being occupied.
int columnWithRoom(const Board& b, int col, const RollFn& roll) {
    // The original spins here forever if every column is full. That cannot
    // happen: the most any wave places is `marked + 8` and the beaker holds 30.
    // The bound is this port's, so a bug upstream shows as a missing atom
    // rather than a hung frame.
    for (int guard = 0; guard < 1000 && b.at(col, 0) != kEmpty; ++guard) {
        col = roll(b.cols());
    }
    return col;
}

// `while (row <> 5) and (cells[row+1, col] = 0) do Inc(row)` - fall to rest.
int restRow(const Board& b, int col, int row) {
    while (row != b.rows() - 1 && b.at(col, row + 1) == kEmpty) ++row;
    return row;
}

}  // namespace

void placeMarkedAtoms(Board& board, int n, bool covered, bool xenon,
                      const RollFn& roll) {
    int col = 0;
    for (; n != 0; --n) {
        int row = 0;
        for (int guard = 0; guard < 1000; ++guard) {
            col = roll(board.cols());
            row = roll(board.rows());
            if (board.at(col, row) == kEmpty) break;
        }
        row = restRow(board, col, row);
        // Random(8) + 1 - the marked atom may be a Flashium.
        board.set(col, row, static_cast<Cell>(roll(8) + 1));
        board.setObjective(col, row, true);
    }

    // One counter for both loops, and it is seeded once.
    int k = 8;
    if (covered) {
        while (k != 0) {
            col = columnWithRoom(board, col, roll);
            board.set(col, restRow(board, col, 0),
                      static_cast<Cell>(k % 7 + 1));
            --k;
            if (++col >= board.cols()) col = 0;
        }
    }
    if (xenon) {
        while (k != 0) {
            col = columnWithRoom(board, col, roll);
            board.set(col, restRow(board, col, 0),
                      static_cast<Cell>(kXenon));
            --k;
            if (++col >= board.cols()) col = 0;
        }
    }
}

void seedPreFilledBeaker(Board& board, int n, const RollFn& roll) {
    int col = roll(board.cols());
    while (n != 0) {
        col = columnWithRoom(board, col, roll);
        board.set(col, restRow(board, col, 0), static_cast<Cell>(n % 7 + 1));
        --n;
        if (++col >= board.cols()) col = 0;
    }
}

void rotateBeakerColours(Board& board) {
    for (int r = 0; r < board.rows(); ++r) {
        for (int c = 0; c < board.cols(); ++c) {
            const Cell v = board.at(c, r);
            if (v == kEmpty || v >= kFlashium) continue;
            if (board.isMarked(c, r)) continue;
            board.set(c, r, v + 1 == kFlashium ? static_cast<Cell>(kRedium)
                                               : static_cast<Cell>(v + 1));
        }
    }
}

void placeCrystals(Board& board, std::vector<Crystal>& crystals, int n,
                   int interval, const RollFn& roll) {
    crystals.clear();
    if (n <= 0) return;
    crystals.resize(static_cast<size_t>(n));
    for (int i = 1; i <= n; ++i) {
        Crystal& x = crystals[static_cast<size_t>(i - 1)];
        x.active = true;
        x.departing = false;
        x.step = 0;
        for (int guard = 0; guard < 1000; ++guard) {
            x.col = roll(board.cols());
            x.row = roll(board.rows());
            if (board.at(x.col, x.row) == kEmpty) break;
        }
        x.row = restRow(board, x.col, x.row);
        board.set(x.col, x.row, static_cast<Cell>(kCrystal));
        x.arriving = false;
        // Staggered, so n crystals never jump on the same frame.
        x.timer = interval * 10 * i / n;
    }
}

bool stepCrystals(Board& board, std::vector<Crystal>& crystals, int interval,
                  const RollFn& roll) {
    bool sound = false;
    for (Crystal& x : crystals) {
        if (!x.active) continue;

        // Arriving: walk the fade value backwards one frame per step until it
        // is the static sprite again.
        if (x.arriving) {
            const Cell v = board.at(x.col, x.row);
            if (v == static_cast<Cell>(kCrystal)) {
                x.arriving = false;
            } else {
                board.set(x.col, x.row, static_cast<Cell>(v - kFadeStride));
            }
        }

        // Departing: the ordinary fade pass is animating the cell out. When the
        // seven steps are up, land at the destination on the last fade frame
        // and start walking back.
        if (x.departing && --x.step == 0) {
            x.col = x.destCol;
            x.row = x.destRow;
            board.set(x.col, x.row,
                      static_cast<Cell>(kCrystal + kFadeStride * 7));
            board.setMarked(x.col, x.row, false);
            x.departing = false;
            x.arriving = true;
            sound = true;
        }

        if (x.timer != 0) {
            --x.timer;
            continue;
        }

        // The clock. Pick somewhere to go, preferring a cell that holds an
        // ordinary atom - landing there overwrites it, which is what
        // "contaminating the beaker" means mechanically.
        x.timer = interval * 10;
        int tries = 10;
        do {
            x.destCol = roll(board.cols());
            int r = 0;
            while (r < board.rows() - 1 && board.at(x.destCol, r) == kEmpty) ++r;
            x.destRow = r + roll(board.rows() - 1 - r);
            const Cell v = board.at(x.destCol, x.destRow);
            if (v != kEmpty && v < kXenon) tries = 1;
            --tries;
        } while (tries != 0);

        board.setMarked(x.col, x.row, true);   // the fade pass takes it from here
        x.departing = true;
        x.step = 7;
        sound = true;
    }
    return sound;
}

void removeCrystalAt(std::vector<Crystal>& crystals, WaveObjective& obj,
                     int col, int row) {
    for (Crystal& x : crystals) {
        if (!x.active || x.col != col || x.row != row) continue;
        if (obj.counter != 0) --obj.counter;
        x.active = false;
    }
}

void crystalCellFell(std::vector<Crystal>& crystals, int col, int fromRow,
                     int toRow) {
    for (Crystal& x : crystals) {
        if (x.col == col && x.row == fromRow) x.row = toRow;
    }
}

Objective objectiveForWave(int wave) {
    if (wave < 1 || wave > kWaveCount) return O::kAnyAtom;
    return kWaveTable[wave - 1];
}

// The Preview's own dispatch. Same shape as the one above, and the same
// out-of-range behaviour, because it is the same `if`-chain in the original -
// one `if/else` on `DS:0x1d4b` with a 25-arm side and a 5-arm side.
Objective objectiveForPreviewWave(int wave) {
    if (wave < 1 || wave > kPreviewWaveCount) return O::kAnyAtom;
    return kPreviewTable[wave - 1];
}

// `1000:7fc7` picks the arm chain by the flag, so callers do not have to.
Objective objectiveForWave(int wave, const EditionState& ed) {
    return ed.preview ? objectiveForPreviewWave(wave) : objectiveForWave(wave);
}

// `1000:a616`. Note what is not stepped: the crystal count, which `1000:66cb`
// increments for itself, and the pre-fill size, which never moves.
void WaveProgress::advance() {
    --interval;                              // a616
    if (wave % 15 == 0) {
        velocity += 0x20;                    // a62b
        interval += 12;                      // a630 - a partial refund
    }
    if (wave % 20 == 0) {
        ++chainTargetColour;                 // a646
        ++chainTargetChain;
        atomTarget += 10;
        ++marked;
    }
    ++wave;                                  // a662
}

void applyBriefing(Objective o, WaveProgress& progress, WaveObjective& obj,
                   const RollFn& roll, bool replay) {
    // `1000:86b8` clears exactly these twelve before dispatching, and leaves
    // the counter, the required colour and the required chain standing. That
    // asymmetry is the whole Continue mechanism.
    obj.morphBeaker = false;
    obj.rotateOnTimer = false;
    obj.mysteryHidden = false;
    obj.preFillBeaker = false;
    obj.anyOrientation = false;
    obj.countIsShown = false;
    obj.rotateColour = false;
    obj.rotateChain = false;
    obj.markedCovered = false;
    obj.markedXenon = false;
    obj.hiddenAtoms = false;
    obj.disabledColour = 0;

    // Mystery Wave, `1000:8581`: roll one of four, run it, then blank the Task
    // Display. The roll is not guarded by the replay flag - only the routine it
    // lands in guards its own.
    if (o == O::kMystery) {
        switch (roll(4)) {
            case 0:  o = O::kShownAtom; break;
            case 1:  o = O::kVerticalAny; break;
            case 2:  o = O::kHorizontalAny; break;
            default: o = O::kDiagonalAny; break;
        }
        applyBriefing(o, progress, obj, roll, replay);
        obj.mysteryHidden = true;
        return;
    }

    switch (o) {
        // Mode 6: marked atoms.
        case O::kMarkedCovered:                          // 1000:643b
            obj.markedCovered = true;
            [[fallthrough]];
        case O::kMarked:                                 // 1000:62f1
            obj.mode = WaveMode::kMarked;
            obj.counter = progress.marked;
            // `62f1` also rolls Random(8) for the ball it illustrates the
            // briefing with, unguarded. Kept so the stream is right once the
            // briefing is drawn.
            roll(8);
            break;
        case O::kMarkedXenon:                            // 1000:6592
            obj.markedXenon = true;
            obj.mode = WaveMode::kMarked;
            obj.counter = progress.marked;
            break;

        // Mode 5: Mischief Crystals.
        case O::kCrystals:                               // 1000:66cb
            // The count is incremented here, not by the progression, and only
            // when the wave is not being replayed. So it is "how many crystal
            // waves you have reached" - wave 50, the first, is 1.
            if (!replay) ++progress.crystals;
            obj.mode = WaveMode::kCrystals;
            obj.counter = progress.crystals;
            break;

        // Mode 4: survive N atoms.
        case O::kSurviveHidden:                          // 1000:6fd5
            obj.hiddenAtoms = true;
            [[fallthrough]];
        case O::kSurvive:                                // 1000:6ede
            obj.mode = WaveMode::kSurvive;
            obj.counter = progress.atomTarget;
            obj.countIsShown = true;
            break;
        case O::kSurviveDisabled:                        // 1000:7100
            if (!replay) obj.disabledColour = rollColour(roll);
            obj.mode = WaveMode::kSurvive;
            obj.counter = progress.atomTarget;
            obj.countIsShown = true;
            break;

        // Mode 3: a colour is required.
        case O::kFlashium:                               // 1000:67d8
            obj.anyOrientation = true;
            obj.reqColour = kFlashium;
            obj.mode = WaveMode::kColour;
            obj.counter = progress.chainTargetColour;
            obj.countIsShown = true;
            break;
        case O::kShownAtom:                              // 1000:68b7
            if (!replay) {
                obj.reqColour = rollColour(roll);
                obj.anyOrientation = true;
            }
            obj.mode = WaveMode::kColour;
            obj.counter = progress.chainTargetColour;
            obj.countIsShown = true;
            break;
        case O::kHorizontalColour:                       // 1000:69e4
        case O::kVerticalColour:                         // 1000:6b5d
        case O::kDiagonalColour:                         // 1000:6cd6
            if (!replay) {
                obj.reqColour = rollColour(roll);
                obj.reqChain = (o == O::kHorizontalColour) ? chaincode::kHorizontal
                             : (o == O::kVerticalColour)   ? chaincode::kVertical
                                                           : chaincode::kDiagonal;
            }
            obj.mode = WaveMode::kColour;
            obj.counter = progress.chainTargetColour;
            break;

        // Mode 3: the Task Display drives it.
        // The only difference between each pair is `-0x1ef`: rotate after each
        // task, or on the 45-second timer.
        case O::kTaskColourTimed:                        // 1000:7802
            obj.rotateOnTimer = true;
            [[fallthrough]];
        case O::kTaskColour:                             // 1000:72ad
            if (!replay) {
                obj.reqColour = rollColour(roll);
                obj.anyOrientation = true;
                obj.rotateColour = true;
            }
            obj.mode = WaveMode::kColour;
            obj.counter = progress.chainTargetColour;
            obj.countIsShown = true;
            break;
        case O::kTaskChainTimed:                         // 1000:798b
            obj.rotateOnTimer = true;
            [[fallthrough]];
        case O::kTaskChain:                              // 1000:744d
            if (!replay) {
                obj.reqChain = static_cast<uint8_t>(roll(3));
                obj.reqColour = rollColour2to7(roll);
                obj.rotateChain = true;
            }
            obj.mode = WaveMode::kColour;
            obj.counter = progress.chainTargetChain;
            obj.countIsShown = true;
            break;
        case O::kTaskBothTimed:                          // 1000:7b72
            obj.rotateOnTimer = true;
            [[fallthrough]];
        case O::kTaskBoth:                               // 1000:764b
            if (!replay) {
                obj.reqChain = static_cast<uint8_t>(roll(3));
                obj.reqColour = rollColour2to7(roll);
                obj.rotateColour = true;
                obj.rotateChain = true;
            }
            obj.mode = WaveMode::kColour;
            obj.counter = progress.chainTargetColour;
            obj.countIsShown = true;
            break;

        // Mode 3: any atom.
        // Three routines with one body and one modifier each. None of them
        // guards on the replay flag, because none of them rolls.
        case O::kAnyAtomPrefill:                         // 1000:7de3
        case O::kAnyAtomMorph:                           // 1000:7f42
        case O::kAnyAtom:                                // 1000:7cce
            obj.preFillBeaker = (o == O::kAnyAtomPrefill);
            obj.morphBeaker = (o == O::kAnyAtomMorph);
            obj.anyOrientation = true;
            obj.reqColour = 0;
            obj.mode = WaveMode::kColour;
            obj.counter = progress.chainTargetColour;
            obj.countIsShown = true;
            break;

        // Mode 2: an orientation is required, any colour.
        case O::kHorizontalAny:                          // 1000:8056
        case O::kVerticalAny:                            // 1000:81bb
        case O::kDiagonalAny:                            // 1000:8320
            if (!replay) {
                obj.reqChain = (o == O::kHorizontalAny) ? chaincode::kHorizontal
                             : (o == O::kVerticalAny)   ? chaincode::kVertical
                                                        : chaincode::kDiagonal;
                // The colour is picked but never required - mode 2 does not
                // look at it. It only chooses the balls in the illustration.
                obj.reqColour = rollColour2to7(roll);
            }
            obj.mode = WaveMode::kOrientation;
            obj.counter = progress.chainTargetChain;
            obj.countIsShown = true;
            break;

        case O::kMystery:
            break;   // handled above
    }
}

// `1000:3ac7`. A required colour of 0 shows Flashium, which is why a
// "form N chains using any atom" wave has a cycling ball in the corner.
TaskDisplay seedTaskDisplay(const WaveObjective& obj) {
    TaskDisplay t;
    t.colour = obj.reqColour == 0 ? static_cast<int8_t>(kFlashium) : obj.reqColour;
    t.chain = obj.reqChain;
    return t;
}

void tickTaskDisplay(const WaveObjective& obj, TaskDisplay& task,
                     int8_t flashColour) {
    // 1000:48ab. Anything that does not name one colour follows Flashium.
    if (obj.reqColour == 0 || obj.reqColour == kFlashium ||
        obj.mode == WaveMode::kOrientation || obj.mode == WaveMode::kSurvive ||
        obj.mode == WaveMode::kMarked) {
        task.colour = flashColour;
    }
    task.diagonalFlip = !task.diagonalFlip;             // 1000:48d8
    if (obj.mode == WaveMode::kColour && obj.anyOrientation) {  // 1000:48e6
        task.chain = (task.chain == chaincode::kVertical)
                         ? chaincode::kDiagonal
                         : static_cast<uint8_t>(task.chain + 1);
    }
}

bool creditRun(WaveObjective& obj, RunKind kind, int8_t matchType,
               TaskDisplay& task) {
    const uint8_t code = chainCodeOf(kind);
    bool scored = false;

    if (obj.mode == WaveMode::kOrientation) {
        if (obj.reqChain == code && obj.counter != 0) {
            --obj.counter;
            scored = true;
        }
    } else if (obj.mode == WaveMode::kColour) {
        // An all-Flashium run satisfies any colour requirement - the third arm
        // of the test, and the one that would never have been guessed.
        const bool colourOk = obj.reqColour == 0 || obj.reqColour == matchType ||
                              matchType == kFlashium;
        const bool chainOk = obj.anyOrientation || obj.reqChain == code;
        if (colourOk && chainOk && obj.counter != 0) {
            --obj.counter;
            scored = true;
        }
    }

    if (!scored) return false;

    // Both rotations are suppressed when the wave rotates on the timer
    // instead. `1000:4b73` is the other half.
    if (obj.rotateChain && !obj.rotateOnTimer) {
        obj.reqChain = (obj.reqChain == chaincode::kVertical)
                           ? chaincode::kDiagonal
                           : static_cast<uint8_t>(obj.reqChain + 1);
        task.chain = obj.reqChain;
    }
    if (obj.rotateColour && !obj.rotateOnTimer) {
        obj.reqColour = (obj.reqColour == kPinkium)
                            ? static_cast<int8_t>(kRedium)
                            : static_cast<int8_t>(obj.reqColour + 1);
        task.colour = obj.reqColour;
    }
    // The Mystery Wave reveals itself once the first task lands.
    if (obj.mysteryHidden) obj.mysteryHidden = false;
    return true;
}

bool taskTimerExpired(WaveObjective& obj, TaskDisplay& task) {
    if (!obj.rotateOnTimer) return false;
    bool moved = false;
    if (obj.rotateChain) {
        obj.reqChain = (obj.reqChain == chaincode::kVertical)
                           ? chaincode::kDiagonal
                           : static_cast<uint8_t>(obj.reqChain + 1);
        task.chain = obj.reqChain;
        moved = true;
    }
    if (obj.rotateColour) {
        obj.reqColour = (obj.reqColour == kPinkium)
                            ? static_cast<int8_t>(kRedium)
                            : static_cast<int8_t>(obj.reqColour + 1);
        task.colour = obj.reqColour;
        moved = true;
    }
    return moved;
}

}  // namespace tubes
