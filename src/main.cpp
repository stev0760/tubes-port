// tubes-port - an SDL reimplementation of Tubes (Absolute Magic, 1994).
//
// Ships no game data. Assets are read at runtime from the user's own copy of
// the original game; point --gamedir at the directory holding TUBES.RES.

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "font.h"
#include "game.h"
#include "gfx.h"
#include "mus.h"
#include "opl.h"
#include "res.h"
#include "screen.h"

namespace {

// Playfield geometry, all measured from the draw loop in 1000:3a67.
//
// The playfield is 6 x 5, not the 7 x 10 the manual implied. Columns are
// pitched 18 apart while the sprites are 16 wide, which is why nothing lined
// up when the pitch was assumed equal to the cell size. The grid spans
// x 107..212, centred on 160 - exactly the centre of the x 74..245 gap
// between the tube walls in GAMEFG.GFX. See docs/reversing-notes.md.
constexpr int kCellW = 16;      // atom sprite width
constexpr int kCellH = 13;      // atom sprite height
constexpr int kPitchX = 18;     // column pitch: sprites are 16 wide, so a 2px gap
constexpr int kPitchY = 13;     // row pitch: rows touch exactly
constexpr int kCols = 6;
constexpr int kRows = 5;
// Column x comes from a six-entry table; the values are 107..197 step 18.
// Row y is computed inline as `row * 13 + 121` for row = 1..5.
constexpr int kGridX = 107;
constexpr int kGridY = 121 + kPitchY;
// The test tube hangs at y = 68 - the literal `0x44` the session setup writes
// into its record at `1000:4409`, and the same 68 the dirty-rect restore uses
// with its 22 x 65 extent. The previous 69 came from 134 - 65, reasoning back
// from the beaker's top; it was one pixel out.
constexpr int kTestTubeH = 65;
constexpr int kTubeY = 68;
constexpr float kFallHeight = static_cast<float>(kTubeY - 16);

// The ball table, transliterated from the original's own initialiser. The game
// builds it at `DS:0x1da6 + 4 * type`, and the entry program assigns each slot
// a resource name in order, so this is not inferred from sprite filenames - it
// is the game's table:
//
//     0x1daa REDBALL   0x1dae GRENBALL  0x1db2 BLUEBALL  0x1db6 CYANBALL
//     0x1dba PURPBALL  0x1dbe YELWBALL  0x1dc2 PINKBALL  0x1dc6 (none)
//     0x1dca ANTIBALL  0x1dce GOLDBALL  0x1dd2 XENBALL   0x1dd6 MULTBALL
//     0x1dda EVILBALL  0x1dde CONVBALL  0x1de2 BLOCBALL  0x1de6 FILLBALL
//     0x1dea OBSTBALL  0x1dee CRYSTAL   0x1df2 MYSTBALL
//
// The gap at 0x1dc6 is type 8, Flashium, which has no sprite of its own -
// confirming from the code what was previously only observed.
const char* kAtomSprites[tubes::kTypeCount] = {
    nullptr,            // 0  empty
    "REDBALL.CSP",      // 1  Redium
    "GRENBALL.CSP",     // 2  Greenium
    "BLUEBALL.CSP",     // 3  Bluium
    "CYANBALL.CSP",     // 4  Cyanium
    "PURPBALL.CSP",     // 5  Purplium
    "YELWBALL.CSP",     // 6  Yellowium
    "PINKBALL.CSP",     // 7  Pinkium
    nullptr,            // 8  Flashium - no sprite, cycles the seven colours
    "ANTIBALL.CSP",     // 9  AntiMatter
    "GOLDBALL.CSP",     // 10 Bonus
    "XENBALL.CSP",      // 11 Xenon
    "MULTBALL.CSP",     // 12 Multiplier
    "EVILBALL.CSP",     // 13 EvilMultiplier
    "CONVBALL.CSP",     // 14 Convertor
    "BLOCBALL.CSP",     // 15 Blocker
    "FILLBALL.CSP",     // 16 Filler
    "OBSTBALL.CSP",     // 17 obstacle
    "CRYSTAL.CSP",      // 18 Crystal
    "MYSTBALL.CSP",     // 19 the "?" concealment sprite, not a ball
};

// The fade families, one per type, in the order `1000:9e53` loads them. The
// loader's inner sequence is types 1..10 then 18, and the eleven name strings
// sit consecutively at 1000:9d07:
//
//     RFADE GFADE BFADE CFADE PFADE YFADE PNKFADE FFADE AFADE GLDFADE CRFADE
//
// followed immediately by the same eleven with `.SFX` - one sound per family,
// which is why the clear sound follows the colour the stack matched AS rather
// than each ball's own type.
//
// Types 11..17 and 19 have null entries: they are never cleared by matching,
// so they have no fade of their own. A fade family is an EFFECT, not a
// "can be cleared" marker - AFADE is AntiMatter's blast applied to everything
// caught in it, and CRFADE is the Crystal's teleport, forward then reverse.
const char* kFadeFamilies[tubes::kTypeCount] = {
    nullptr,      // 0  empty
    "RFADE",      // 1  Redium
    "GFADE",      // 2  Greenium
    "BFADE",      // 3  Bluium
    "CFADE",      // 4  Cyanium
    "PFADE",      // 5  Purplium
    "YFADE",      // 6  Yellowium
    "PNKFADE",    // 7  Pinkium
    "FFADE",      // 8  Flashium
    "AFADE",      // 9  AntiMatter
    "GLDFADE",    // 10 Bonus
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,   // 11..17
    "CRFADE",     // 18 Crystal
    nullptr,      // 19 MYSTBALL is a rendering state, not a ball
};

// How many distinct values a beaker cell can take: `type + 19 * fadeFrame`
// runs to 152 before the cell empties, so the sprite table needs 153 slots.
constexpr int kCellStates = tubes::kCellClearAbove + 1;

// The tube network furniture, decompiled out of 1000:3a67. It is NOT a
// backdrop: the network is built from individual segment sprites in layered
// passes, and the atoms are drawn *between* those passes so the solid pieces
// overpaint them. That is what makes the tubes read as hollow, with atoms
// visibly inside them and tubes overlapping one another.
enum Furn {
    kTubeH, kTubeHS, kTubeHR,
    kTubeV, kTubeVS, kTubeVR, kTubeVRS, kTubeVL, kTubeVLS,
    kBeakerFront, kBeakerShadow, kTestTubeShadow, kMarker,
    kFurnCount
};

const char* kFurnFiles[kFurnCount] = {
    "TUBEH.CSP",  "TUBEHS.CSP",  "TUBEHR.CSP",
    "TUBEV.CSP",  "TUBEVS.CSP",  "TUBEVR.CSP", "TUBEVRS.CSP",
    "TUBEVL.CSP", "TUBEVLS.CSP",
    "BEAKER.CSP", "BEAKERS.CSP", "TESTUBES.CSP", "MARKER.CSP",
};

struct FurnDraw {
    uint8_t sprite;
    int16_t y, x;
};

// Transcribed in order from the decompiled draw sequence. The two ATOMS marks
// in that sequence split this into three groups; see kFurnGroup* below.
const FurnDraw kFurniture[] = {
    // --- group 0: back layers, drawn before any atom ---
    {kTubeH,   26,  34}, {kTubeH,   26, 270}, {kTubeH,   26,  58},
    {kTubeH,   26, 246}, {kTubeH,   26, 107}, {kTubeH,   26, 197},
    {kTubeHR,  26, 125}, {kTubeH,   26, 179},
    {kTubeHS,  13,  58}, {kTubeHS,  13, 246}, {kTubeHS,  13, 107},
    {kTubeHS,  13, 197},
    {kTubeVLS, 26,  34}, {kTubeVRS, 26, 270}, {kTubeVRS, 26, 125},
    {kTubeVLS, 26, 179},
    // --- group 1: mid layers ---
    {kTubeH,   13,  58}, {kTubeH,   13, 246}, {kTubeHR,  13, 107},
    {kTubeH,   13, 197},
    {kTubeVL,  26,  34}, {kTubeVR,  26, 270}, {kTubeVR,  26, 125},
    {kTubeVL,  26, 179},
    {kTubeVLS, 13,  58}, {kTubeVRS, 13, 246}, {kTubeVRS, 13, 107},
    {kTubeVLS, 13, 197},
    {kTubeVS,  26,  58}, {kTubeVS,  26, 246}, {kTubeVS,  26, 107},
    {kTubeVS,  26, 197},
    // --- group 2: front layers ---
    {kTubeVL,  13,  58}, {kTubeVR,  13, 246}, {kTubeVR,  13, 107},
    {kTubeVL,  13, 197},
    {kTubeV,   26,  58}, {kTubeV,   26, 246}, {kTubeV,   26, 107},
    {kTubeV,   26, 197},
};
constexpr int kFurnGroup0 = 16;   // atoms are drawn after this many
constexpr int kFurnGroup1 = 32;   // and again after this many
constexpr int kFurnTotal = static_cast<int>(sizeof(kFurniture) /
                                            sizeof(kFurniture[0]));

struct Options {
    std::string gameDir = ".";
    int scale = 0;              // 0 = pick the largest that fits
    std::string screenshot;     // render one frame here and exit
    int autoFrames = 0;         // simulate N scripted frames first
    bool demo = false;          // let the scripted player drive the real loop
    std::string music = "TUBES.MUS";   // song to play; empty disables audio
    std::string renderMus;      // render a song to WAV and exit
    std::string dumpRegs;       // print the OPL register stream and exit
    std::string renderState;    // load a captured state, render it, exit
    std::string gameBg = "GAMEBG1.GFX";   // backdrop, for matching a capture
    double renderSeconds = 0;   // 0 = one pass, songs loop forever
    bool help = false;
};


// Load a state captured from the original so this engine can render the exact
// same frame. Text, one directive per line, because the harness that writes it
// is a Python script talking to a debugger and a tiny format keeps both ends
// obvious:
//
//     tubecol <0..5>
//     tubex <pixels>          optional; the tube's exact x, mid-slide
//     grid <30 type values, row-major from the top>
//     tube <types held in the test tube, bottom slot first>
//     score <n>   chains <n>   drops <n>   pending <award> <multiplier>
//     atom <x> <y> <state> <column> <type> [<slot>]
//
// The optional slot is the atom's index in the original's array. It matters
// because that index IS the column for the six network atoms, and the index
// alone decides how deep in the tube artwork the atom is drawn. Captures
// written before that was known omit it, and those are read as slot = column,
// which is right for exactly the atoms they contain.
//
bool loadState(const std::string& path, tubes::Game& game) {
    std::FILE* fh = std::fopen(path.c_str(), "r");
    if (!fh) {
        std::fprintf(stderr, "cannot open state %s\n", path.c_str());
        return false;
    }
    char line[1024];
    while (std::fgets(line, sizeof(line), fh)) {
        char key[32] = {0};
        if (std::sscanf(line, "%31s", key) != 1) continue;
        const char* rest = line + std::strlen(key);
        if (!std::strcmp(key, "tubecol")) {
            game.setTubeColumn(std::atoi(rest));
        } else if (!std::strcmp(key, "tubex")) {
            // Overrides the stop, and must therefore come after `tubecol`.
            // A paused capture regularly catches the tube between two stops,
            // and pinning it to the nearer one put a whole tube's worth of
            // false difference into the diff.
            game.setTubeX(std::atoi(rest));
        } else if (!std::strcmp(key, "grid")) {
            tubes::Board& b = game.boardMutable();
            b.clear();
            const char* p2 = rest;
            for (int i = 0; i < 30; ++i) {
                int v = 0;
                if (std::sscanf(p2, "%d", &v) != 1) break;
                b.set(i % 6, i / 6, static_cast<int8_t>(v));
                while (*p2 == ' ') ++p2;
                while (*p2 && *p2 != ' ') ++p2;
            }
        } else if (!std::strcmp(key, "score")) {
            game.setScore(std::atoi(rest));
        } else if (!std::strcmp(key, "chains")) {
            game.setChains(std::atoi(rest));
        } else if (!std::strcmp(key, "drops")) {
            game.setDropsRemaining(std::atoi(rest));
        } else if (!std::strcmp(key, "pending")) {
            int p = 0, m = 0;
            if (std::sscanf(rest, "%d %d", &p, &m) >= 1) {
                game.setScorePending(p, m);
            }
        } else if (!std::strcmp(key, "tube")) {
            std::vector<int8_t> v;
            const char* p2 = rest;
            int t = 0;
            while (std::sscanf(p2, "%d", &t) == 1) {
                v.push_back(static_cast<int8_t>(t));
                while (*p2 == ' ') ++p2;
                while (*p2 && *p2 != ' ') ++p2;
                if (!*p2) break;
            }
            game.setTubeAtoms(v);
        } else if (!std::strcmp(key, "atom")) {
            tubes::Falling f;
            int x=0, y=0, st=0, col=1, ty=0, slot=0;
            const int n = std::sscanf(rest, "%d %d %d %d %d %d",
                                      &x, &y, &st, &col, &ty, &slot);
            if (n >= 5) {
                if (n < 6) slot = col;
                // Slots 7..12 are the atoms tipped out of the test tube and
                // falling into the beaker. They used to be skipped, because the
                // engine settled a tip instantly and had nowhere to put one;
                // now they are records like any other and load straight in.
                if (slot < 1 || slot > tubes::kAtomRecords) continue;
                f.x = x; f.y = y;
                f.state = static_cast<uint8_t>(st);
                f.column = col;
                f.colour = static_cast<int8_t>(ty);
                f.anchorX = x;
                f.targetY = y;
                game.setAtom(slot, f);
            }
        }
    }
    std::fclose(fh);
    return true;
}

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "--gamedir" || a == "-g") && i + 1 < argc) {
            o.gameDir = argv[++i];
        } else if (a == "--scale" && i + 1 < argc) {
            o.scale = std::atoi(argv[++i]);
        } else if (a == "--screenshot" && i + 1 < argc) {
            o.screenshot = argv[++i];
        } else if (a == "--auto" && i + 1 < argc) {
            o.autoFrames = std::atoi(argv[++i]);
        } else if (a == "--demo") {
            o.demo = true;
        } else if (a == "--music" && i + 1 < argc) {
            o.music = argv[++i];
        } else if (a == "--no-music") {
            o.music.clear();
        } else if (a == "--render-mus" && i + 2 < argc) {
            o.music = argv[++i];
            o.renderMus = argv[++i];
        } else if (a == "--seconds" && i + 1 < argc) {
            o.renderSeconds = std::atof(argv[++i]);
        } else if (a == "--dump-regs" && i + 1 < argc) {
            o.dumpRegs = argv[++i];
        } else if (a == "--render-state" && i + 1 < argc) {
            o.renderState = argv[++i];
        } else if (a == "--gamebg" && i + 1 < argc) {
            o.gameBg = argv[++i];
        } else if (a == "--help" || a == "-h") {
            o.help = true;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            o.help = true;
        }
    }
    return o;
}

