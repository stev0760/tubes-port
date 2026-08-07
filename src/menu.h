// The title screen and its menu - `1b2e:52bf` and the nested procedures that
// share its frame.
//
// `MapProgram.java` labels `1b2e:52bf` "title / main menu". This is the
// title/attract screen. The menu is a nested Pascal procedure at `1b2e:4d80`
// that reaches the parent's locals through the static link at `[BP+4]`, the
// same arrangement as `1000:3a67` inside `1000:9e53`. They decompile and port
// as one module.
//
// Nothing here touches SDL. See docs/reversing-notes.md for the derivation of
// every constant.

#pragma once

#include <cstdint>
#include <string>

#include "edition.h"

namespace tubes {

// ---------------------------------------------------------------------------
// The page table, DGROUP 0x00ca
// ---------------------------------------------------------------------------
//
// The image stores the seven pages as one array. The pointers `1b2e:4d80`
// passes are 0xca, 0x256, 0x3e2, 0x56e, 0x6fa, 0x886 and 0xa12, spaced by
// exactly 0x18c. So the declaration is
//
//     MenuPages: array[1..7] of array[0..10] of string[35];
//
// Entry 0 is the page title and 1..10 are the items. The 0x18c spacing is
// 11 * 0x24, which proves the shape.

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

// The shareware main page is the same page with two items inserted:
// `Preview Registered` at 3 and `Ordering Info` at 8. Everything below each
// insertion shifts down, so Exit ends up at 10. The registered build has it
// at 8.
//
// This is not a guessed layout. The labels are data at a `0x24` stride in
// both images, which is why no function references them and why an early
// search for `Start Game` came back empty in both editions. `entry`'s
// dispatch agrees arm for arm - `1000:abf7` compares the selection against
// `0xa`, while the registered `1000:b2ba` compares against 8.
enum SharewareMainItem {
    kSwStartGame = 1,
    kSwContinueSaved = 2,
    kSwPreviewRegistered = 3,   // `1000:ab4e`, sets DS:0x1d4b
    kSwGameOptions = 4,
    kSwHighScores = 5,
    kSwInstructions = 6,
    kSwViewDemo = 7,            // `1000:ab87`, ALSO sets DS:0x1d4b
    kSwOrderingInfo = 8,        // `1000:aba9`
    kSwCredits = 9,
    kSwExitTubes = 10,          // `1000:abf7  CMP ...,0xa`
};

struct MenuPage {
    const char* title;                 // entry 0. empty on page 1
    const char* items[kMaxItems];      // nullptr-terminated
    int count;
    int rowHeight;                     // 16, but 26 on Game Options
};

// Indexed 1..7. Element 0 matches the original's table and stays unused.
// **This is the image's data and must stay that way** - read it to know what
// the original menu said. The rest of this file routes through `menuPage`
// below, where the port's one addition lives.
extern const MenuPage kMenuPages[kPageCount + 1];

// Game Options with the port's own `Graphics Options` row inserted before
// Exit - this is the port's page, not the original's, which is why it is a
// separate object rather than an edit to `kMenuPages`.
//
// `input.h` is the reason for adding the row. The original's Game Options
// page ends in `Redefine Input Device`, which chooses a DOS input driver.
// SDL makes that item meaningless as written. The port already keeps the row
// and gives it a modern meaning. Display settings are the same case one layer
// over - Mode X was the only display the original had - so they get a row
// beside it rather than a key nobody would find.
//
// That shifts the layout. A five-item page at the same 26-pixel pitch starts
// 13 pixels higher than the original's four-item one, so page 6 is the only
// menu page the port does not render pixel-identically. Pages 1 and 3 measure
// 0.00% and are untouched by this.
extern const MenuPage kOptionsPagePort;

// The shareware main page. Unlike `kOptionsPagePort`, this is not the port's
// invention - it is the other edition's own table, so it sits beside
// `kMenuPages` as data rather than as an addition.
extern const MenuPage kMainPageShareware;

// The page as the port draws it: `kOptionsPagePort` for page 6, the image's
// own row for every other page. Every layout, navigation and selection path
// goes through here.
const MenuPage& menuPage(Page p, Edition e = Edition::kRegistered);

// ---------------------------------------------------------------------------
// Layout, `1b2e:467a` and `1b2e:4607`
// ---------------------------------------------------------------------------

// `SetMenuPage` does `INC byte ptr [BP+6]` before computing the origin, so a
// page of n items is laid out as n+1 rows. The extra one is the title, which
// occupies a row whether or not the page has one. Missing that `INC` put every
// row six pixels out. Only a capture of the original caught it.
constexpr int kMenuBlockHeight = 180;   // `0xb4`

int menuYBase(Page p, Edition e = Edition::kRegistered);                  // (180 - rowH*(count+1)) div 2
int menuItemY(Page p, int item, Edition e = Edition::kRegistered);        // yBase + item*rowH
int menuTitleY(Page p, Edition e = Edition::kRegistered);                 // yBase
int menuRuleY(Page p, Edition e = Edition::kRegistered);                  // yBase + 2

// `1b2e:4607`, PlaceStars. `L` is the item's length. The stars bracket the
// centered text and move outward four pixels a character - half a glyph each
// side. The row sits two pixels below the text row.
struct StarPlacement {
    int xLeft;
    int xRight;
    int y;
};
// `text` is what is actually on the row. Pass null for the page's own.
StarPlacement placeStars(Page p, int item, const char* text = nullptr,
                         Edition e = Edition::kRegistered);

// `1b2e:4743`, the render. All items share one colour. The stars mark the
// selection. The text is not recoloured.
constexpr uint8_t kMenuTitleColour = 47;    // `0x2f`
constexpr uint8_t kMenuTitleMode = 0x83;    // shadow | peak
constexpr uint8_t kMenuItemColour = 112;    // `0x70`
constexpr uint8_t kMenuItemMode = 0x82;     // shadow | fade-up

// The rule under a page title is `length(title) - 2` underscores, built by
// `2000:5ec6` from `'_'` = 0x5f.
std::string menuRule(Page p, Edition e = Edition::kRegistered);

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
    // `1000:b264` and `1000:b287` do the same three stores - mode 0, new game,
    // difficulty 2 - so View Demo and the attract timeout are one path. The
    // timeout runs the blackboard cutscene `1b2e:1651` first and skips the
    // demo if that returns 2.
    kViewDemo = 6,
    kCredits = 7,
    kQuit = 8,
    kAttract = 9,          // the timeout - this is how the game plays DEMO.SCR

