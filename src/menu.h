// The title screen and its menu - `1b2e:52bf` and the nested procedures that
// share its frame.
//
// `MapProgram.java` labelled `1b2e:52bf` "title / main menu". It is the
// title/attract screen; the menu is a nested Pascal procedure at `1b2e:4d80`
// reaching the parent's locals through the static link at `[BP+4]`, the same
// arrangement as `1000:3a67` inside `1000:9e53`. They only decompile as a
// pair, and they port as one module.
//
// Nothing here touches SDL. See docs/reversing-notes.md for the derivation of
// every constant.

#pragma once

#include <cstdint>
#include <string>

namespace tubes {

// ---------------------------------------------------------------------------
// The page table, DGROUP 0x00ca
// ---------------------------------------------------------------------------
//
// Seven pages, and in the image they are ONE array: the pointers `1b2e:4d80`
// passes are 0xca, 0x256, 0x3e2, 0x56e, 0x6fa, 0x886 and 0xa12, spaced by
// exactly 0x18c. So the declaration is
//
//     MenuPages: array[1..7] of array[0..10] of string[35];
//
// with entry 0 the page TITLE and 1..10 the items. The 0x18c spacing is
// 11 * 0x24, which is what proves the shape.

enum class Page : uint8_t {
    kMain = 1,
    kGameMode = 2,
    kDifficulty = 3,
    kSavesEndurance = 4,
    kSavesWave = 5,
    kOptions = 6,
    kQuit = 7,
};

constexpr int kPageCount = 7;
constexpr int kMaxItems = 10;

// The items on page 1, named rather than numbered because `1b2e:4d80`
// switches on them.
enum MainItem {
    kStartGame = 1,
    kContinueSaved = 2,
    kGameOptions = 3,
    kHighScores = 4,
    kInstructions = 5,
    kViewDemo = 6,
    kCredits = 7,
    kExitTubes = 8,
};

struct MenuPage {
    const char* title;                 // entry 0; empty on page 1
    const char* items[kMaxItems];      // nullptr-terminated
    int count;
    int rowHeight;                     // 16, but 26 on Game Options
};

// Indexed 1..7; element 0 is unused, matching the original's own table.
extern const MenuPage kMenuPages[kPageCount + 1];

// ---------------------------------------------------------------------------
// Layout, `1b2e:467a` and `1b2e:4607`
// ---------------------------------------------------------------------------

// `SetMenuPage` does `INC byte ptr [BP+6]` before computing the origin, so a
// page of n items is laid out as n+1 rows - the extra one is the title, which
// occupies a row whether or not the page has one. Missing that `INC` put every
// row six pixels out, and only a capture of the original caught it.
constexpr int kMenuBlockHeight = 180;   // `0xb4`

int menuYBase(Page p);                  // (180 - rowH*(count+1)) div 2
int menuItemY(Page p, int item);        // yBase + item*rowH
int menuTitleY(Page p);                 // yBase
int menuRuleY(Page p);                  // yBase + 2

// `1b2e:4607`, PlaceStars. `L` is the item's length; the stars bracket the
// centred text and move outward four pixels a character - half a glyph each
// side. The row is the text row plus two.
struct StarPlacement {
    int xLeft;
    int xRight;
    int y;
};
StarPlacement placeStars(Page p, int item);

// `1b2e:4743`, the render. All items share one colour: the SELECTION is marked
// by the stars alone, not by recolouring the text.
constexpr uint8_t kMenuTitleColour = 47;    // `0x2f`
constexpr uint8_t kMenuTitleMode = 0x83;    // shadow | peak
constexpr uint8_t kMenuItemColour = 112;    // `0x70`
constexpr uint8_t kMenuItemMode = 0x82;     // shadow | fade-up

// The rule under a page title is `length(title) - 2` underscores, built by
// `2000:5ec6` from `'_'` = 0x5f.
std::string menuRule(Page p);

// The star sprite: four frames on a three-frame divider, 12 x 10, packed and
// masked, pointers at `DGROUP:0x1d76 + f*8`.
constexpr int kStarFrames = 4;
constexpr int kStarDivider = 3;
constexpr int kStarWidth = 12;
constexpr int kStarHeight = 10;

// ---------------------------------------------------------------------------
// The menu state machine, `1b2e:4d80`
// ---------------------------------------------------------------------------

// What the title screen returns. `1b2e:52bf` hands its caller a byte: 9 on the
// 720-frame timeout, otherwise whatever the menu stored.
enum class MenuResult : uint8_t {
    kNone = 0,
    kPlay = 1,             // a new game, difficulty and mode chosen
    kLoad = 2,             // a saved game, from `slot`
    kHighScores = 4,
    kInstructions = 5,
    kCredits = 7,
    kQuit = 8,
    kAttract = 9,          // the timeout - this is how DEMO.SCR gets played
};

// The globals the game session reads. All four are set HERE and nowhere else,
// which is why the port's difficulty seeding has been dead code until now.
struct MenuChoice {
    bool newGame = true;   // `DS:0x1d4c` - 1 for Start Game, 0 for Continue
    int slot = 0;          // `DS:0x1d4d` - save slot 1..5
    int mode = 1;          // `DS:0x1d4e` - 1 endurance, 2 wave
    int difficulty = 0;    // `DS:0x1d4f` - 0..2 = Tubes 101 / 201 / 301
};

class Menu {
public:
    // `DS:0x1d42`. The title loop swallows the press that raises the menu, so
    // it cannot also select an item - the two-key protocol.
    bool up() const { return up_; }
    void raise() { up_ = true; }