void usage() {
    std::printf(
        "tubes-port - SDL reimplementation of Tubes\n"
        "\n"
        "  --gamedir DIR     directory holding your TUBES.RES (default: .)\n"
        "  --scale N         integer scale factor (default: fit the display)\n"
        "  --screenshot FILE render one frame to a BMP and exit\n"
        "  --auto N          simulate N scripted frames first (for testing)\n"
        "  --demo            let the scripted player drive the live loop\n"
        "  --music NAME      song to play (default: TUBES.MUS)\n"
        "  --no-music        start silent\n"
        "  --render-mus NAME OUT.wav   render a song to WAV and exit\n"
        "  --seconds N       length for --render-mus (default: one pass)\n"
        "  --dump-regs NAME  print the OPL2 register stream and exit\n"
        "  --help\n"
        "\n"
        "Controls: left/right move the test tube, Down speeds the atom,\n"
        "Ctrl or Space releases a held atom into the beaker, Esc quits.\n"
        "\n"
        "You need your own copy of Tubes; no game data ships with this.\n");
}

bool loadImage(const tubes::Archive& res, const std::string& name,
               tubes::Image& out, int transparent) {
    tubes::Bytes raw;
    std::string err;
    if (!res.read(name, raw, err) || !tubes::decodeGfx(raw, out, err)) {
        std::fprintf(stderr, "  %s: %s\n", name.c_str(), err.c_str());
        return false;
    }
    out.transparent = transparent;
    return true;
}