    // Page 6's three live items. The original does the first two inline and
    // its third loads a driver. The port hands all three back to the caller,
    // because the audio flags and SDL bindings they act on belong to the
    // caller. These values do not come from the original, hence the range.
    kToggleMusic = 20,
    kToggleSound = 21,
    kRedefine = 22,
    // The port's added row - see `kOptionsPagePort`. Nothing in the original
    // returns this because the original had nothing to return it for.
    kGraphics = 23,

    // The shareware's two extra items. They cannot reuse the original's own
    // codes for them - 3 and 8 - because the registered dispatch keys this
    // enum, and there those already mean Game Options and Quit. The shareware
    // numbering lives on `SharewareMainItem` instead.
    kPreview = 30,         // `1000:ab4e`, menu item 3
    kOrdering = 31,        // `1000:aba9`, menu item 8
};

// The globals the game session reads. The menu sets all four, and nothing
// else does, so the port's difficulty seeding stayed dead code until now.
struct MenuChoice {
    bool newGame = true;   // `DS:0x1d4c` - 1 for Start Game, 0 for Continue
    int slot = 0;          // `DS:0x1d4d` - save slot 1..5
    int mode = 1;          // `DS:0x1d4e` - 1 endurance, 2 wave
    int difficulty = 0;    // `DS:0x1d4f` - 0..2 = Tubes 101 / 201 / 301
};