    Page page() const { return page_; }
    int item() const { return item_; }          // `DS:0x1d44`
    const MenuChoice& choice() const { return choice_; }

    void moveUp();
    void moveDown();

    // Enter. Returns kNone while still navigating.
    MenuResult select();

    // Escape. NOT a stack, and not the page you came from: `1b2e:50cf` reads
    // a fixed parent table, and ESC on the MAIN menu opens the quit confirm
    // rather than doing nothing.
    //
    //     7 -> 1     1 -> 7     2 -> 1     3 -> 2
    //     4 -> 2     5 -> 2     6 -> 1
    //
    // `SetMenuPage` does save the outgoing page at `[BP-4]`, but the escape
    // arm does not read it; what that copy is for is not yet known.
    void back();

    // True when the slot holds a live save; the original tests the record's
    // first byte after copying it out of `0x18d8`/`0x1ab8`.
    void setSaveSlotLive(int mode, int slot, bool live);
    bool saveSlotLive(int mode, int slot) const;

    // Pages 4 and 5 hold "(Unavailable)" in the image and the live text is
    // copied over each slot at runtime.
    const char* itemText(int i) const;

    // The turning star, advanced once per frame.
    void tick();
    int starFrame() const { return starFrame_; }

private:
    void setPage(Page p);

    bool up_ = false;
    Page page_ = Page::kMain;
    Page previous_ = Page::kMain;   // `[BP-4]`
    int item_ = 1;                  // `DS:0x1d44`
    int mainItem_ = 1;              // `DS:0x1d43`, the remembered main-menu row
    MenuChoice choice_;
    bool slotLive_[3][6] = {};      // [mode][slot], 1-based
    int starTick_ = 1;              // `DS:0x1d78`, 1..3
    int starFrame_ = 1;             // `DS:0x1d79`, 1..4
};

// ---------------------------------------------------------------------------
// The atom in the letterforms
// ---------------------------------------------------------------------------
//
// TUBESBG.GFX and TUBESFG.GFX are a background/foreground pair - the same
// arrangement as the play field - and the word TUBES is drawn as a connected
// tube network with the atom travelling INSIDE the pipes, clipped by the
// foreground exactly the way a falling atom is. The walk is 25 legs, one per
// stroke of the five letters plus a return run, out of four tables that sit
// contiguously at DGROUP 0x30..0xc9.

constexpr int kTitleLegs = 25;

// The 720-frame timeout, `0x2d0`. Reset on every accepted keypress.
constexpr int kAttractTimeout = 720;

struct TitleAtom {
    int x = 61;      // the initial (61, 53) is a point part-way down leg 1,
    int y = 53;      // so the circuit closes with no seam
    int leg = 1;     // 1..25

    // One frame: 4 px along the leg's axis, with the cross-axis dragged toward
    // the corner by 7, 6, 3, 2, 1 as the limit is approached. Leg 23's `dirV`
    // byte is neither 'F' nor 'B', so that leg does not round - it is the T's
    // stem walked down and straight back up, a reversal rather than a turn.
    void step();
};

// Exposed for the tests, which walk the whole circuit and check that it closes.
int titleLegLimitX(int leg);
int titleLegLimitY(int leg);
char titleLegDirH(int leg);
char titleLegDirV(int leg);   // 'F', 'B', or '?' for leg 23

}  // namespace tubes