bool loadSprite(const tubes::Archive& res, const std::string& name,
                tubes::Sprite& out) {
    tubes::Bytes raw;
    std::string err;
    if (!res.read(name, raw, err) || !tubes::decodeCsp(raw, out, err)) {
        std::fprintf(stderr, "  %s: %s\n", name.c_str(), err.c_str());
        return false;
    }
    return true;
}

// The HUD, transliterated from `1000:42ae` (the labels, drawn once at session
// setup) and `1000:5707` (the numbers, redrawn every frame).
//
//     SetFont(small);                                    { 8 x 8, advance 6 }
//     OutText(  1, 1, 127, $81, 'Chains');
//     OutText(288, 1, 127, $81, 'Drops');
//     SetFont(big);                                     { 8 x 16, advance 8 }
//     ...
//     OutTextCentred(0, 319, 0, 127, $81, Str(score));
//     OutText(32, 0, 127, $81, Str(chains:3));
//     if (drops > 0) and (drops < 255) then
//         OutText(265, 0, 127, $81, Str(drops))
//     else begin
//         SetFont(small);  OutText(270, 1, 127, $81, 'No');  SetFont(big)
//     end
//
// `$81` is the shadow bit plus mode 1, so every glyph is drawn twice: once
// flat in index 0 at (x+1, y+1), then again walking DOWN the palette one index
// per scanline from 127. The palette holds a cyan ramp at 112..127, which is
// where the HUD's colour comes from - there is no second colour constant
// anywhere. Confirmed against a captured frame: the pixels of "Chains" read
// 127, 126, 125 ... down its eight rows, exactly one per scanline.
//
// The width-3 field on `chains` is the reason its digit sits at x = 48 rather
// than 32 - the two leading spaces advance without drawing.
void drawHud(tubes::Screen& screen, const tubes::Game& game,
             const tubes::Font& big, const tubes::Font& small, bool haveBig,
             bool haveSmall) {
    constexpr uint8_t kHudColour = 127;
    constexpr uint8_t kHudMode = tubes::textmode::kShadow |
                                 tubes::textmode::kFadeDown;

    if (haveSmall) {
        tubes::drawText(screen, small, 1, 1, kHudColour, kHudMode, "Chains");
        tubes::drawText(screen, small, 288, 1, kHudColour, kHudMode, "Drops");
    }
    if (!haveBig) return;

    tubes::drawTextCentred(screen, big, 0, 319, 0, kHudColour, kHudMode,
                           std::to_string(game.score()));

    std::string chains = std::to_string(game.chains());
    while (chains.size() < 3) chains.insert(chains.begin(), ' ');
    tubes::drawText(screen, big, 32, 0, kHudColour, kHudMode, chains);

    // 1000:576f. The 255 arm is the drop counter having wrapped past zero,
    // which is the game-over condition - so "No" is on screen for the frame
    // that ends the session as well as for the last one before it.
    const int drops = game.dropsRemaining();
    if (drops > 0 && drops < 255) {
        tubes::drawText(screen, big, 265, 0, kHudColour, kHudMode,
                        std::to_string(drops));
    } else if (haveSmall) {
        tubes::drawText(screen, small, 270, 1, kHudColour, kHudMode, "No");
    }

    // The award in flight, centred under the score - `1000:582a`. Colour 168
    // with mode 3, which brightens to the middle of the cell and dims again
    // rather than ramping one way.
    if (!haveSmall || game.scorePending() <= 0) return;
    constexpr uint8_t kPopColour = 168;
    constexpr uint8_t kPopMode = tubes::textmode::kShadow |
                                 tubes::textmode::kPeak;
    tubes::drawTextCentred(screen, small, 0, 319, 13, kPopColour, kPopMode,
                           "+" + std::to_string(game.scorePending()));
    // 1000:586a - the multiplier only appears when it is worth more than one.
    if (game.scoreMultiplier() > 1) {
        tubes::drawTextCentred(screen, small, 0, 319, 21, kPopColour, kPopMode,
                               "x" + std::to_string(game.scoreMultiplier()));
    }
}