class Menu {
public:
    // Which edition's menu this is. It changes the main page's contents,
    // length, and layout - a ten-item page centers differently from an
    // eight-item one.
    void setEdition(Edition e) { edition_ = e; }
    Edition edition() const { return edition_; }

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

    // Escape. Not a stack, and not the page you came from: `1b2e:50cf` reads
    // a fixed parent table, and ESC on the main menu opens the quit confirm
    // rather than doing nothing.
    //
    //     7 -> 1     1 -> 7     2 -> 1     3 -> 2
    //     4 -> 2     5 -> 2     6 -> 1
    //
    // `SetMenuPage` does save the outgoing page at `[BP-4]`, but the escape
    // arm does not read it. The copy's purpose is unknown.
    void back();

    // Records whether the slot holds a live save. The original tests the
    // record's first byte after copying it out of `0x18d8`/`0x1ab8`.
    void setSaveSlotLive(int mode, int slot, bool live);
    // Returns the recorded state.
    bool saveSlotLive(int mode, int slot) const;

// Pages 4 and 5 store "(Unavailable)" in the image. At runtime,
// `1b2e:5427` onward builds one string per live record and assigns it over
// the menu item. `saveSlotLabel` in save.h builds the same string. This
// function stores it.
    void setSaveSlotText(int mode, int slot, const std::string& text);
    // Page 6's first two rows carry their state - the captured page reads
    // "Toggle Music <yes/no>" - so the code builds them at runtime, like the
    // slot rows, instead of using fixed strings.
    void setOptionText(int item, const std::string& text);
    // The Graphics row is the port's addition, so what page 6 does about it is
    // the port's too: `select()` returns `kGraphics` for whichever row it is on.
    static constexpr int kGraphicsItem = 4;
    const char* itemText(int i) const;

    // The turning star, advanced once per frame.
    void tick();
    int starFrame() const { return starFrame_; }

private:
    Edition edition_ = Edition::kRegistered;
    void setPage(Page p);

    bool up_ = false;
    Page page_ = Page::kMain;
    Page previous_ = Page::kMain;   // `[BP-4]`
    int item_ = 1;                  // `DS:0x1d44`
    int mainItem_ = 1;              // `DS:0x1d43`, the remembered main-menu row
    MenuChoice choice_;
    bool slotLive_[3][6] = {};      // [mode][slot], 1-based
    std::string slotText_[3][6];    // the live row, empty when the slot is
    std::string optionText_[kMaxItems + 1];   // page 6, 1-based
    int starTick_ = 1;              // `DS:0x1d78`, 1..3
    int starFrame_ = 1;             // `DS:0x1d79`, 1..4
};

// ---------------------------------------------------------------------------
// The atom in the letterforms
// ---------------------------------------------------------------------------
//
// TUBESBG.GFX and TUBESFG.GFX are a background/foreground pair, the same
// arrangement as the play field. The code draws TUBES as a connected tube
// network with the atom travelling inside the pipes. The foreground clips it
// exactly like a falling atom. The walk is 25 legs, one per stroke of the five
// letters plus a return run, driven by four contiguous tables at DGROUP
// 0x30..0xc9.

constexpr int kTitleLegs = 25;

// The 720-frame timeout, `0x2d0`. The title loop resets it on every accepted
// keypress.
constexpr int kAttractTimeout = 720;

struct TitleAtom {
    int x = 61;      // the initial (61, 53) is a point part-way down leg 1,
    int y = 53;      // so the circuit closes with no seam
    int leg = 1;     // 1..25

// One frame: 4 px along the leg's axis, with the cross-axis dragged toward
// the corner by 7, 6, 3, 2, 1 as the leg nears its limit. Leg 23's `dirV`
// byte is neither 'F' nor 'B', so that leg does not round - it is the T's
// stem walked down and straight back up, a reversal rather than a turn.
    void step();
};

// Exposed for the tests, which walk the whole circuit and check that it
// closes.
int titleLegLimitX(int leg);
int titleLegLimitY(int leg);
char titleLegDirH(int leg);
char titleLegDirV(int leg);   // 'F', 'B', or '?' for leg 23

}  // namespace tubes
