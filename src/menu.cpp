#include "menu.h"

namespace tubes {
namespace {

// ---------------------------------------------------------------------------
// The path tables, DGROUP 0x30..0xc9
// ---------------------------------------------------------------------------
//
// One contiguous block, bracketed by 0x00 at 0x95, 0xaf and 0xc9 - the unused
// element 0 of each byte table plus a terminator - with the menu page table
// starting immediately after at 0xca. That bracketing is what confirms the
// strides rather than assuming them.
//
// `limY` here already has the `+0x20` the code applies on load.

constexpr int kLimX[kTitleLegs + 1] = {
    0,
     61,  94,  94, 120, 120, 166, 166, 139, 139, 224,
    224, 190, 190, 280, 280, 245, 245, 303, 303,   1,
      1,  31,  31,  31,  61,
};

constexpr int kLimY[kTitleLegs + 1] = {
    0,
    127, 127,  34,  34, 127, 127,  73,  73, 100, 100,
     73,  73, 127, 127, 100, 100,  73,  73, 147, 147,
     33,  33, 127,  33,  33,
};

// 'F' forward, 'B' back. Leg 23 holds 0x3f, which is NEITHER, so neither
// corner-rounding branch fires there. Not corruption: legs 23 and 24 are the
// T's stem walked down and straight back up, and rounding a reversal would
// bulge the stem sideways.
constexpr char kDirV[kTitleLegs + 1] = {
    0,
    'B', 'B', 'F', 'F', 'B', 'B', 'B', 'B', 'B', 'B',
    'B', 'B', 'B', 'B', 'B', 'F', 'F', 'F', 'F', 'F',
    'F', 'F', '?', 'F', 'F',
};

constexpr char kDirH[kTitleLegs + 1] = {
    0,
    'D', 'R', 'U', 'R', 'D', 'R', 'U', 'L', 'D', 'R',
    'U', 'L', 'D', 'R', 'U', 'L', 'U', 'R', 'D', 'L',
    'U', 'R', 'D', 'U', 'R',
};

// The cross-axis displacement as a limit is approached, innermost first. The
// original spells this out as a five-arm if/else chain against limit+1, +2,
// +4, +6 and beyond; the thresholds and the offsets are paired here.
struct Curve { int within; int offset; };
constexpr Curve kCurve[5] = {{1, 7}, {2, 6}, {4, 3}, {6, 2}, {0x7fff, 1}};

int curveOffset(int distance) {
    for (const Curve& c : kCurve) {
        if (distance <= c.within) return c.offset;
    }
    return 1;
}

}  // namespace

int titleLegLimitX(int leg) { return kLimX[leg]; }
int titleLegLimitY(int leg) { return kLimY[leg]; }
char titleLegDirH(int leg) { return kDirH[leg]; }
char titleLegDirV(int leg) { return kDirV[leg]; }

void TitleAtom::step() {
    const int limX = kLimX[leg];
    const int limY = kLimY[leg];
    const char dh = kDirH[leg];
    const char dv = kDirV[leg];
    bool turn = false;

    // The sign the cross-axis is displaced in. 'F' pulls one way and 'B' the
    // other; leg 23's byte is neither, so `sign` stays 0 and the cross-axis is
    // left alone - which is exactly what the original's two `if`s do when the
    // byte matches neither.
    const int sign = (dv == 'F') ? -1 : (dv == 'B') ? +1 : 0;

    if (dh == 'L') {
        x -= 4;
        if (x < limX) { x = limX; turn = true; }
        if (sign && x < limX + 10) y = limY + sign * curveOffset(x - limX);
    } else if (dh == 'R') {
        x += 4;
        if (x > limX) { x = limX; turn = true; }
        if (sign && x > limX - 10) y = limY - sign * curveOffset(limX - x);
    } else if (dh == 'U') {
        y -= 4;
        if (y < limY) { y = limY; turn = true; }
        if (sign && y < limY + 10) x = limX + sign * curveOffset(y - limY);
    } else if (dh == 'D') {
        y += 4;
        if (y > limY) { y = limY; turn = true; }
        if (sign && y > limY - 10) x = limX - sign * curveOffset(limY - y);
    }

    if (turn) {
        ++leg;
        if (leg > kTitleLegs) leg = 1;
    }
}

// ---------------------------------------------------------------------------
// The pages
// ---------------------------------------------------------------------------
//
// Transcribed from the image at DGROUP 0x00ca, stride 0x24 per entry and
// 0x18c per page. Like `wave_text.cpp`, this is the one place in `src/` that
// holds the game's own prose: if the project ever reads these out of the
// user's TUBES.EXE at runtime instead, only this table changes.
//
// "Endurace Mode" is the game's own spelling and is reproduced as such.
// Pages 4 and 5 hold "(Unavailable)" in the image; the live text is copied
// over each slot from the save records at 0x18d8 and 0x1ab8.

const MenuPage kMenuPages[kPageCount + 1] = {
    {"", {nullptr}, 0, 16},
    {"", {"Start Game", "Continue Saved Game", "Game Options", "High Scores",
          "Instructions", "View Demo", "Credits", "Exit Tubes"}, 8, 16},
    {"Game Mode", {"Endurace Mode", "Wave Mode", "Exit"}, 3, 16},
    {"Difficulty", {"Tubes 101", "Tubes 201", "Tubes 301", "Exit"}, 4, 16},
    {"Saved Games Available",
     {"(Unavailable)", "(Unavailable)", "(Unavailable)", "(Unavailable)",
      "(Unavailable)", "Exit"}, 6, 16},
    {"Saved Games Available",
     {"(Unavailable)", "(Unavailable)", "(Unavailable)", "(Unavailable)",
      "(Unavailable)", "Exit"}, 6, 16},
    {"Game Options", {"Toggle Music", "Toggle Sound FX",
                      "Redefine Input Device", "Exit"}, 4, 26},
    {"Exit Tubes?", {"Yes", "No"}, 2, 16},
};

int menuYBase(Page p) {
    const MenuPage& mp = kMenuPages[static_cast<int>(p)];
    return (kMenuBlockHeight - mp.rowHeight * (mp.count + 1)) / 2;
}

int menuItemY(Page p, int item) {
    return menuYBase(p) + item * kMenuPages[static_cast<int>(p)].rowHeight;
}

int menuTitleY(Page p) { return menuYBase(p); }
int menuRuleY(Page p) { return menuYBase(p) + 2; }

std::string menuRule(Page p) {
    const char* t = kMenuPages[static_cast<int>(p)].title;
    int n = 0;
    while (t[n]) ++n;
    n -= 2;
    return n > 0 ? std::string(static_cast<size_t>(n), '_') : std::string();
}

StarPlacement placeStars(Page p, int item) {
    const char* s = kMenuPages[static_cast<int>(p)].items[item - 1];
    int len = 0;
    while (s && s[len]) ++len;
    return {140 - 4 * len, 163 + 4 * len, menuItemY(p, item) + 2};
}

// ---------------------------------------------------------------------------
// The menu
// ---------------------------------------------------------------------------

void Menu::setPage(Page p) {
    previous_ = page_;
    page_ = p;
    // Arriving at page 1 restores the remembered row; every other page starts
    // at its first item. `DS:0x1d43` is that memory.
    item_ = (p == Page::kMain) ? mainItem_ : 1;
}

void Menu::moveUp() {
    const int n = kMenuPages[static_cast<int>(page_)].count;
    item_ = (item_ <= 1) ? n : item_ - 1;
    if (page_ == Page::kMain) mainItem_ = item_;
}

void Menu::moveDown() {
    const int n = kMenuPages[static_cast<int>(page_)].count;
    item_ = (item_ >= n) ? 1 : item_ + 1;
    if (page_ == Page::kMain) mainItem_ = item_;
}

void Menu::back() { setPage(previous_); }

void Menu::setSaveSlotLive(int mode, int slot, bool live) {
    if (mode >= 1 && mode <= 2 && slot >= 1 && slot <= 5) {
        slotLive_[mode][slot] = live;
    }
}

bool Menu::saveSlotLive(int mode, int slot) const {
    if (mode < 1 || mode > 2 || slot < 1 || slot > 5) return false;
    return slotLive_[mode][slot];
}

const char* Menu::itemText(int i) const {
    return kMenuPages[static_cast<int>(page_)].items[i - 1];
}

void Menu::tick() {
    if (++starTick_ > kStarDivider) {
        starTick_ = 1;
        if (++starFrame_ > kStarFrames) starFrame_ = 1;
    }
}

MenuResult Menu::select() {
    switch (page_) {
    case Page::kMain:
        mainItem_ = item_;
        switch (item_) {
        case kStartGame:
            choice_.newGame = true;
            setPage(Page::kGameMode);
            return MenuResult::kNone;
        case kContinueSaved:
            choice_.newGame = false;
            setPage(Page::kGameMode);
            return MenuResult::kNone;
        case kGameOptions:
            setPage(Page::kOptions);
            return MenuResult::kNone;
        case kExitTubes:
            setPage(Page::kQuit);
            return MenuResult::kNone;
        default:
            // High Scores, Instructions, View Demo and Credits leave the title
            // screen, returning the item number.
            return static_cast<MenuResult>(item_);
        }

    case Page::kGameMode:
        if (item_ == 1 || item_ == 2) {
            choice_.mode = item_;
            setPage(choice_.newGame
                        ? Page::kDifficulty
                        : (item_ == 1 ? Page::kSavesEndurance : Page::kSavesWave));
        } else {
            setPage(Page::kMain);
        }
        return MenuResult::kNone;

    case Page::kDifficulty:
        if (item_ >= 1 && item_ <= 3) {
            choice_.difficulty = item_ - 1;
            return MenuResult::kPlay;
        }
        setPage(Page::kGameMode);
        return MenuResult::kNone;

    case Page::kSavesEndurance:
    case Page::kSavesWave: {
        if (item_ < 1 || item_ > 5) {
            setPage(Page::kGameMode);
            return MenuResult::kNone;
        }
        choice_.slot = item_;
        // Only a live record leaves the menu; an empty slot is simply ignored,
        // which is why the original re-tests the copied record's first byte.
        return saveSlotLive(choice_.mode, item_) ? MenuResult::kLoad
                                                 : MenuResult::kNone;
    }

    case Page::kOptions:
        // Toggling music and sound effects is the caller's business; only the
        // Exit arm changes page.
        if (item_ == 4) setPage(Page::kMain);
        return MenuResult::kNone;

    case Page::kQuit:
        if (item_ == 1) return MenuResult::kQuit;
        setPage(Page::kMain);
        return MenuResult::kNone;
    }
    return MenuResult::kNone;
}

}  // namespace tubes