bool loadFont(const tubes::Archive& res, const std::string& name, int advance,
              int peak, tubes::Font& out) {
    tubes::Bytes raw;
    std::string err;
    if (!res.read(name, raw, err) ||
        !tubes::decodeFont(raw, advance, peak, out, err)) {
        std::fprintf(stderr, "  %s: %s\n", name.c_str(), err.c_str());
        return false;
    }
    return true;
}

uint8_t readKeyboard() {
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    uint8_t b = 0;
    if (k[SDL_SCANCODE_UP]) b |= tubes::button::kUp;
    if (k[SDL_SCANCODE_DOWN]) b |= tubes::button::kDown;
    if (k[SDL_SCANCODE_LEFT]) b |= tubes::button::kLeft;
    if (k[SDL_SCANCODE_RIGHT]) b |= tubes::button::kRight;
    if (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL] ||
        k[SDL_SCANCODE_SPACE]) {
        b |= tubes::button::kA;
    }
    if (k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT]) b |= tubes::button::kB;
    return b;
}

// A scripted player, used by --auto so the game loop can be exercised
// headless. It stacks like colours together, which is enough to drive real
// matches and cascades rather than scattering atoms at random.
uint8_t scriptedInput(const tubes::Game& game) {
    uint8_t b = tubes::button::kDown;

    if (game.heldAtom() != tubes::kEmpty) {
        const tubes::Board& board = game.board();
        int target = -1;

        // Prefer a column whose topmost atom already matches what we hold.
        for (int c = 0; c < board.cols(); ++c) {
            int free = board.dropRow(c);
            if (free < 0) continue;
            if (free + 1 < board.rows() &&
                board.at(c, free + 1) == game.heldAtom()) {
                target = c;
                break;
            }
        }
        // Otherwise use the emptiest column, to keep the beaker level.
        if (target < 0) {
            int best = -1;
            for (int c = 0; c < board.cols(); ++c) {
                int free = board.dropRow(c);
                if (free > best) {
                    best = free;
                    target = c;
                }
            }
        }
        if (target < 0) return b;

        if (game.tubeColumn() < target) return b | tubes::button::kRight;
        if (game.tubeColumn() > target) return b | tubes::button::kLeft;
        return b | tubes::button::kA;
    }

    // Nothing held: line up under an atom to catch it. Six can be in flight,
    // so go for the one furthest along - the lowest y among those already
    // coming down a play column, falling back to whatever is in the network.
    // The atom carries the original's 1..6 column numbering, whose x order is
    // 143,125,107,197,179,161 - not the board's 0..5. Steer by comparing x.
    int bestCol = 0;
    int bestRank = -1;
    for (int c = 1; c <= tubes::kAtomSlots; ++c) {
        const tubes::Falling& a = game.atom(c);
        if (!a.active()) continue;
        const int rank =
            (a.state == tubes::atomstate::kDescend ? 1000 : 0) + a.y;
        if (rank > bestRank) {
            bestRank = rank;
            bestCol = c;
        }
    }
    if (bestCol) {
        const int atomX = tubes::kAtomColumnX[game.atom(bestCol).column];
        const int tubeX = tubes::playColumnX(game.tubeColumn());
        if (atomX != tubeX) {
            return b | (atomX > tubeX ? tubes::button::kRight
                                      : tubes::button::kLeft);
        }
    }
    return b;
}

}  // namespace

// One pass of a song, in seconds. Songs loop forever, so an offline render
// needs an explicit length from somewhere.
double songSeconds(const tubes::Bytes& song) {
    std::vector<tubes::MusEvent> events;
    std::string err;
    if (!tubes::parseMus(song, events, err)) return 0.0;
    uint32_t ticks = 0;
    for (const auto& ev : events) ticks += ev.delta;
    return ticks / tubes::kMusTickHz;
}

bool writeWav(const std::string& path, const std::vector<int16_t>& samples,
              int sampleRate, std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        error = "cannot write " + path;
        return false;
    }
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
    const uint16_t channels = 2, bits = 16;
    const uint32_t byteRate = sampleRate * channels * bits / 8;
    const uint16_t blockAlign = channels * bits / 8;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);                     // PCM
    u16(channels);
    u32(static_cast<uint32_t>(sampleRate));
    u32(byteRate);
    u16(blockAlign);
    u16(bits);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    std::fwrite(samples.data(), 1, dataBytes, f);
    std::fclose(f);
    return true;
}

// Prints every register write the sequencer makes, for diffing against
// tools/mus_decode.py. If the two streams agree, the port is correct
// independently of whether the synthesis sounds right.
int dumpRegisters(const tubes::Archive& drivers, const tubes::Bytes& song,
                  std::string& err) {
    tubes::RegisterLog log;
    const int ticks = static_cast<int>(songSeconds(song) * tubes::kMusTickHz) + 2;
    if (!tubes::MusicPlayer::logRegisters(drivers, song, ticks, log, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    for (const auto& w : log.writes) {
        std::printf("%u %02x %02x\n", w.tick, w.reg, w.value);
    }
    return 0;
}

int renderSong(const tubes::Archive& drivers, const tubes::Bytes& song,
               const std::string& out, double seconds, std::string& err) {
    if (seconds <= 0.0) seconds = songSeconds(song);
    if (seconds <= 0.0) {
        std::fprintf(stderr, "error: could not determine song length\n");
        return 1;
    }
    constexpr int kRate = 44100;
    std::vector<int16_t> samples;
    if (!tubes::MusicPlayer::renderOffline(drivers, song, seconds, kRate,
                                           samples, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (!writeWav(out, samples, kRate, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("wrote %s (%.2f s, %zu frames)\n", out.c_str(), seconds,
                samples.size() / 2);
    return 0;
}

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    if (opt.help) {
        usage();
        return 0;
    }

    const std::string resPath = opt.gameDir + "/TUBES.RES";
    tubes::Archive res;
    std::string err;
    if (!res.open(resPath, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        std::fprintf(stderr,
                     "\nPoint --gamedir at a directory containing TUBES.RES "
                     "from your copy of the game.\n");
        return 1;
    }
    std::printf("opened %s (%zu resources)\n", resPath.c_str(),
                res.entries().size());

    // DRIVERS.RES holds FMMUSIC.DRV, whose tables the sequencer needs. Music
    // is optional: a missing or broken driver must not stop the game.
    tubes::Archive drivers;
    std::string driverErr;
    const bool haveDrivers =
        drivers.open(opt.gameDir + "/DRIVERS.RES", driverErr);

    if (!opt.dumpRegs.empty() || !opt.renderMus.empty()) {
        if (!haveDrivers) {
            std::fprintf(stderr, "error: %s\n", driverErr.c_str());
            return 1;
        }
        tubes::Bytes song;
        if (!res.read(opt.music, song, err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        if (!opt.dumpRegs.empty()) return dumpRegisters(drivers, song, err);
        return renderSong(drivers, song, opt.renderMus, opt.renderSeconds, err);
    }

    tubes::Bytes palRaw;
    tubes::Palette pal;
    if (!res.read("TUBES.PAL", palRaw, err) ||
        !tubes::loadPalette(palRaw, pal, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    tubes::Image background;
    tubes::Image foreground;
    bool haveBg = loadImage(res, opt.gameBg, background, -1);
    bool haveFg = loadImage(res, "GAMEFG.GFX", foreground, 0);

    // ONE table, indexed by a beaker cell's raw value. The original's is at
    // DS:0x1da6 and is indexed by `type + 19 * fadeFrame`, so the same lookup
    // serves a settled atom and a fading one and the drawing code never
    // branches on whether a cell is clearing. Frames past 6 are null and draw
    // nothing, which is exactly what the original does with them.
    tubes::Sprite atoms[kCellStates];
    bool haveAtom[kCellStates] = {};
    int loaded = 0;
    int drawable = 0;
    for (int i = 0; i < tubes::kTypeCount; ++i) {
        if (!kAtomSprites[i]) continue;
        ++drawable;
        if (loadSprite(res, kAtomSprites[i], atoms[i])) {
            haveAtom[i] = true;
            ++loaded;
        }
    }
    int fadesLoaded = 0;
    for (int frame = 1; frame <= tubes::kFadeFrames; ++frame) {
        for (int type = 0; type < tubes::kTypeCount; ++type) {
            if (!kFadeFamilies[type]) continue;
            const int slot = type + tubes::kFadeStride * frame;
            if (slot >= kCellStates) continue;
            const std::string name =
                std::string(kFadeFamilies[type]) + std::to_string(frame) + ".CSP";
            if (loadSprite(res, name, atoms[slot])) {
                haveAtom[slot] = true;
                ++fadesLoaded;
            }
        }
    }
    std::printf("loaded %d/66 fade sprites\n", fadesLoaded);
    // TESTUBE1/2/3 are the tipping animation's three frames, indexed by the
    // tube's phase - upright, tilted, pouring. Their heights of 65/42/27 are
    // a tube going over, which is what they were for all along; the port read
    // them as three difficulty capacities for several sessions, a guess the
    // flat capacity of five already contradicted without explaining.
    tubes::Sprite testTube[tubes::tubephase::kRelease + 1];
    bool haveTube[tubes::tubephase::kRelease + 1] = {false, false, false, false,
                                                     false};
    haveTube[tubes::tubephase::kUpright] =
        loadSprite(res, "TESTUBE1.CSP", testTube[tubes::tubephase::kUpright]);
    haveTube[tubes::tubephase::kTilted] =
        loadSprite(res, "TESTUBE2.CSP", testTube[tubes::tubephase::kTilted]);
    haveTube[tubes::tubephase::kPoured] =
        loadSprite(res, "TESTUBE3.CSP", testTube[tubes::tubephase::kPoured]);

    tubes::Sprite furn[kFurnCount];
    bool haveFurn[kFurnCount] = {};
    int furnLoaded = 0;
    for (int i = 0; i < kFurnCount; ++i) {
        haveFurn[i] = loadSprite(res, kFurnFiles[i], furn[i]);
        if (haveFurn[i]) ++furnLoaded;
    }
    std::printf("loaded %d/%d tube network sprites\n", furnLoaded,
                static_cast<int>(kFurnCount));
    // The beaker's interior is exactly the grid: 106 x 65, with 4px walls, so
    // it sits 4px left of column 1 and level with row 1.
    tubes::Sprite beaker;
    const bool haveBeaker = loadSprite(res, "BEAKER.CSP", beaker);

    // The two fonts the game session swaps between, with the numbers
    // `1000:42ae` and `1000:42e8` pass to `2000:3fab`: the small one for the
    // labels and the pop-ups, the large one for every number.
    //
    // Which resources they are is settled, not guessed. The small one has to
    // be `TINY6X8.88` because it is the only 8 x 8 font in the archive and the
    // setup selects a cell height of 8. The large one was identified by
    // pulling the digit `0` out of a captured HUD and comparing it against all
    // four `.816` fonts: `FUTURE.816` matches byte for byte and the other
    // three do not come close.
    tubes::Font bigFont, smallFont;
    const bool haveBig = loadFont(res, "FUTURE.816", 8, 7, bigFont);
    const bool haveSmall = loadFont(res, "TINY6X8.88", 6, 4, smallFont);
    const int tubeFrames = static_cast<int>(haveTube[1]) +
                           static_cast<int>(haveTube[2]) +
                           static_cast<int>(haveTube[3]);
    std::printf("loaded %d/%d atoms, %d/3 test tube frames\n", loaded, drawable,
                tubeFrames);

    tubes::Game game(kCols, kRows, tubes::Difficulty::k101, 0x9E3779B9u);
    game.setFallHeight(kFallHeight);
    if (!opt.renderState.empty() && !loadState(opt.renderState, game)) return 1;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    int scale = opt.scale;
    if (scale <= 0) {
        SDL_DisplayMode dm;
        scale = 3;
        if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
            int fit = std::min(dm.w / tubes::kScreenWidth,
                               dm.h / tubes::kScreenHeight);
            scale = std::max(1, std::min(fit - 1, 6));
        }
    }

    SDL_Window* win = SDL_CreateWindow(
        "Tubes", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        tubes::kScreenWidth * scale, tubes::kScreenHeight * scale,
        SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren =
        win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : nullptr;
    if (win && !ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!win || !ren) {
        std::fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         tubes::kScreenWidth,
                                         tubes::kScreenHeight);

    // Run the scripted player before drawing, so a headless screenshot shows
    // a populated beaker rather than the empty opening frame.
    for (int f = 0; f < opt.autoFrames; ++f) {
        (void)f;
        game.update(scriptedInput(game), 1.0f / 60.0f);
    }
    if (opt.autoFrames) {
        std::printf(
            "simulated %d frames: %d atoms, score %d, chains %d, drops %d/%d%s\n",
            opt.autoFrames, game.board().count(), game.score(), game.chains(),
            game.dropsRemaining(), game.startingDrops(),
            game.gameOver() ? ", GAME OVER" : "");
        for (int c = 1; c <= tubes::kAtomSlots; ++c) {
            const tubes::Falling& fa = game.atom(c);
            if (!fa.active()) continue;
            const char* stateName =
                fa.state == tubes::atomstate::kRise      ? "rise"
                : fa.state == tubes::atomstate::kGoLeft  ? "go-left"
                : fa.state == tubes::atomstate::kGoRight ? "go-right"
                : fa.state == tubes::atomstate::kDescend ? "descend"
                                                         : "?";
            std::printf("  column %d: %-8s type %2d at (%3d,%3d) -> x=%d\n",
                        c, stateName, fa.colour, fa.x, fa.y,
                        tubes::kAtomColumnX[fa.column]);
        }
    }

    // Music is best-effort: a missing DRIVERS.RES or a busy audio device
    // must not stop the game from being playable.
    tubes::MusicPlayer music;
    if (!opt.music.empty() && opt.screenshot.empty()) {
        std::string musicErr;
        tubes::Bytes song;
        if (!haveDrivers) {
            std::fprintf(stderr, "music disabled: %s\n", driverErr.c_str());
        } else if (!music.open(drivers, musicErr) ||
                   !res.read(opt.music, song, musicErr) ||
                   !music.play(song, musicErr)) {
            std::fprintf(stderr, "music disabled: %s\n", musicErr.c_str());
        } else {
            std::printf("playing %s through Nuked-OPL3\n", opt.music.c_str());
        }
    }

    tubes::Screen screen;
    std::vector<uint8_t> rgba;

    // GAMEFG on its own, kept as a buffer to stamp back over moving sprites.
    //
    // This is the game's own arrangement, from three routines in the graphics
    // unit that all write to the video segment:
    //
    //     2321:0792  opaque full-screen blit   <- the backdrop, GAMEBG
    //     2321:0711  transparent full-screen   <- GAMEFG, over it
    //     2321:0874  transparent w x h box from the buffer at DS:0x238e
    //
    // and one line of session setup: after blitting GAMEFG with 0711, it
    // stores *that same buffer pointer* at DS:0x238e. So the stamp source is
    // GAMEFG itself - not a snapshot of the composed screen.
    //
    // The distinction is the whole behaviour. An atom crossing lane 13 near
    // x=217 is clipped by the tube walls, because GAMEFG is solid there; the
    // same atom rising at x=246 is NOT, because GAMEFG is transparent inside
    // the feed tubes and the walls there come from the furniture sprites,
    // which are drawn before the atom. Composing furniture into the stamp
    // buffer clipped the second case as well and was measurably worse.
    tubes::Screen scene;
    scene.clear(0);
    if (haveFg) scene.blit(foreground);

    bool running = true;
    Uint32 last = SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN &&
                (ev.key.keysym.sym == SDLK_ESCAPE ||
                 ev.key.keysym.sym == SDLK_q)) {
                running = false;
            }
        }

        Uint32 now = SDL_GetTicks();
        float dt = static_cast<float>(now - last) / 1000.0f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;    // a stall must not teleport atoms

        // --demo drives the REAL loop with the scripted player, so the render
        // path gets exercised on every frame of a whole session rather than
        // only on the one frame --auto screenshots. That distinction matters:
        // a crash that needs both a full beaker and a live render is invisible
        // to --auto, which simulates first and draws once at the end.
        if (opt.screenshot.empty()) {
            game.update(opt.demo ? scriptedInput(game) : readKeyboard(), dt);
        }

        screen.clear(0);
        if (haveBg) screen.blit(background);
        if (haveFg) screen.blit(foreground);

        // Draw order transcribed from 1000:3a67. The atoms go BETWEEN the
        // furniture passes, not on top of them, so the solid tube pieces
        // overpaint them and the network reads as hollow glass.
        auto drawFurn = [&](int from, int to) {
            for (int i = from; i < to; ++i) {
                const FurnDraw& d = kFurniture[i];
                if (haveFurn[d.sprite]) screen.draw(furn[d.sprite], d.x, d.y);
            }
        };

        // Each of the six atom draw sites in 1000:3a67 is the same three
        // steps, and the port makes all three:
        //
        //     Stamp(scene, savedX, savedY, 16, 13)     ; erase the old box
        //     if state > 2 then
        //         Draw(x, y, ball[type])
        //         Stamp(scene, x, y, 16, 13)           ; clip to the tube
        //
        // The erase is redundant here because this renderer repaints the whole
        // screen rather than tracking dirty rectangles, but the second stamp
        // is not, and it is what a previous version was reaching for when it
        // painted a guessed TUBEH or TUBEV over each atom. The guess put pipe
        // where there was none and had to special-case bends and descents; the
        // snapshot needs no cases, because it IS the artwork.
        //
        // Note what this implies about the interleaving: with the scene
        // stamped back over every atom, an atom is clipped by all of the
        // network whatever pass it was drawn between. The interleave still
        // orders the atoms among themselves, and it is what the original does,
        // so it stays - but it is not what makes a ball look like it is inside
        // a pipe. That was the previous session's working theory and it was
        // only half right.
        // Type 8 has no sprite. `1000:48a1` rewrites its slot in the ball
        // table with one of the seven ordinary colours every fourth frame;
        // substituting the same colour at the draw is the same picture, and it
        // keeps the cell value 8 - which is what makes a Flashium always clear
        // with FFADE however it is drawn.
        //
        // Without this a Flashium is INVISIBLE: `kAtomSprites[8]` is null, so
        // every draw site skipped it. It showed up as a phantom ball in the
        // test tube, because a Multiplier fills with `Random(8) + 1` and 8 is
        // in that range.
        auto ball = [&](int8_t cell) -> int8_t {
            return cell == tubes::kFlashium ? game.flashColour() : cell;
        };

        auto drawAtom = [&](int col) {
            const tubes::Falling& a = game.atom(col);
            // `state > 2` is the original's own test, made at every one of the
            // six draw sites. States 0..2 are a free or parked slot.
            if (!a.drawn() || a.colour == tubes::kEmpty) return;
            if (!haveAtom[ball(a.colour)]) return;
            screen.draw(atoms[ball(a.colour)], a.x, a.y);
            screen.stamp(scene, a.x, a.y, kCellW, kCellH);
        };

        // The six interleave points are hard-coded in 1000:3a67, each bound to
        // one slot of the atom array - and the slot index IS the column. So an
        // atom's draw depth is fixed by its column for its whole flight, not
        // by where it happens to be:
        //
        //     column 1, column 6   |  pass 1  (16 draws)
        //     column 2, column 5   |  pass 2  (16 draws)
        //     column 3, column 4   |  pass 3  ( 8 draws)
        //
        // The pairing is the network's mirror symmetry: columns 1 and 6 are
        // fed by the outermost tubes at x = 10 and 294, which take the lowest
        // lane and cross furthest, so they are the deepest layer. 2/5 and 3/4
        // nest inside them in turn.
        //
        // GAMEFG.GFX carries much of the network, but NOT all of it: the
        // vertical pieces through the lane rows are missing from it and come
        // from these passes. A previous version removed the passes on the
        // theory that GAMEFG was the complete network; the arcs came out
        // without their verticals. Both are needed.
        drawAtom(1); drawAtom(6);
        drawFurn(0, kFurnGroup0);
        drawAtom(2); drawAtom(5);
        drawFurn(kFurnGroup0, kFurnGroup1);
        drawAtom(3); drawAtom(4);
        drawFurn(kFurnGroup1, kFurnTotal);

        // The test tube is drawn TWICE per frame from two sprite pointers held
        // in its own record, at one position, with its contents in between:
        //
        //     Draw(tube.x, tube.y, tube.sprite[4])   if tube.phase = 1
        //     ... the atoms it is holding ...
        //     ... the HUD ...
        //     Draw(tube.x, tube.y, tube.sprite[phase])
        //
        // so the contents sit between the two layers of glass. The port had
        // this arrangement already; what is new is that it is now read off the
        // draw sequence rather than reasoned from "the beaker works this way".
        //
        // `tube.x` comes straight from the six-stop table at DGROUP:0x24 -
        // 104, 122, 140, 158, 176, 194, exactly the column x minus 3 - and
        // `tube.y` is the literal 0x44 the setup writes. The pixel diff's
        // apparent 6 px offset was the capture catching the tube mid-slide, at
        // a different stop from the one the state file named; there was never
        // an offset to sweep for.
        const int tubeX = game.tubeX();
        if (haveFurn[kTestTubeShadow]) {
            screen.draw(furn[kTestTubeShadow], tubeX, kTubeY);
        }

        // The tube's contents carry their OWN positions now. They used to be
        // computed from the index, which was right at rest and impossible
        // during the tip - the animation moves the slots, and a caught atom
        // slides down to its own before that.
        for (const tubes::Falling& s : game.tubeAtoms()) {
            if (!haveAtom[ball(s.colour)]) continue;
            screen.draw(atoms[ball(s.colour)], s.x, s.y);
        }

        // `1000:5922` draws the tube from a four-entry sprite table indexed by
        // the animation phase. Only three sprites exist because phase 4 resets
        // to 1 before the frame is drawn, so it can never be the one selected.
        const int phase = game.tubePhase();
        if (haveTube[phase]) screen.draw(testTube[phase], tubeX, kTubeY);

        // Beaker shadow, then its contents, then the glass FRONT last - the
        // original draws BEAKER.CSP after the settled atoms, so the glass
        // overlaps the balls. The port used to draw it first.
        if (haveFurn[kBeakerShadow]) screen.draw(furn[kBeakerShadow], 186, 135);

        // The beaker grid, then the MARKER overlay, in `1000:598b`'s order.
        // The cell is used as the sprite index directly - that is the whole
        // point of the `type + 19 * fadeFrame` encoding, and it is why a
        // clearing atom animates with no branch anywhere in the draw.
        const tubes::Board& b = game.board();
        for (int r = 0; r < b.rows(); ++r) {
            const int y = kGridY + r * kPitchY;
            for (int c = 0; c < b.cols(); ++c) {
                // Only the BARE type 8 is substituted. A fading Flashium
                // holds `8 + 19 * frame`, whose table slot is FFADE and is
                // never rewritten.
                const tubes::Cell v = ball(b.at(c, r));
                if (v == 0 || !haveAtom[v]) continue;
                screen.draw(atoms[v], kGridX + c * kPitchX, y);
            }
        }
        // Plane C, drawn over a flagged cell at (x + 2, y + 1). Gated on the
        // wave mode in the original; the port has no waves yet, so the plane
        // is carried and drawn but nothing sets it.
        if (haveFurn[kMarker]) {
            for (int r = 0; r < b.rows(); ++r) {
                for (int c = 0; c < b.cols(); ++c) {
                    if (!b.isObjective(c, r)) continue;
                    screen.draw(furn[kMarker], kGridX + c * kPitchX + 2,
                                kGridY + r * kPitchY + 1);
                }
            }
        }

        // Records 7..12 - the atoms tipped out of the tube and falling into
        // the beaker. `1000:5c1f` runs them AFTER the grid and the MARKER
        // overlay and BEFORE `BEAKER.CSP`, so a falling atom passes in front of
        // the settled ones and behind the glass.
        for (int n = tubes::kAtomSlots + 1; n <= tubes::kAtomRecords; ++n) {
            const tubes::Falling& f = game.atom(n);
            if (!f.drawn() || !haveAtom[ball(f.colour)]) continue;
            screen.draw(atoms[ball(f.colour)], f.x, f.y);
        }

        if (haveBeaker) screen.draw(beaker, kGridX - 4, kGridY);

        drawHud(screen, game, bigFont, smallFont, haveBig, haveSmall);

        screen.toRgba(pal, rgba);
        SDL_UpdateTexture(tex, nullptr, rgba.data(), tubes::kScreenWidth * 4);

        int winW = 0, winH = 0;
        SDL_GetRendererOutputSize(ren, &winW, &winH);
        int s = std::max(1, std::min(winW / tubes::kScreenWidth,
                                     winH / tubes::kScreenHeight));
        SDL_Rect dst{(winW - tubes::kScreenWidth * s) / 2,
                     (winH - tubes::kScreenHeight * s) / 2,
                     tubes::kScreenWidth * s, tubes::kScreenHeight * s};

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        SDL_RenderPresent(ren);

        if (!opt.screenshot.empty()) {
            SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
                rgba.data(), tubes::kScreenWidth, tubes::kScreenHeight, 32,
                tubes::kScreenWidth * 4, SDL_PIXELFORMAT_RGBA32);
            if (surf) {
                SDL_SaveBMP(surf, opt.screenshot.c_str());
                SDL_FreeSurface(surf);
                std::printf("wrote %s\n", opt.screenshot.c_str());
            }
            running = false;
        }

        SDL_Delay(16);
    }

    music.stop();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
