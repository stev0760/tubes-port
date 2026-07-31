// tubes-port - an SDL reimplementation of Tubes (Absolute Magic, 1994).
//
// Ships no game data. Assets are read at runtime from the user's own copy of
// the original game; point --gamedir at the directory holding TUBES.RES.

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include "font.h"
#include "game.h"
#include "gfx.h"
#include "hiscore.h"
#include "menu.h"
#include "mus.h"
#include "opl.h"
#include "res.h"
#include "screen.h"
#include "session.h"
#include "wave_text.h"
#include "scr.h"
#include "sfx.h"

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

// The sound table, indexed exactly as the original's is - by ATOM TYPE, with
// index 0 the sound of losing one. Loaded by name at `1000:a2e0` onward; types
// 11..17 and 19 are silent, which is the same set that has no fade family.
const char* kSoundFiles[tubes::sfx::kCount] = {
    "DROP.SFX",         //  0  an atom lost, or tipped into a full column
    "RFADE.SFX",        //  1  Redium
    "GFADE.SFX",        //  2  Greenium
    "BFADE.SFX",        //  3  Bluium
    "CFADE.SFX",        //  4  Cyanium
    "PFADE.SFX",        //  5  Purplium
    "YFADE.SFX",        //  6  Yellowium
    "PNKFADE.SFX",      //  7  Pinkium
    "FFADE.SFX",        //  8  Flashium
    "AFADE.SFX",        //  9  the AntiMatter blast
    "GLDFADE.SFX",      // 10  a Bonus caught
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,  // 11..17
    "CRFADE.SFX",       // 18  the Crystal
    nullptr,            // 19  MYSTBALL is a rendering state, not a ball
    "HITGLASS.SFX",     // 20  landing on the beaker floor
    "HITATOM.SFX",      // 21  landing on another atom, and the beaker settling
    "SELECT.SFX",       // 22  the wave-mode element cycle
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

// The Task Display's half-size balls, `DS:0x200a`, loaded by name at
// `1000:ad41`. Same seven colours in the same order as the full-size table,
// and `1000:2894` is their only consumer in the game session.
const char* kSmallBallFiles[7] = {
    "SRBALL.CSP", "SGBALL.CSP", "SBBALL.CSP", "SCBALL.CSP",
    "SPBALL.CSP", "SYBALL.CSP", "SPNKBALL.CSP",
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

// DEMO.SCR is recorded at TUBES 301, not 101.
//
// `DS:0x1d4f` is the difficulty INDEX - `1000:a483` switches on it and is the
// only writer of the three constants the session runs on:
//
//     0 -> drops 9, velocity 0x100, spawn interval 0x46 (70)     Tubes 101
//     1 -> drops 6, velocity 0x180, spawn interval 0x3c (60)     Tubes 201
//     2 -> drops 3, velocity 0x200, spawn interval 0x32 (50)     Tubes 301
//
// and the menu's View Demo arm sets `[0x1d4f] := 2` at `1000:b272` before
// calling the session. It was read as a mode flag at first because the same arm
// also sets `[0x1d4e]` and `[0x1d4c]`, and because `1000:b1ee` presets the
// difficulty block to the 101 values before the menu loop - which made 101 look
// like what the demo inherits. It is not: a483 rewrites the block on entry.
//
// Confirmed live: the running demo reads 3 at the drops counter (0x245bc)
// before the session has made its first `Random` call. Only arm 2 produces a 3.
//
// This matters far more than "the demo starts with fewer lives". The interval
// sets how often an atom is dispensed and the velocity how fast it travels, so
// at 101 the port was dispensing on a 70-frame beat against a recording made on
// a 50-frame one. The recorded player was reaching for atoms that were not
// there yet - which is exactly the symptom the oracle reported.
constexpr tubes::Difficulty kDemoDifficulty = tubes::Difficulty::k301;

// DERIVED. `23e7:0024` is a vertical-retrace wait - it polls port 0x3da bit 3
// low-then-high `n` times - so `1b2e:0e37(param)`, which runs `param * 7`
// iterations of `23e7:0024(10)`, waits `param * 70` retraces. At Mode X's
// 70 Hz that is `param` SECONDS exactly, and the round number is what
// confirms the reading. The Continue screen passes 2.
const float kContinueTickSeconds = tubes::waitKeySeconds(2);

// The program's stages, in the order `1000:aaba` calls them. `kPlay` is the
// whole of `1000:9e53` - its own wave loop is a second, nested state machine
// in `session.h`. The splashes and the instructions slideshow are the gaps.
enum class Stage { kTitle, kPlay };

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
    bool dumpSfx = false;       // print every .SFX header and exit
    bool playDemo = false;      // replay DEMO.SCR through the live loop
    bool demoTrace = false;     // run DEMO.SCR headless and print the spawns
    int randomTrace = 0;        // with --demo-trace: print the first N rolls
    std::string demoCsv;        // with --demo-trace: per-frame state, for the rig
    std::string gameBg = "GAMEBG1.GFX";   // backdrop, for matching a capture
    double renderSeconds = 0;   // 0 = one pass, songs loop forever
    int wave = 0;               // 0 = Endurance; 1..75 starts Wave mode there
    int titlePage = -1;         // -1 off; 0 the bare title; 1..7 a menu page
    int hsPage = -1;            // -1 off; 0 Endurance, 1 Wave in the viewer
    // Harness only. `--screenshot` captures the first frame drawn, which can
    // never show a screen that is reached by PLAYING - the banners, the stats
    // screen and the Continue prompt are all past a game over. These two run
    // the real loop to get there instead of adding entry points that the
    // original does not have.
    int shotAfter = 0;          // present the screenshot after N live frames
    bool autoAdvance = false;   // synthesise RETURN whenever a stage waits
    uint32_t seed = 0;          // 0 = clock for play, fixed for the harnesses
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
        } else if (a == "--demo-trace") {
            o.demoTrace = true;
        } else if (a == "--random-trace" && i + 1 < argc) {
            o.demoTrace = true;
            o.randomTrace = std::atoi(argv[++i]);
        } else if (a == "--demo-csv" && i + 1 < argc) {
            o.demoTrace = true;
            o.demoCsv = argv[++i];
        } else if (a == "--play-demo") {
            o.playDemo = true;
        } else if (a == "--dump-sfx") {
            o.dumpSfx = true;
        } else if (a == "--render-state" && i + 1 < argc) {
            o.renderState = argv[++i];
        } else if (a == "--gamebg" && i + 1 < argc) {
            o.gameBg = argv[++i];
        } else if (a == "--seed" && i + 1 < argc) {
            o.seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        } else if (a == "--title") {
            // Optional page: `--title` alone is the bare title screen, and
            // `--title N` raises the menu on page N. For capturing against
            // the original, which is how the star placement got fixed.
            o.titlePage = 0;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                o.titlePage = std::atoi(argv[++i]);
            }
        } else if (a == "--hiscores") {
            // The viewer's two pages, for capturing against the original:
            // `--hiscores` is Endurance and `--hiscores 1` is Wave.
            o.hsPage = 0;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                o.hsPage = std::atoi(argv[++i]);
            }
        } else if (a == "--screenshot-after" && i + 1 < argc) {
            o.shotAfter = std::atoi(argv[++i]);
        } else if (a == "--auto-advance") {
            o.autoAdvance = true;
        } else if (a == "--wave" && i + 1 < argc) {
            o.wave = std::atoi(argv[++i]);
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
        "  --screenshot-after N  with it, capture after N frames of the LIVE\n"
        "                    loop at a fixed step - the only way to reach a\n"
        "                    screen that is past a game over\n"
        "  --auto-advance    press RETURN periodically, so a headless run\n"
        "                    walks through the screens that hold for a key\n"
        "  --auto N          simulate N scripted frames first (for testing)\n"
        "  --demo            let the scripted player drive the live loop\n"
        "  --music NAME      song to play (default: TUBES.MUS)\n"
        "  --no-music        start silent\n"
        "  --render-mus NAME OUT.wav   render a song to WAV and exit\n"
        "  --seconds N       length for --render-mus (default: one pass)\n"
        "  --dump-regs NAME  print the OPL2 register stream and exit\n"
        "  --dump-sfx        print every .SFX header and exit\n"
        "  --play-demo       replay DEMO.SCR through the live game loop\n"
        "  --demo-trace      run DEMO.SCR headless and print every spawn\n"
        "  --demo-csv FILE   with it, write per-frame state for the rig diff\n"
        "  --wave N          start Wave mode on wave N (1..75) instead of\n"
        "                    Endurance. There is no briefing screen yet, and\n"
        "                    the marked-atom, crystal and pre-filled waves\n"
        "                    cannot be finished until their setup routines are\n"
        "                    decompiled - see PLAN.md.\n"
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

// The briefing screen, `1000:86b8`. The dispatch half lives in wave.cpp; this
// is its presentation, and every coordinate here is a literal the original
// pushes - see wave_text.cpp.
//
//     load GAMEBG<Random(10)+1>, re-rolled until it differs from the last
//     SetFont(big);   OutTextCentred(0, 319, 45, 159, 3, 'Wave ' + Str(n))
//                     OutTextCentred(0, 319, 48, 159, 3, '____________')
//     SetFont(small); <the objective routine's lines and illustration>
//                     OutText(76, 150, 155, 1, 'You are allowed N drops.')
//     wait for a key
//
// The one thing not settled from code is the justification of the padded
// fields. Turbo Pascal's `:width` right-justifies and `Str(n:2)` clearly does,
// so the colour names are right-justified here too; no capture of an original
// briefing has been taken to confirm it.
std::string padLeft(const std::string& s, size_t w) {
    return s.size() >= w ? s : std::string(w - s.size(), ' ') + s;
}

// `1b2e:0a11`, the classroom scene. Called by the briefing `1000:86b8`, the
// stats screen `1000:8da5` AND the Continue screen `1000:8c38` - one routine
// behind all three, which is why they share a look.
//
// `2321:060b(x, y, w, h, colour)` is a filled rect; the argument order comes
// off `1b2e:097a`, which passes its own two parameters into the same slots the
// fixed call fills with 0x4a and 0x1f. The blackboard is blitted at
// `(0, 12)`, which `2321:068d`'s `[BP+0xe]` (multiplied by 80, the Mode X
// plane pitch) settles - and `BLACKBRD.GFX` is 48644 bytes, exactly
// 320 x 152 plus a header, so it lands on rows 12..163.
struct SceneArt {
    const tubes::Image* corners = nullptr;   // UL, UR, DL, DR
    const bool* haveCorner = nullptr;
    const tubes::Image* pointer = nullptr;   // POINTER0..3
    const bool* havePointer = nullptr;
    const tubes::Image* books = nullptr;
    bool haveBooks = false;
    const tubes::Image* bar = nullptr;
    bool haveBar = false;
};

void drawScene(tubes::Screen& screen, const tubes::Image* board, bool haveBoard,
               const SceneArt& art, int slideX, int slideY, int profFrame) {
    const tubes::Image* corners = art.corners;
    const bool* haveCorner = art.haveCorner;
    screen.clear(0);
    // `1000:8db4` / the briefing: the held image goes to (0, 12), NOT to the
    // origin. The port drew it at (0, 0) for several sessions and the pixel
    // diff never caught it, because every region ever measured was INSIDE the
    // white slide - where the two agree by construction.
    if (haveBoard) screen.blit(*board, 0, tubes::kBoardY);

    uint8_t* px = screen.pixelsMutable();
    auto fillRect = [&](int x, int y, int w, int h, uint8_t colour) {
        for (int yy = y; yy < y + h; ++yy) {
            if (yy < 0 || yy >= tubes::kScreenHeight) continue;
            for (int xx = x; xx < x + w; ++xx) {
                if (xx < 0 || xx >= tubes::kScreenWidth) continue;
                px[static_cast<size_t>(yy) * tubes::kScreenWidth + xx] = colour;
            }
        }
    };

    // `1b2e:0656` runs BEFORE the frame and slide, so the professor, his books
    // and the roller bar go down first - and the frame paints over none of
    // them, because it spans x 62..257 and he stands at 267.
    // The professor is TWO draws, and the sprite sizes are what say so:
    // POINTER0 is 44 x 79 - the whole figure, legs and book stack - while
    // POINTER1..3 are 44 x 39, his upper body only. `1b2e:0656` lays down
    // POINTER0 masked (`2321:0711`), and then `1b2e:0e37` stamps the wave
    // frame OPAQUELY (`2321:068d`) over his top half once every ten retraces.
    // Drawing only the wave frame erases him from the waist down; drawing it
    // masked leaves the base pose's arm showing through it.
    //
    // No separate BOOKS.GFX draw: the normal arm never reaches one. `BOOKS` is
    // used by the clap and jump arms, where he stands at a different height.
    if (art.havePointer && art.havePointer[0]) {
        screen.blit(art.pointer[0], tubes::kProfX, tubes::kProfY);
    }
    if (art.havePointer && profFrame > 0 && profFrame < 4 &&
        art.havePointer[profFrame]) {
        screen.blit(art.pointer[profFrame], tubes::kProfX, tubes::kProfY);
    }

    // The frame behind the slide, constant in every call.
    fillRect(tubes::kFrameX, tubes::kFrameY, tubes::kFrameW, tubes::kFrameH,
             tubes::kFrameColour);
    // The roller bar rides the frame's bottom edge - `Draw(57, DS:0xbba + 26)`
    // in `1b2e:0656`, where `DS:0xbba` is the frame height as it rolls down.
    if (art.haveBar) {
        screen.blit(*art.bar, tubes::kBarX, tubes::kFrameH + tubes::kBarDY);
    }
    // The slide itself. Its resting place, (74, 31, 172, 132, 17), is exactly
    // the rectangle this file used to carry as "MEASURED, NOT DECOMPILED" -
    // the measurement was right, and it is now derived.
    fillRect(slideX, slideY, tubes::kSlideW, tubes::kSlideH, tubes::kSlideColour);

    // Four 4x4 corner clips, `UL`/`UR`/`DL`/`DRCORNER.GFX`, each 20 bytes = a
    // 4-byte header plus 4 x 4. `1b2e:097a` places them at the slide's origin
    // plus (0,0), (168,0), (0,128) and (168,128).
    const int cx[4] = {slideX, slideX + tubes::kCornerDX, slideX, slideX + tubes::kCornerDX};
    const int cy[4] = {slideY, slideY, slideY + tubes::kCornerDY, slideY + tubes::kCornerDY};
    for (int i = 0; i < 4; ++i) {
        if (haveCorner[i]) screen.blit(corners[i], cx[i], cy[i]);
    }
}

void drawBriefing(tubes::Screen& screen, const tubes::Game& game,
                  const tubes::Image* bg, bool haveBg,
                  const tubes::Font& big, const tubes::Font& small,
                  bool haveBigF, bool haveSmallF,
                  const tubes::Sprite* atoms, const bool* haveAtom,
                  const tubes::Sprite* furn, const bool* haveFurn,
                  int8_t decorBall, const SceneArt& art, int slideX,
                  int slideY, int profFrame) {
    drawScene(screen, bg, haveBg, art, slideX, slideY, profFrame);

    const tubes::WaveObjective& obj = game.objective();
    const int count = obj.counter;
    const int8_t colour = obj.reqColour >= 1 && obj.reqColour <= 7
                              ? obj.reqColour
                              : static_cast<int8_t>(1);
    const std::string name = tubes::kElementNames[colour];

    if (haveBigF) {
        tubes::drawTextCentred(screen, big, 0, 319, tubes::kBriefTitleY,
                               tubes::kBriefTitleColour, tubes::kBriefTitleMode,
                               tubes::kBriefTitle +
                                   std::to_string(game.progress().wave));
        tubes::drawTextCentred(screen, big, 0, 319, tubes::kBriefRuleY,
                               tubes::kBriefTitleColour, tubes::kBriefTitleMode,
                               tubes::kBriefRule);
    }
    if (!haveSmallF) return;

    const tubes::Briefing& b =
        tubes::briefingFor(tubes::objectiveForWave(game.progress().wave));
    for (int i = 0; i < b.lineCount; ++i) {
        const tubes::BriefLine& l = b.lines[i];
        std::string s;
        switch (l.fmt) {
            case tubes::BriefFmt::kLiteral:  s = l.a; break;
            case tubes::BriefFmt::kCount:
                s = std::string(l.a) + padLeft(std::to_string(count), 2) + l.b;
                break;
            case tubes::BriefFmt::kCountName:
                s = std::string(l.a) + padLeft(std::to_string(count), 2) + l.b +
                    padLeft(name, 9) + l.c;
                break;
            case tubes::BriefFmt::kName:
                s = std::string(l.a) + padLeft(name, 9) + l.b;
                break;
            case tubes::BriefFmt::kNameWide:
                s = std::string(l.a) +
                    padLeft(tubes::kElementNames[obj.disabledColour >= 1 &&
                                                         obj.disabledColour <= 7
                                                     ? obj.disabledColour
                                                     : 1],
                            10) +
                    l.b;
                break;
            case tubes::BriefFmt::kNameOnly: s = name; break;
        }
        if (l.x < 0) {
            tubes::drawTextCentred(screen, small, 0, 319, l.y, l.colour, l.mode, s);
        } else {
            tubes::drawText(screen, small, l.x, l.y, l.colour, l.mode, s);
        }
    }

    for (int i = 0; i < b.ballCount; ++i) {
        const tubes::BriefBall& ball = b.balls[i];
        int8_t type = colour;
        switch (ball.kind) {
            case tubes::BriefBallKind::kRequired: type = colour; break;
            case tubes::BriefBallKind::kCrystal:  type = tubes::kCrystal; break;
            case tubes::BriefBallKind::kRandom:   type = decorBall; break;
            case tubes::BriefBallKind::kMarker:
                if (haveFurn[kMarker]) screen.draw(furn[kMarker], ball.x, ball.y);
                continue;
        }
        if (type >= 1 && type < tubes::kTypeCount && haveAtom[type]) {
            screen.draw(atoms[type], ball.x, ball.y);
        }
    }

    tubes::drawText(screen, small, tubes::kBriefDropsX, tubes::kBriefDropsY,
                    tubes::kBriefBodyColour, 1,
                    std::string(tubes::kBriefDropsA) +
                        std::to_string(game.dropsRemaining()) +
                        tubes::kBriefDropsB);
}

// The Task Display, `1000:2a4a` - the small ball and the number in the top
// left corner that say what the current wave wants.
//
//     if (counter <> 0) and not mysteryHidden then begin
//       case waveMode of
//         4: Draw(ball[taskColour], 10, 6);
//         5: Draw(crystal, 10, 6);
//         6: Draw(ball[taskColour], 10, 6); Draw(marker, 11, 8)
//         else Draw2894(...)                  { modes 2 and 3 }
//       end;
//       n := counter;
//       if      n <  10 then OutText(Str(n), mode 1, 127, y 10, x 10)
//       else if n <= 99 then OutText(Str(n), mode 1, 127, y 10, x  6)
//       else                 OutText(Str(n), mode 1, 127, y 10, x  2)
//     end
//
// The three x values are 10, 6 and 2 - a step of 4, which is half the big
// font's advance, so the number is CENTRED about x = 14 rather than moved.
//
// `1000:2894`, the modes 2 and 3 arm, is not decompiled: it draws the chain
// illustration that shows which orientation is wanted. Left undrawn rather
// than invented, so those waves show their count and nothing else.

// The end-of-session banner, `1000:5d64`. It is drawn by `1000:3a67` itself,
// over whatever the play field was left showing, rather than on a fresh
// screen - so the caller must NOT clear first.
void drawBanner(tubes::Screen& screen, tubes::Banner banner,
                const tubes::Font& heading, bool haveHeading,
                const tubes::Font& small, bool haveSmall, bool showHint) {
    if (banner == tubes::Banner::kNone || !haveHeading) return;
    const tubes::BannerText t = tubes::bannerText(banner);
    tubes::drawTextCentred(screen, heading, 0, 319, tubes::kBannerY,
                           tubes::kBannerColour, tubes::kBannerMode, t.line1);
    tubes::drawTextCentred(screen, heading, 0, 319, tubes::kBannerRuleY,
                           tubes::kBannerColour, tubes::kBannerMode, t.rule);
    // `1000:5ed7`, only on the abort arm and only when saving is enabled.
    if (showHint && haveSmall) {
        tubes::drawTextCentred(screen, small, 0, 319, tubes::kAbortHintY,
                               tubes::kBannerColour, tubes::kAbortHintMode,
                               tubes::kAbortHint);
    }
}

// The stats screen, `1000:8da5`. The rows are built once, when the screen is
// entered, because building them is what accumulates the running chain total -
// see `buildStatsScreen`. This function only draws what it is given.
void drawStats(tubes::Screen& screen, const std::vector<tubes::StatsRow>& rows,
               const tubes::Image* board, bool haveBoard, const SceneArt& art,
               const tubes::Font& heading, const tubes::Font& label,
               const tubes::Font& number, bool haveHeading, bool haveLabel,
               bool haveNumber, int profFrame) {
    // `1000:8db4` blits the HELD image through `2321:068d` at (0, 12) and then
    // calls `1b2e:0a11`, the same classroom scene the briefing uses - so the
    // stats land on the blackboard's white slide, not on the play backdrop.
    // The held image is `BLACKBRD.GFX`: 48644 bytes is exactly 320 x 152 plus
    // a header, and 12 + 152 = 164 is the only placement that fits.
    //
    // The slide is always at rest here - the drop animation belongs to the
    // first briefing and `DS:0x210e` has long since been set by this point.
    drawScene(screen, board, haveBoard, art, tubes::kSlideX, tubes::kSlideY,
              profFrame);

    for (const tubes::StatsRow& r : rows) {
        const tubes::Font* f = nullptr;
        switch (r.font) {
        case tubes::StatsFont::kHeading: if (haveHeading) f = &heading; break;
        case tubes::StatsFont::kLabel:   if (haveLabel)   f = &label;   break;
        case tubes::StatsFont::kNumber:  if (haveNumber)  f = &number;  break;
        }
        if (!f) continue;
        tubes::drawTextCentred(screen, *f, 0, 319, r.y, r.colour, r.mode,
                               r.text);
    }
}

// The Continue screen, `1000:8c38`. Drawn over whatever is already there - the
// original never clears, which is why the stats screen stays behind it.
void drawContinue(tubes::Screen& screen, int ticksLeft,
                  const tubes::Font& heading, bool haveHeading,
                  const tubes::Font& number, bool haveNumber) {
    if (haveHeading) {
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kContinueTitleY,
                               tubes::kContinueTitleColour, tubes::textmode::kPeak,
                               "Continue");
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kContinueRuleY,
                               tubes::kContinueTitleColour, tubes::textmode::kPeak,
                               "______");
    }
    if (haveNumber) {
        tubes::drawTextCentred(screen, number, 0, 319, tubes::kContinueCountY,
                               tubes::kContinueCountColour,
                               tubes::textmode::kFadeUp,
                               std::to_string(ticksLeft));
    }
}

// `2321:060b(10, 37, 299, 118, 111)`, the panel both high-score screens put
// over the chalkboard. It is what hides the equations chalked into
// `BLACKBRD.GFX`, so the board reads as blank behind the table.
void fillHiScorePanel(tubes::Screen& screen) {
    uint8_t* px = screen.pixelsMutable();
    for (int y = tubes::kHsPanelY; y < tubes::kHsPanelY + tubes::kHsPanelH; ++y) {
        if (y < 0 || y >= tubes::kScreenHeight) continue;
        for (int x = tubes::kHsPanelX;
             x < tubes::kHsPanelX + tubes::kHsPanelW; ++x) {
            if (x < 0 || x >= tubes::kScreenWidth) continue;
            px[static_cast<size_t>(y) * tubes::kScreenWidth + x] =
                tubes::kHsPanelColour;
        }
    }
}

// `Str(score:10)`: right-justified in ten characters, which is what puts the
// digits' right edge in the same column on every row.
std::string hiScoreScoreText(uint32_t score) {
    std::string s = std::to_string(score);
    if (static_cast<int>(s.size()) < tubes::kHsScoreWidth) {
        s.insert(s.begin(),
                 tubes::kHsScoreWidth - static_cast<int>(s.size()), ' ');
    }
    return s;
}

// The high-score entry screen, `1000:96db`. It draws over the classroom scene
// the stats screen left up - the original re-blits the held image and the
// roller bar and then puts a panel over them, so the caller supplies the same
// background it always does.
void drawHiScores(tubes::Screen& screen, const tubes::HiScoreBankData& bank,
                  const tubes::Font& heading, bool haveHeading,
                  const tubes::Font& body, bool haveBody, int editRow,
                  const std::string& editName, int cursorPhase) {
    uint8_t* px = screen.pixelsMutable();
    fillHiScorePanel(screen);

    if (haveHeading) {
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kHsTitleY,
                               tubes::kHsTitleColour, tubes::textmode::kPeak,
                               tubes::kHsTitle);
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kHsRuleY,
                               tubes::kHsTitleColour, tubes::textmode::kPeak,
                               tubes::kHsRule);
    }
    if (!haveBody) return;

    for (int i = 1; i <= tubes::kHiScoreShown; ++i) {
        const int y = tubes::hiScoreRowY(i);
        const tubes::HiScoreEntry& e = bank.rows[i - 1];
        // The row being typed shows the live text, not what is in the table.
        const std::string name = (i == editRow) ? editName : e.name;
        tubes::drawText(screen, body, tubes::kHsNameX, y, tubes::kHsRowColour,
                        tubes::textmode::kPeak, name);
        tubes::drawText(screen, body, tubes::kHsScoreX, y, tubes::kHsRowColour,
                        tubes::textmode::kPeak, hiScoreScoreText(e.score));

        // `1000:9757`: a 4 x 4 block just past the last character, its colour
        // walking 0x91..0x9e.
        if (i == editRow && cursorPhase > 0) {
            const int cx = static_cast<int>(name.size()) * 8 + tubes::kHsCursorDX;
            const int cy = y + tubes::kHsCursorDY;
            const uint8_t col =
                static_cast<uint8_t>(tubes::kHsCursorBase + cursorPhase);
            for (int yy = cy; yy < cy + tubes::kHsCursorSize; ++yy) {
                if (yy < 0 || yy >= tubes::kScreenHeight) continue;
                for (int xx = cx; xx < cx + tubes::kHsCursorSize; ++xx) {
                    if (xx < 0 || xx >= tubes::kScreenWidth) continue;
                    px[static_cast<size_t>(yy) * tubes::kScreenWidth + xx] = col;
                }
            }
        }
    }
}

// The high score VIEWER, `1b2e:61b6` - the menu item. ONE bank per page, the
// same panel and rows as the entry screen, and a heading that names the mode.
//
// No professor and no projector slide: the function draws the board, the
// panel, the ten rows, the roller bar and the title, and nothing else. The
// clap is a SOUND, played and replayed by the wait loop; `1b2e:0656`'s clap
// ANIMATION is a different thing and does not belong here.
void drawHiScoreViewer(tubes::Screen& screen, const tubes::HiScoreBankData& bank,
                       const char* title, const tubes::Image* board,
                       bool haveBoard, const tubes::Image* bar, bool haveBar,
                       const tubes::Font& heading, bool haveHeading,
                       const tubes::Font& script, bool haveScript) {
    screen.clear(0);
    // `2321:068d(0, 12, DS:0x2058)` - the classroom's own blackboard, at the
    // classroom's own offset.
    if (haveBoard) screen.blit(*board, 0, tubes::kBoardY);
    // ...and then the panel over it, which is why the chalked equations do not
    // show through.
    fillHiScorePanel(screen);

    if (haveScript) {
        for (int i = 1; i <= tubes::kHiScoreShown; ++i) {
            const int y = tubes::hiScoreRowY(i);
            const tubes::HiScoreEntry& e = bank.rows[i - 1];
            tubes::drawText(screen, script, tubes::kHsNameX, y,
                            tubes::kHsRowColour, tubes::textmode::kPeak,
                            e.name);
            tubes::drawText(screen, script, tubes::kHsScoreX, y,
                            tubes::kHsRowColour, tubes::textmode::kPeak,
                            hiScoreScoreText(e.score));
        }
    }

    // `2321:0711(57, 26, DS:0x2060)`: the roller bar, masked, at the top of the
    // panel - the same bar the classroom scene rides down the slide's edge,
    // parked here at its fully-drawn height.
    if (haveBar) {
        screen.blit(*bar, tubes::kHsViewBarX, tubes::kHsViewBarY);
    }
    if (haveHeading) {
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kHsViewTitleY,
                               tubes::kHsViewTitleColour,
                               tubes::textmode::kPeak, title);
    }
}

// The pause overlay, `1000:3916`. Same two rows as a banner, and the loop is
// blocked entirely while it is up.
void drawPaused(tubes::Screen& screen, const tubes::Font& heading,
                bool haveHeading) {
    if (!haveHeading) return;
    tubes::drawTextCentred(screen, heading, 0, 319, tubes::kBannerY,
                           tubes::kBannerColour, tubes::textmode::kPeak,
                           "Game Paused");
    tubes::drawTextCentred(screen, heading, 0, 319, tubes::kBannerRuleY,
                           tubes::kBannerColour, tubes::textmode::kPeak,
                           "_________");
}

// The title screen, `1b2e:52bf`, and its menu, `1b2e:4d80`.
//
// `TUBESBG.GFX` and `TUBESFG.GFX` are a background/foreground pair - the word
// TUBES drawn as a connected tube network - and the atom travels INSIDE the
// pipes. That works here for the same reason it works on the play field: the
// atom is drawn, then a 16 x 13 box of the foreground is stamped back over it,
// so the pipe walls come back on top and the ball shows through only where the
// foreground is transparent. Same routine, `2321:0874`, same 16 x 13 box.
void drawTitle(tubes::Screen& screen, const tubes::Image& bg,
               const tubes::Screen& fgScene, bool haveArt,
               const tubes::Menu& menu, const tubes::TitleAtom& atom,
               const tubes::Sprite* atoms, const bool* haveAtom, int atomBall,
               const tubes::Image* stars, const bool* haveStar,
               const tubes::Font& big, bool haveBig,
               const tubes::Image& fg, const tubes::Font& small,
               bool haveSmall) {
    screen.clear(0);
    if (haveArt) {
        screen.blit(bg);
        // `1b2e:5754`: a full-screen MASKED blit of the foreground,
        // `2321:0711(0, 0, fg, 320, 200)`. Without it the pipe walls are
        // simply absent - the network reads as a flat silhouette, which is
        // what happened when only the atom's own box was stamped.
        screen.blit(fg);
    }

    // `1b2e:5780`, drawn once under the artwork with the SMALL font, which
    // `1b2e:576b` selects just before it. Mode 3, no shadow bit.
    if (haveSmall) {
        tubes::drawTextCentred(screen, small, 0, 319, 190, 157, 3,
                               "Copyright 1994 Absolute Magic");
        tubes::drawText(screen, small, 294, 190, 157, 3, "v1.0");
    }

    // The atom, then the foreground back over its box.
    if (atomBall >= 1 && atomBall < tubes::kTypeCount && haveAtom[atomBall]) {
        screen.draw(atoms[atomBall], atom.x, atom.y);
    }
    if (haveArt) screen.stamp(fgScene, atom.x, atom.y, 16, 13);

    if (!menu.up() || !haveBig) return;

    const tubes::Page p = menu.page();
    const tubes::MenuPage& page = tubes::kMenuPages[static_cast<int>(p)];

    // `1b2e:4743`. The title is drawn only when the page has one - page 1's is
    // empty - with its rule two rows below.
    if (page.title[0]) {
        tubes::drawTextCentred(screen, big, 0, 319, tubes::menuTitleY(p),
                               tubes::kMenuTitleColour, tubes::kMenuTitleMode,
                               page.title);
        tubes::drawTextCentred(screen, big, 0, 319, tubes::menuRuleY(p),
                               tubes::kMenuTitleColour, tubes::kMenuTitleMode,
                               tubes::menuRule(p));
    }

    // Every item in one colour: the SELECTION is marked by the stars alone,
    // which is why there is no highlight colour here.
    for (int i = 1; i <= page.count; ++i) {
        tubes::drawTextCentred(screen, big, 0, 319, tubes::menuItemY(p, i),
                               tubes::kMenuItemColour, tubes::kMenuItemMode,
                               menu.itemText(i));
    }

    const tubes::StarPlacement s = tubes::placeStars(p, menu.item());
    const int f = menu.starFrame();
    if (f >= 1 && f <= tubes::kStarFrames && haveStar[f]) {
        screen.blit(stars[f], s.xLeft, s.y);
        screen.blit(stars[f], s.xRight, s.y);
    }
}

void drawTaskDisplay(tubes::Screen& screen, const tubes::Game& game,
                     const tubes::Font& big, bool haveBig,
                     const tubes::Sprite* atoms, const bool* haveAtom,
                     const tubes::Sprite* furn, const bool* haveFurn,
                     const tubes::Sprite* smallBall, const bool* haveSmallBall) {
    const tubes::WaveObjective& obj = game.objective();
    if (!tubes::isWaveMode(obj.mode)) return;
    if (obj.counter == 0 || obj.mysteryHidden) return;

    // Flashium has no sprite of its own - the original rewrites its table slot
    // every fourth frame - and the Task Display's colour is pointed at that
    // same cycling value whenever the wave names no colour, so this ball
    // cycles for exactly the waves the original's does.
    const int8_t taskColour = game.taskDisplay().colour;
    const int8_t ball = taskColour == tubes::kFlashium ? game.flashColour()
                                                       : taskColour;
    switch (obj.mode) {
        case tubes::WaveMode::kSurvive:
            if (haveAtom[ball]) screen.draw(atoms[ball], 6, 10);
            break;
        case tubes::WaveMode::kCrystals:
            if (haveAtom[tubes::kCrystal]) {
                screen.draw(atoms[tubes::kCrystal], 6, 10);
            }
            break;
        case tubes::WaveMode::kMarked:
            if (haveAtom[ball]) screen.draw(atoms[ball], 6, 10);
            if (haveFurn[kMarker]) screen.draw(furn[kMarker], 8, 11);
            break;
        default:
            // Modes 2 and 3: `1000:2894` lays three half-size balls out in the
            // shape of the required chain. Pitch 7 across and 5 down, which is
            // what an 8x7 sprite wants.
            if (ball >= 1 && ball <= 7 && haveSmallBall[ball]) {
                const tubes::Sprite& s = smallBall[ball];
                const uint8_t chain = game.taskDisplay().chain;
                if (chain == tubes::chaincode::kVertical) {
                    screen.draw(s, 10, 9);
                    screen.draw(s, 10, 14);
                    screen.draw(s, 10, 19);
                } else if (chain == tubes::chaincode::kHorizontal) {
                    screen.draw(s, 3, 14);
                    screen.draw(s, 10, 14);
                    screen.draw(s, 17, 14);
                } else {
                    // Both diagonals count, so the picture alternates between
                    // them every flash tick rather than committing to one.
                    if (!game.taskDisplay().diagonalFlip) {
                        screen.draw(s, 3, 9);
                        screen.draw(s, 17, 19);
                    } else {
                        screen.draw(s, 17, 9);
                        screen.draw(s, 3, 19);
                    }
                    screen.draw(s, 10, 14);
                }
            }
            break;
    }

    if (!haveBig) return;
    const std::string n = std::to_string(obj.counter);
    const int x = obj.counter < 10 ? 10 : (obj.counter <= 99 ? 6 : 2);
    tubes::drawText(screen, big, x, 10, 127, tubes::textmode::kFadeDown, n);
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

// An SDL keycode as the original's `ReadKey` would have reported it, so
// `classifyGameKey` can be the game's own dispatch rather than an SDL one.
// Turbo Pascal returns #0 then the scancode for an extended key and
// `2000:7823` folds that pair into `0x80 + scancode`, which is why F1..F5 are
// 0xbb..0xbf and not 0x3b..0x3f.
uint8_t originalKeyCode(SDL_Keycode k) {
    switch (k) {
    case SDLK_ESCAPE: return tubes::gamekey::kEsc;
    case SDLK_F1: return tubes::gamekey::kF1;
    case SDLK_F2: return tubes::gamekey::kF2;
    case SDLK_F3: return tubes::gamekey::kF3;
    case SDLK_F4: return tubes::gamekey::kF4;
    case SDLK_F5: return tubes::gamekey::kF5;
    default: return 0;
    }
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

    // Prints every .SFX header the way `tools/sfx_decode.py INFO` does, so the
    // two decoders can be diffed. Music is verified by diffing its register
    // stream against the Python tool rather than by listening; this is the same
    // check for the digital side, and it is what caught the rate being a WORD.
    if (opt.dumpSfx) {
        for (const auto& kv : res.entries()) {
            const std::string& name = kv.first;
            if (name.size() < 4 ||
                name.compare(name.size() - 4, 4, ".SFX") != 0) {
                continue;
            }
            tubes::Bytes raw;
            tubes::Sound snd;
            std::string sfxErr;
            if (!res.read(name, raw, sfxErr) ||
                !tubes::decodeSfx(raw, snd, sfxErr)) {
                std::printf("  %-16s ERROR %s\n", name.c_str(),
                            sfxErr.c_str());
                continue;
            }
            std::printf("  %-16s %5d Hz %7zu samples  \"%s\"\n", name.c_str(),
                        snd.rate, snd.pcm.size(), snd.name.c_str());
        }
        return 0;
    }

    // Runs DEMO.SCR at full speed with no window and prints every atom the
    // dispenser rolls. This is the regression oracle the prime directive asks
    // for, and the spawn sequence is the sharpest form of it: the recording
    // stores only the player's buttons, so which colour appears in which column
    // is decided entirely by `Random` - by the generator AND by how many times
    // each frame calls it. Nothing else in the port cross-checks that.
    //
    // It is also robust to timing. A trace captured off the original by polling
    // its memory cannot be aligned frame for frame, but the Nth atom it
    // dispenses is the Nth either way.
    if (opt.demoTrace) {
        tubes::Bytes raw;
        tubes::Demo dm;
        if (!res.read("DEMO.SCR", raw, err) || !tubes::decodeScr(raw, dm, err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        std::printf("# DEMO.SCR seed 0x%08x, %zu frames\n", dm.seed,
                    dm.input.size());
        // What the recorded player is actually holding. The Down/B boost is
        // the difference between a 4 px/frame traverse and a 9 px/frame one,
        // so how often it is pressed sets how long a record stays occupied -
        // and that decides how often the spawn re-rolls its column.
        {
            static const char* kNames[6] = {"Up", "Down", "Left", "Right",
                                            "A(tip)", "B"};
            static const uint8_t kBits[6] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20};
            const size_t win = std::min<size_t>(dm.input.size(), 250);
            std::printf("# first %zu recorded inputs held:", win);
            for (int b = 0; b < 6; ++b) {
                size_t held = 0;
                for (size_t f = 0; f < win; ++f)
                    if (dm.input[f] & kBits[b]) ++held;
                std::printf("  %s=%zu", kNames[b], held);
            }
            std::printf("\n");
        }
        // The rig's exp18_random_calls.py logs the same two fields off the
        // original, breaking on `Random`'s own entry, so the first index where
        // the two disagree is the divergence - and the original's log names the
        // call site that produced it.
        std::vector<std::pair<int, uint32_t>> rolls;
        // Per-frame state, in the form the rig diffs against the original.
        // `idx` is the key that matters: the original's demo reader keeps its
        // stream index at `24c1:001e` (linear 0x24c2e) and `read` increments
        // it, so the two sides can be aligned on the byte being consumed
        // rather than on any notion of time.
        std::FILE* csv = nullptr;
        if (!opt.demoCsv.empty()) {
            csv = std::fopen(opt.demoCsv.c_str(), "w");
            if (!csv) {
                std::fprintf(stderr, "cannot write %s\n", opt.demoCsv.c_str());
                return 1;
            }
            std::fprintf(csv, "frame,idx,btn,tubex,tubestop,tubebusy,rolls,drops,score,tubecount,ramp,interval,vel,runs,atoms,grid\n");
        }
        // Always collected: the per-spawn roll count below is the comparison
        // that matters, and it is cheap. `--random-trace N` only controls how
        // many individual rolls get printed.
        tubes::Game g(kCols, kRows, kDemoDifficulty, dm.seed, &rolls);
        bool wasActive[tubes::kAtomRecords + 1] = {};
        int born[tubes::kAtomRecords + 1] = {};
        int drops = g.dropsRemaining();
        int spawns = 0;
        // One byte per IDLE frame - see Game::acceptsInput. The frame count is
        // therefore larger than the byte count, so the loop ends when the
        // recording is exhausted rather than after input.size() frames.
        size_t idx = 0;
        for (size_t f = 0; idx < dm.input.size() && !g.gameOver(); ++f) {
            const uint8_t btn = g.acceptsInput() ? dm.input[idx++] : 0;
            const int wasBusy = g.acceptsInput() ? 0 : 1;
            g.stepOnce(btn);
            if (csv) {
                std::fprintf(csv, "%zu,%zu,%u,%d,%d,%d,%zu,%d,%d,%d,", f, idx,
                             btn, g.tubeX(), g.tubeColumn() + 1, wasBusy,
                             rolls.size(), g.dropsRemaining(), g.score(),
                             static_cast<int>(g.tubeAtoms().size()));
                std::fprintf(csv, "%d,%d,%d,%d,", g.rampCounterForTest(),
                             g.spawnIntervalForTest(), g.networkVelForTest(),
                             g.runsThisFrameForTest());
                // The six network records: state and y, which is what a catch
                // turns on.
                for (int c = 1; c <= tubes::kAtomSlots; ++c) {
                    std::fprintf(csv, "%d:%d;", g.atom(c).state, g.atom(c).y);
                }
                std::fprintf(csv, ",");
                // The beaker, in the original's own row-major order, so the two
                // sides diff cell for cell. A catch that goes the other way
                // shows up here long before it shows up in the score.
                // The RAW cell, `type + 19*fadeFrame`, which is what the
                // original's beaker plane holds. Writing typeAt() here instead
                // strips the fade and makes every clearing cell look like an
                // unmatched one - it produced a confident false report of the
                // matcher missing a diagonal.
                for (int r = 0; r < g.board().rows(); ++r) {
                    for (int c = 0; c < g.board().cols(); ++c) {
                        std::fprintf(csv, "%02x", g.board().at(c, r) & 0xFF);
                    }
                }
                std::fprintf(csv, "\n");
            }
            for (int c = 1; c <= tubes::kAtomSlots; ++c) {
                const bool now = g.atom(c).active();
                if (now && !wasActive[c]) {
                    // The cumulative roll count is the field the rig can match
                    // without any notion of time: `RandSeed` is one orbit of an
                    // injective LCG, so reading it off the original converts
                    // straight back into "how many times Random has been
                    // called". Spawn N is spawn N in both runs, so comparing
                    // the count AT each spawn needs no frame alignment - and
                    // needs no breakpoint, which is what made this the usable
                    // instrument after Z0 on the RTL turned out not to trap.
                    std::printf("spawn %4d frame %5zu col %d type %2d rolls %zu\n",
                                ++spawns, f, c, g.atom(c).colour, rolls.size());
                    born[c] = static_cast<int>(f);
                }
                if (g.dropsRemaining() != drops) {
                    // A Bonus caught gives one BACK - `1000:180c` - so this is
                    // not always a loss, and calling every change a miss made
                    // the trace read as three misses where one was a gain.
                    const bool gained = g.dropsRemaining() > drops;
                    drops = g.dropsRemaining();
                    std::printf("%-5s       frame %5zu  drops now %d\n",
                                gained ? "BONUS" : "MISS", f, drops);
                }
                if (!now && wasActive[c]) {
                    // The record going free is half the spawn rule: the column
                    // is re-rolled up to ten times looking for a FREE slot, so
                    // how long an atom occupies its record decides how often
                    // the original retries - and the retry count is exactly
                    // what the rig's roll count measures.
                    std::printf("free        frame %5zu col %d  after %d frames\n",
                                f, c, static_cast<int>(f) - born[c]);
                }
                wasActive[c] = now;
            }
        }
        if (opt.randomTrace) {
            const size_t lim = std::min(rolls.size(),
                                        static_cast<size_t>(opt.randomTrace));
            for (size_t i = 0; i < lim; ++i) {
                std::printf("roll %4zu n %3d seed 0x%08x\n", i, rolls[i].first,
                            rolls[i].second);
            }
            std::printf("# %zu rolls total\n", rolls.size());
        }
        std::printf("# %d spawns, score %d, chains %d, drops %d/%d%s\n", spawns,
                    g.score(), g.chains(), g.dropsRemaining(),
                    g.startingDrops(), g.gameOver() ? ", GAME OVER" : "");
        std::printf("# grid");
        for (int r = 0; r < g.board().rows(); ++r) {
            for (int c = 0; c < g.board().cols(); ++c) {
                std::printf(" %d", g.board().typeAt(c, r));
            }
        }
        std::printf("\n");
        if (csv) std::fclose(csv);
        return 0;
    }

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
    // The briefing is NOT drawn over the play backdrop. `1000:86b8` loads
    // GAMEBG for the wave that is about to START - into `[BP-0x86]`, which
    // `1000:3a67` blits at `1000:3c0b` - and draws its own text over a
    // blackboard scene instead. Confirmed by capturing the original.
    // The title screen's pair, and the four star frames the Pascal main
    // program loads as shared sprites (`1000:aaba`) - which is why the title
    // screen itself only names four files and not these.
    tubes::Image titleBg, titleFg;
    const bool haveTitleBg = loadImage(res, "TUBESBG.GFX", titleBg, 0);
    const bool haveTitleFg = loadImage(res, "TUBESFG.GFX", titleFg, 0);
    tubes::Image stars[tubes::kStarFrames + 1];
    bool haveStar[tubes::kStarFrames + 1] = {};
    for (int i = 1; i <= tubes::kStarFrames; ++i) {
        haveStar[i] = loadImage(res, "STAR" + std::to_string(i) + ".GFX",
                                stars[i], 0);
    }

    // Every harness entry point - a screenshot, a scripted run, a recorded
    // demo, a captured state, an explicit wave - must stay deterministic, so
    // only interactive play gets a clock seed. It also must not WRITE to the
    // player's game directory; see `saveHiScores`.
    const bool harness = !opt.screenshot.empty() || opt.autoFrames > 0 ||
                         opt.demo || opt.playDemo || !opt.renderState.empty() ||
                         opt.wave > 0;

    // `1b2e:0243`: read `TUBES.HSC` if it is there, otherwise fill both banks
    // with the twenty names the binary ships. The file lives beside the game
    // data, which is where the original writes it.
    const std::string hiScorePath = opt.gameDir + "/TUBES.HSC";
    tubes::HiScoreFile hiScores = tubes::defaultHiScores();
    {
        std::ifstream hf(hiScorePath, std::ios::binary);
        if (hf) {
            std::vector<uint8_t> raw((std::istreambuf_iterator<char>(hf)),
                                      std::istreambuf_iterator<char>());
            if (!tubes::decodeHiScores(raw, hiScores)) {
                std::fprintf(stderr,
                             "TUBES.HSC is malformed (%zu bytes); using the "
                             "shipped table\n", raw.size());
                hiScores = tubes::defaultHiScores();
            }
        }
    }
    auto saveHiScores = [&]() {
        // A HARNESS RUN MUST NOT WRITE TO THE GAME DIRECTORY. `--auto-advance`
        // walks a whole session, so it reaches the end of a wave, qualifies,
        // and saved a real `TUBES.HSC` into the player's own game files -
        // which then changed what every later capture compared against. The
        // unit tests were careful about this from the start; the harness was
        // not, because nothing in it used to write anything.
        if (harness) return;
        const std::vector<uint8_t> raw = tubes::encodeHiScores(hiScores);
        std::ofstream hf(hiScorePath, std::ios::binary);
        if (hf) hf.write(reinterpret_cast<const char*>(raw.data()),
                         static_cast<std::streamsize>(raw.size()));
    };

    tubes::Image blackboard;
    const bool haveBlackboard = loadImage(res, "BLACKBRD.GFX", blackboard, -1);

    // The slide's four corner clips, in the order `1b2e:097a` places them:
    // upper-left, upper-right, lower-left, lower-right. Each is 20 bytes - a
    // header plus 4 x 4 - which is what pins them to the `2321:0711(4, 4, ...)`
    // calls. Index 0 is transparent, since `2321:0711` is a masked blit.
    tubes::Image slideCorner[4];
    bool haveCorner[4] = {false, false, false, false};
    for (int i = 0; i < 4; ++i) {
        haveCorner[i] = loadImage(res, tubes::kCornerNames[i], slideCorner[i], 0);
    }

    // The professor and his furniture, `1b2e:0656`. Index 0 is transparent -
    // `2321:0711` is the masked blit, and the original reaches these through
    // `DS:0x207c`, `0x2068` and `0x2060`.
    tubes::Image pointerFrame[4];
    bool havePointer[4] = {false, false, false, false};
    for (int i = 0; i < 4; ++i) {
        // Frame 0 is masked - it goes over the blackboard. The wave frames are
        // opaque, because the original stamps them with `2321:068d`, which is
        // the opaque member of the blit family.
        havePointer[i] =
            loadImage(res, tubes::kPointerNames[i], pointerFrame[i], i ? -1 : 0);
    }
    tubes::Image booksArt, slideBar;
    const bool haveBooks = loadImage(res, "BOOKS.GFX", booksArt, 0);
    const bool haveBar = loadImage(res, "SLIDEBAR.GFX", slideBar, 0);

    SceneArt sceneArt;
    sceneArt.corners = slideCorner;
    sceneArt.haveCorner = haveCorner;
    sceneArt.pointer = pointerFrame;
    sceneArt.havePointer = havePointer;
    sceneArt.books = &booksArt;
    sceneArt.haveBooks = haveBooks;
    sceneArt.bar = &slideBar;
    sceneArt.haveBar = haveBar;
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

    // Indexed 1..7 by colour, so slot 0 stays empty the way the original's
    // table does.
    tubes::Sprite smallBall[8];
    bool haveSmallBall[8] = {};
    for (int i = 1; i <= 7; ++i) {
        haveSmallBall[i] = loadSprite(res, kSmallBallFiles[i - 1], smallBall[i]);
    }
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
    // The "big font" is a SLOT, `DS:0x2110`, not one font: each stage loads
    // what it wants into it. The HUD's is FUTURE.816, proven byte for byte
    // against a captured digit. The MENU's and the BRIEFING TITLE's is
    // STARTREK.816, matched the same way against captures of each - 344 lit
    // pixels hit / 7 missed for `Start Game`, and 187 / 187 with 27 missed for
    // `Wave 1` at exactly the y the port already used.
    tubes::Font headingFont;
    const bool haveHeading = loadFont(res, "STARTREK.816", 8, 8, headingFont);
    const bool haveSmall = loadFont(res, "TINY6X8.88", 6, 4, smallFont);
    // The fourth `.816`, and the last slot to be identified. `1000:aaba` loads
    // it into `DS:0x2114`, and `1000:96db` selects that slot for the high
    // score list - so the table is written in CURSIVE, which is what the
    // player sees on the viewer's chalkboard. Metrics from `23e7:013b`'s
    // `(ptr, 8, 0x10, 8, 8)`: advance 8, peak 8.
    tubes::Font scriptFont;
    const bool haveScript = loadFont(res, "SCRIPT.816", 8, 8, scriptFont);
    const int tubeFrames = static_cast<int>(haveTube[1]) +
                           static_cast<int>(haveTube[2]) +
                           static_cast<int>(haveTube[3]);
    std::printf("loaded %d/%d atoms, %d/3 test tube frames\n", loaded, drawable,
                tubeFrames);

    // The demo carries the generator state its recording was made against, so
    // the session has to be seeded from it before anything rolls a die.
    tubes::Demo demo;
    if (opt.playDemo) {
        tubes::Bytes raw;
        std::string demoErr;
        if (!res.read("DEMO.SCR", raw, demoErr) ||
            !tubes::decodeScr(raw, demo, demoErr)) {
            std::fprintf(stderr, "error: %s\n", demoErr.c_str());
            return 1;
        }
        std::printf("DEMO.SCR: seed 0x%08x, %zu frames\n", demo.seed,
                    demo.input.size());
    }

    // `1000:9e53` IS the session: the menu leaves the title screen and the
    // session is entered fresh, with the difficulty the player chose. So the
    // Game is owned rather than a local - starting a second game after a
    // Game Over has to build a new one, not reset the old one in place.
    // `harness` is computed above, where the high score table is loaded.
    std::unique_ptr<tubes::Game> game;
    auto newSession = [&](tubes::Difficulty diff, uint32_t seed) {
        game = std::make_unique<tubes::Game>(kCols, kRows, diff, seed);
        game->setFallHeight(kFallHeight);
    };
    // The original's `Randomize` at startup. Without it the title screen's
    // `Random(7)` returns the same colour every run - the atom in the
    // letterforms was always the same blue.
    const uint32_t kFixedSeed = 0x9E3779B9u;
    uint32_t bootSeed = kFixedSeed;
    if (!harness && !opt.playDemo) {
        bootSeed = static_cast<uint32_t>(std::time(nullptr)) * 2654435761u + 1u;
    }
    if (opt.seed) bootSeed = opt.seed;   // reproduce a specific run
    newSession(opt.playDemo ? kDemoDifficulty : tubes::Difficulty::k101,
               opt.playDemo ? demo.seed : bootSeed);
    // `1000:9e53`'s new-game arm seeds the wave number from `DS:0x1d50` and
    // then loops brief-play-advance. Only the first half of that exists here:
    // there is no briefing screen and no stats blackboard, so the loop below
    // just steps to the next wave when one is cleared.
    if (opt.wave > 0) {
        while (game->progress().wave < opt.wave) game->advanceWave();
        game->startWave();
        std::printf("Wave %d: mode %d, %d to go\n", game->progress().wave,
                    static_cast<int>(game->waveMode()), game->objective().counter);
    }
    if (!opt.renderState.empty() && !loadState(opt.renderState, *game)) return 1;

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
        game->update(scriptedInput(*game), 1.0f / 60.0f);
        if (game->waveComplete()) {
            game->advanceWave();
            game->startWave();
            std::printf("Wave %d: mode %d, %d to go\n", game->progress().wave,
                        static_cast<int>(game->waveMode()),
                        game->objective().counter);
        }
    }
    if (opt.autoFrames) {
        std::printf(
            "simulated %d frames: %d atoms, score %d, chains %d, drops %d/%d%s\n",
            opt.autoFrames, game->board().count(), game->score(), game->chains(),
            game->dropsRemaining(), game->startingDrops(),
            game->gameOver() ? ", GAME OVER" : "");
        for (int c = 1; c <= tubes::kAtomSlots; ++c) {
            const tubes::Falling& fa = game->atom(c);
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
    // must not stop the game from being playable. Sound effects are the same,
    // and they do not need DRIVERS.RES at all - the .SFX resources are in
    // TUBES.RES and the driver only ever fed them to the card.
    // DECLARATION ORDER MATTERS. The audio callback holds a bare pointer into
    // `sounds` while a voice is playing, and locals are destroyed in reverse,
    // so `sounds` has to be declared FIRST - then `music` closes the device in
    // its destructor while the samples are still alive. The other way round is
    // a use-after-free on the audio thread on the way out.
    tubes::Sound sounds[tubes::sfx::kCount];
    // `CLAP.SFX` is not in the atom-indexed table - that table is keyed by
    // atom TYPE - so it is loaded on its own for the high score viewer.
    tubes::Sound clapSound;
    bool haveClapSound = false;
    tubes::MusicPlayer music;
    if (opt.screenshot.empty()) {
        std::string audioErr;
        if (!music.openSilent(audioErr)) {
            std::fprintf(stderr, "sound disabled: %s\n", audioErr.c_str());
        } else {
            {
                tubes::Bytes raw;
                std::string err;
                haveClapSound = res.read("CLAP.SFX", raw, err) &&
                                tubes::decodeSfx(raw, clapSound, err);
            }
            int loadedSfx = 0, wanted = 0;
            for (int i = 0; i < tubes::sfx::kCount; ++i) {
                if (!kSoundFiles[i]) continue;
                ++wanted;
                tubes::Bytes raw;
                std::string err;
                if (res.read(kSoundFiles[i], raw, err) &&
                    tubes::decodeSfx(raw, sounds[i], err)) {
                    ++loadedSfx;
                } else {
                    std::fprintf(stderr, "  %s: %s\n", kSoundFiles[i],
                                 err.c_str());
                }
            }
            std::printf("loaded %d/%d sound effects\n", loadedSfx, wanted);
        }
    }
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

    // The title screen's foreground, as a Screen so `stamp` can take a box out
    // of it - the same arrangement as `scene` above.
    tubes::Screen titleFgScene;
    titleFgScene.clear(0);
    if (haveTitleFg) titleFgScene.blit(titleFg);

    // `1b2e:52bf`. Every harness entry point goes straight to the session, so
    // the flags keep working exactly as they did.
    Stage stage = (harness && opt.titlePage < 0) ? Stage::kPlay : Stage::kTitle;
    tubes::Menu menu;
    tubes::TitleAtom titleAtom;
    // `1b2e:5312`: one of the seven ordinary colours, rolled once on entry.
    int titleBall = game->rollForTest(7) + 1;
    int attractTimer = tubes::kAttractTimeout;
    float titleAccum = 0.0f;
    if (opt.titlePage > 0) {
        // Navigate there the way a player would, rather than setting the page
        // directly - so a capture can only show a page the menu really reaches.
        menu.raise();
        switch (opt.titlePage) {
        case 3: menu.select(); menu.select(); break;        // Start, Endurance
        case 2: menu.select(); break;                       // Start Game
        case 4: menu.moveDown(); menu.select(); menu.select(); break;
        case 5: menu.moveDown(); menu.select();
                menu.moveDown(); menu.select(); break;
        case 6: menu.moveDown(); menu.moveDown(); menu.select(); break;
        case 7: for (int k = 0; k < 7; ++k) menu.moveDown();
                menu.select(); break;
        default: break;
        }
    }

    bool running = true;
    // Counts down to zero on presented frames, so `--screenshot-after N`
    // captures the (N+1)th frame the loop actually draws.
    int shotCountdown = opt.shotAfter;
    int autoAdvanceTick = 0;

    // The present tail, shared. This is a lambda rather than repeated code
    // because a previous version duplicated it for an overlay and `continue`d
    // past the screenshot arm, which made `--screenshot` hang forever with
    // nothing written.
    auto presentFrame = [&]() {
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

        if (!opt.screenshot.empty() && shotCountdown-- <= 0) {
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

        // `--screenshot-after` runs the loop as fast as it can, since it may
        // need thousands of frames to reach a screen that is past a game over.
        if (opt.shotAfter <= 0) SDL_Delay(16);
    };

    // `1000:a5d2`: the briefing runs once per wave, before `1000:3a67`, and
    // holds until a key. `1000:632f` rolls its decorative ball, so that roll
    // belongs to the screen rather than to the wave.
    bool briefingUp = opt.wave > 0;
    int8_t briefDecor = 1;
    // `1000:86b8`: `repeat n := Random(10) + 1 until n <> DS:0x2056`. The
    // backdrop is re-rolled until it differs from the last one, so no two
    // consecutive waves share a backdrop - and the wave it is loaded for is
    // the one ABOUT TO START, not the briefing being shown, which never blits
    // it. `--gamebg` pins it so a capture can be matched.
    int lastBackdrop = 0;                 // DS:0x2056
    const bool backdropPinned = opt.gameBg != "GAMEBG1.GFX";
    auto rollBackdrop = [&]() {
        if (backdropPinned) return;
        int n = lastBackdrop;
        while (n == lastBackdrop) n = game->rollForTest(10) + 1;
        lastBackdrop = n;
        tubes::Image next;
        if (loadImage(res, "GAMEBG" + std::to_string(n) + ".GFX", next, -1)) {
            background = std::move(next);
            haveBg = true;
        }
    };

    auto raiseBriefing = [&]() {
        briefingUp = true;
        briefDecor = static_cast<int8_t>(game->rollForTest(8) + 1);
        rollBackdrop();
    };
    if (briefingUp) raiseBriefing();

    // ---- `1000:9e53`'s wave loop -------------------------------------------
    //
    // `DS:0x1d4e`: 0 attract, 1 endurance, anything else wave mode. Every
    // wave test in the original is `mode <> 0 and mode <> 1`.
    int gameMode = opt.playDemo ? 0 : (opt.wave > 0 ? 2 : 1);
    tubes::SessionFlags flags;
    tubes::SessionTotals totals;
    tubes::SessionStage sstage =
        briefingUp ? tubes::SessionStage::kBriefing : tubes::SessionStage::kPlay;
    tubes::Banner banner = tubes::Banner::kNone;
    // The banner's own three-phase sequence - see `BannerPhase`.
    tubes::BannerPhase bannerPhase = tubes::BannerPhase::kHold;
    float bannerTimer = 0.0f;
    bool bannerWaitsForMusic = false;
    std::vector<tubes::StatsRow> statsRows;
    tubes::ContinuePrompt continuePrompt;
    float continueAccum = 0.0f;
    // F5, `1000:3916`. The original blocks in `repeat until ReadKey = $bf`, so
    // the simulation does not advance and ONLY F5 releases it.
    bool paused = false;
    // F3 and F4, `DS:0x215f` and `DS:0x215e`.
    bool musicOn = !opt.music.empty();
    bool soundOn = true;

    // `1b2e:0a11`'s slide drop, gated on `DS:0x210e` - it runs the FIRST time
    // the scene is shown and never again, so this is a program-lifetime flag
    // and not a per-briefing one.
    bool slideDropped = false;
    int slideFrame = 0;          // index into kSlideDrop while dropping
    float slideAccum = 0.0f;
    // `DS:0x20b0`, the professor's pointer frame. `1b2e:0656` parks it at the
    // standing pose and `1b2e:0e37` advances it once every ten retraces while
    // a screen waits for a key.
    int profWave = 0;
    float profAccum = 0.0f;
    auto slidePos = [&]() {
        if (slideDropped || slideFrame >= tubes::kSlideDropFrames) {
            return tubes::SlideFrame{tubes::kSlideX, tubes::kSlideY};
        }
        return tubes::kSlideDrop[slideFrame];
    };

    // Swapping the song for a stage. The seven names live in `1000:9e53`'s own
    // frame as far pointers four bytes apart - see reversing-notes.
    auto playSong = [&](const char* name) {
        if (!name || !*name || !musicOn || !music.isOpen()) return;
        tubes::Bytes data;
        std::string err;
        if (res.read(name, data, err)) music.play(data, err);
    };

    // ---- the high-score entry screen, `1000:96db` -------------------------
    //
    // `1000:a6c1` runs it when the wave loop falls out and the session was NOT
    // aborted, the mode is not attract, and `DS:0x1d4b` is clear. The score is
    // then offered to the bank for the mode just played.
    bool hsActive = false;
    std::string hsName;
    int hsRow = 0;             // 1-based, as the original's display loop is
    int hsCursor = tubes::kHsCursorMin;
    int hsCursorDir = 1;
    float hsCursorAccum = 0.0f;
    // Counts out the applause between finishing the name and committing it.
    float hsHold = 0.0f;
    tubes::HiScoreBank hsBank = tubes::HiScoreBank::kWave;

    // The standalone viewer the menu opens, `1b2e:61b6` - now decompiled, so
    // the layout is the original's rather than borrowed. Page 0 is Endurance
    // and page 1 is Wave; the original pre-renders both onto the two video
    // pages and flips between them, and redrawing gives the same picture.
    bool hsViewing = opt.hsPage >= 0;
    int hsViewPage = opt.hsPage > 0 ? 1 : 0;
    float hsViewTimer = tubes::kHsViewSeconds;   // the thirty-second give-up

    // Every route out of a session goes through here, so the offer cannot be
    // skipped on one path and taken on another.
    auto endSession = [&]() {
        hsBank = (gameMode == 1) ? tubes::HiScoreBank::kEndurance
                                 : tubes::HiScoreBank::kWave;
        const uint32_t sc = static_cast<uint32_t>(game->score());
        if (!flags.aborted && gameMode != 0 &&
            tubes::qualifies(hiScores[hsBank], sc)) {
            // Seeded with the sentinel and typed over, exactly as the original
            // does - which is why the sentinel's tail survives in the file.
            hsRow = tubes::insertHiScore(hiScores[hsBank], "", sc) + 1;
            hsName.clear();
            hsCursor = tubes::kHsCursorMin;
            hsCursorDir = 1;
            hsHold = 0.0f;
            hsActive = true;
            music.stop();
            return;
        }
        stage = Stage::kTitle;
        menu.raise();
        playSong("TUBES.MUS");
    };

    // `1000:5dbb`'s five steps, entered when the wave loop falls out.
    auto raiseBanner = [&](tubes::Banner b) {
        banner = b;
        sstage = tubes::SessionStage::kBanner;
        const tubes::BannerText bt = tubes::bannerText(b);
        if (*bt.music) playSong(bt.music);
        else music.stop();
        // `1000:5dfb`: the hold comes first, and input is not read during it.
        bannerPhase = tubes::BannerPhase::kHold;
        bannerTimer = tubes::kBannerHoldSeconds;
        // Only an arm that plays something can end on `[DS:0x22ce]` - and
        // only when there is a driver to ask. With music off the original has
        // no song to finish either, so the wait is a key wait.
        bannerWaitsForMusic = *bt.music != 0 && musicOn && music.isOpen();
    };

    // `1000:5e23`/`5e90`: the wait ends, the music is stopped, and
    // `1000:5ef0`'s second Delay runs before anything else is drawn.
    auto leaveBannerWait = [&]() {
        music.stop();                                  // [DS:0x22da]
        bannerPhase = tubes::BannerPhase::kOutro;
        bannerTimer = tubes::kBannerHoldSeconds;
    };

    // Entering the stats screen is what accumulates the running chain total,
    // so it happens exactly once per visit - never in the draw path.
    auto enterStats = [&]() {
        totals.chainsThisWave = game->chains();
        statsRows = tubes::buildStatsScreen(totals, game->progress().wave,
                                            game->score(), false);
        playSong(tubes::kStatsMusic);
    };
    float demoAccum = 0.0f;
    size_t demoFrame = 0;
    Uint32 last = SDL_GetTicks();

    while (running) {
        // Harness only: press RETURN periodically so a headless run walks
        // through the screens that hold for a key. Periodic rather than every
        // frame, so each screen is on display long enough to be captured.
        if (opt.autoAdvance && ++autoAdvanceTick % 40 == 0) {
            SDL_Event fake{};
            fake.type = SDL_KEYDOWN;
            fake.key.keysym.sym = SDLK_RETURN;
            SDL_PushEvent(&fake);
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { running = false; continue; }
            if (ev.type != SDL_KEYDOWN) continue;
            const SDL_Keycode k = ev.key.keysym.sym;

            // `1000:9744`'s typing loop. It owns the keyboard entirely while
            // it is up: printable characters append, backspace removes, and
            // ESC or RETURN finish - nothing else is looked at.
            if (hsActive) {
                if (hsHold > 0.0f) continue;   // the applause owns the screen
                if (k == SDLK_RETURN || k == SDLK_ESCAPE) {
                    // `1000:96db`'s tail: the row is redrawn in the settled
                    // colour with no cursor, the applause plays, and the
                    // screen HOLDS for `23e7:0024(0x78)` - 120 retraces,
                    // 1.71 s - before the record is committed and the file
                    // written. The port committed and left in the same frame.
                    if (soundOn && haveClapSound) music.playSound(&clapSound);
                    hsHold = tubes::kHsCommitSeconds;
                } else if (k == SDLK_BACKSPACE) {
                    if (!hsName.empty()) hsName.pop_back();
                } else if (k >= 0x20 && k <= 0x7e &&
                           static_cast<int>(hsName.size()) <
                               tubes::kHiScoreNameMax) {
                    // `1000:9718` gates on 0x20..0x7e and a length under 25.
                    const bool shift =
                        (SDL_GetModState() & KMOD_SHIFT) != 0;
                    char c = static_cast<char>(k);
                    if (shift && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
                    hsName.push_back(c);
                }
                continue;
            }

            // `1b2e:6423`. The first page treats ESC specially - it leaves at
            // once - and any other key advances to the second. On the second
            // page every key leaves, ESC included.
            if (hsViewing) {
                if (hsViewPage == 0 && k != SDLK_ESCAPE) {
                    hsViewPage = 1;
                    hsViewTimer = tubes::kHsViewSeconds;
                } else {
                    hsViewing = false;
                    playSong("TUBES.MUS");
                }
                continue;
            }

            if (stage == Stage::kTitle) {
                // Every accepted press resets the attract countdown.
                attractTimer = tubes::kAttractTimeout;
                if (!menu.up()) {
                    // The two-key protocol, `DS:0x1d42`: ESC, SPACE or RETURN
                    // raises the menu and the press is SWALLOWED, so it cannot
                    // also pick an item.
                    if (k == SDLK_ESCAPE || k == SDLK_SPACE ||
                        k == SDLK_RETURN) {
                        menu.raise();
                    }
                    continue;
                }
                // `SELECT.SFX` is one of the four resources the title stage
                // loads for itself - `TUBESBG.GFX`, `TUBESFG.GFX`,
                // `TUBES.MUS`, `SELECT.SFX` at `1b2e:5238` - so the sound
                // belongs to this screen. It plays on SELECTING an item, not
                // on moving between them; that is the player's account, since
                // the menu's own call sites are near calls whose targets
                // Ghidra renders with the 0x10000 bias and a scan for the play
                // vector cannot see them. Marked as reported rather than
                // decompiled, and it may be used elsewhere too.
                if (k == SDLK_UP) menu.moveUp();
                else if (k == SDLK_DOWN) menu.moveDown();
                else if (k == SDLK_ESCAPE) menu.back();
                else if (k == SDLK_RETURN || k == SDLK_SPACE) {
                    if (soundOn && sounds[tubes::sfx::kSelect].valid()) {
                        music.playSound(&sounds[tubes::sfx::kSelect]);
                    }
                    switch (menu.select()) {
                    case tubes::MenuResult::kPlay: {
                        static const tubes::Difficulty kDiff[3] = {
                            tubes::Difficulty::k101, tubes::Difficulty::k201,
                            tubes::Difficulty::k301};
                        const tubes::MenuChoice& c = menu.choice();
                        newSession(kDiff[c.difficulty], bootSeed ^ 0x5bf03635u);
                        // `DS:0x1d4e`: 2 is Wave mode, which starts at wave 1
                        // and briefs before playing. Endurance has no wave
                        // structure and no briefing.
                        gameMode = c.mode;
                        flags = tubes::SessionFlags{};
                        totals = tubes::SessionTotals{};
                        banner = tubes::Banner::kNone;
                        paused = false;
                        briefingUp = false;
                        if (c.mode == 2) {
                            game->startWave();
                            raiseBriefing();
                        }
                        sstage = tubes::firstStage(gameMode);
                        playSong(sstage == tubes::SessionStage::kBriefing
                                     ? tubes::kBriefingMusic
                                     : tubes::playMusicFor(
                                           game->dropsRemaining()));
                        stage = Stage::kPlay;
                        break;
                    }
                    case tubes::MenuResult::kQuit:
                        running = false;
                        break;
                    case tubes::MenuResult::kHighScores:
                        // `1b2e:63c7`: the music becomes CLASS.MUS and the
                        // applause starts, on the Endurance page.
                        hsViewing = true;
                        hsViewPage = 0;
                        hsViewTimer = tubes::kHsViewSeconds;
                        playSong(tubes::kHsViewMusic);
                        if (soundOn && haveClapSound) {
                            music.playSound(&clapSound);
                        }
                        break;
                    default:
                        // Instructions, View Demo, Credits and Load are stages
                        // that do not exist yet; the menu simply stays up
                        // rather than pretending otherwise.
                        break;
                    }
                }
                continue;
            }

            // ---- inside the session, `1000:9e53` -------------------------
            //
            // `1000:2dd0` runs once a frame and only when `KeyPressed`, so the
            // whole of it belongs here rather than in the per-frame update.
            // The keys it recognises are ESC and F1..F5; everything else falls
            // through to the tube, which reads the keyboard separately.
            const uint8_t code = originalKeyCode(k);

            // F5 first: while paused the original is blocked inside
            // `repeat until ReadKey = $bf`, so NOTHING else is looked at and
            // only F5 gets out.
            if (paused) {
                if (code == tubes::gamekey::kF5) {
                    paused = false;
                    music.setPaused(false);
                }
                continue;
            }

            switch (sstage) {
            case tubes::SessionStage::kBriefing:
                // `1b2e:0e37` accepts any of its keys here; the briefing has
                // nothing to choose, so any key advances it.
                briefingUp = false;
                sstage = tubes::SessionStage::kPlay;
                playSong(tubes::playMusicFor(game->dropsRemaining()));
                continue;

            case tubes::SessionStage::kBanner:
                // `1000:5e0b` and `1000:5e78`: a key ends the WAIT, and only
                // the wait. The 40-retrace hold before it does not look at
                // input at all, and the outro after it is already committed -
                // which is what stops the tip keypress that ended the wave
                // from dismissing the banner it caused.
                if (bannerPhase == tubes::BannerPhase::kWait) leaveBannerWait();
                continue;

            case tubes::SessionStage::kStats: {
                const tubes::StageTransition t =
                    tubes::advanceStage(sstage, flags, gameMode);
                if (t.advanceWave) game->advanceWave();
                sstage = t.next;
                if (sstage == tubes::SessionStage::kContinue) {
                    if (!continuePrompt.begin(totals)) {
                        // `1000:8c3f`: with no continues left the screen does
                        // not appear, and the loop ends.
                        sstage = tubes::SessionStage::kFinished;
                        endSession();
                    } else {
                        continueAccum = 0.0f;
                        playSong(tubes::kContinueMusic);
                    }
                } else if (sstage == tubes::SessionStage::kBriefing) {
                    game->startWave(flags.replay);
                    flags.replay = false;   // `1000:86b8`'s tail clears it
                    raiseBriefing();
                    playSong(tubes::kBriefingMusic);
                }
                continue;
            }

            case tubes::SessionStage::kContinue: {
                // `1b2e:0e37` returns 1 for Enter/Space and 2 for ESC; the
                // Continue screen acts on exactly those two.
                const bool accept = (k == SDLK_RETURN || k == SDLK_SPACE);
                const bool decline = (k == SDLK_ESCAPE);
                if (!accept && !decline) continue;
                const tubes::ContinueResult r =
                    continuePrompt.tick(accept, decline);
                if (r == tubes::ContinueResult::kAccepted) {
                    tubes::applyContinue(flags, totals);
                    game->continueSession();
                }
                if (r != tubes::ContinueResult::kWaiting) {
                    const tubes::StageTransition t =
                        tubes::advanceStage(sstage, flags, gameMode);
                    sstage = t.next;
                    if (sstage == tubes::SessionStage::kBriefing) {
                        game->startWave(flags.replay);
                        flags.replay = false;
                        raiseBriefing();
                        playSong(tubes::kBriefingMusic);
                    } else {
                        endSession();
                    }
                }
                continue;
            }

            default:
                break;
            }

            // Still playing: `1000:2dd0`'s dispatch proper.
            switch (tubes::classifyGameKey(code, gameMode == 0,
                                           /*saveDisabled=*/true)) {
            case tubes::GameAction::kAbort:
                flags.aborted = true;
                break;
            case tubes::GameAction::kPause:
                paused = true;
                music.setPaused(true);
                break;
            case tubes::GameAction::kMusicToggle:
                musicOn = !musicOn;
                // `1000:377a`: switching on restarts the CURRENT song, which
                // is the one chosen at the top of the wave.
                if (musicOn) playSong(tubes::playMusicFor(
                                 game->dropsRemaining()));
                else music.stop();
                break;
            case tubes::GameAction::kSoundToggle:
                soundOn = !soundOn;
                break;
            case tubes::GameAction::kHelp:
            case tubes::GameAction::kSave:
                // `1b2e:2d63`'s help body and the F2 slot picker are read as
                // a dispatch but their screens are not decompiled. Left inert
                // rather than invented.
                break;
            default:
                break;
            }
        }

        Uint32 now = SDL_GetTicks();
        float dt = static_cast<float>(now - last) / 1000.0f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;    // a stall must not teleport atoms
        // Under `--screenshot-after` the step is fixed, so one presented frame
        // is one simulation frame and the capture point is reproducible.
        if (opt.shotAfter > 0) dt = 1.0f / tubes::kFrameHz;

        // --demo drives the REAL loop with the scripted player, so the render
        // path gets exercised on every frame of a whole session rather than
        // only on the one frame --auto screenshots. That distinction matters:
        // a crash that needs both a full beaker and a live render is invisible
        // to --auto, which simulates first and draws once at the end.
        if (stage == Stage::kTitle) {
            // `1b2e:52bf`'s loop body, at the TITLE screen's own rate - see
            // kTitleHz. Everything in it is counted in frames: 4 px a frame
            // along a leg, a star frame every three, 720 frames to attract.
            titleAccum += dt * tubes::kTitleHz;
            int steps = static_cast<int>(titleAccum);
            titleAccum -= static_cast<float>(steps);
            if (steps > 8) steps = 8;      // a stall must not teleport the atom
            for (int k = 0; k < steps; ++k) {
                titleAtom.step();
                if (menu.up()) menu.tick();
                --attractTimer;
            }
            if (attractTimer <= 0) {
                // The attract arm returns 9. DEMO.SCR replay through the live
                // loop exists (`--play-demo`) but is not wired to this yet, so
                // for now the countdown simply restarts rather than silently
                // doing nothing.
                attractTimer = tubes::kAttractTimeout;
            }
        } else if (opt.screenshot.empty() || opt.shotAfter > 0) {
            // `--screenshot` alone captures the opening frame and exits, so it
            // deliberately does not simulate. `--screenshot-after N` does, or
            // it could never reach a screen that is past a game over.
            if (opt.playDemo) {
                // The recording is consumed at the fixed game step rather than
                // through `update`'s real-time conversion - same accumulator,
                // driving an index instead. One byte per frame the tube was
                // IDLE, not per frame: see Game::acceptsInput.
                demoAccum += dt * tubes::kFrameHz;
                int steps = static_cast<int>(demoAccum);
                demoAccum -= static_cast<float>(steps);
                if (steps > 8) steps = 8;
                for (int k = 0; k < steps; ++k) {
                    if (demoFrame >= demo.input.size()) { running = false; break; }
                    game->stepOnce(game->acceptsInput() ? demo.input[demoFrame++]
                                                      : 0);
                }
            } else if (sstage == tubes::SessionStage::kPlay && !paused) {
                game->update(opt.demo ? scriptedInput(*game) : readKeyboard(), dt);
            }

            // `1000:5cff`, the tail of `3a67`'s frame loop: the wave ends on a
            // completed objective, a drop underflow, or the key handler having
            // set the abort. All three then fall through to the banner at
            // `1000:5d64`, which `3a67` draws itself before returning.
            if (sstage == tubes::SessionStage::kPlay &&
                (game->waveComplete() || game->gameOver() || flags.aborted)) {
                flags.gameOver = game->gameOver();
                // `1000:5dae`: the Perfect Bonus is added to the score before
                // the banner, not by the stats screen that reports it.
                if (totals.perfectBonus) {
                    game->setScore(game->score() + tubes::kPerfectBonus);
                }
                raiseBanner(tubes::bannerFor(flags, game->waveComplete()));
            }

            // `1000:5dfb` -> `5e0b` -> `5ef0`. The hold and the outro are
            // timed and deaf; only the middle phase looks at input, and it
            // also ends when the song has been round once - `[DS:0x22ce]`.
            if (sstage == tubes::SessionStage::kBanner) {
                bannerTimer -= dt;
                if (bannerPhase == tubes::BannerPhase::kHold) {
                    if (bannerTimer <= 0.0f) {
                        bannerPhase = tubes::BannerPhase::kWait;
                        // `[DS:0x234e]` and `2591:0552`, the flush pair the
                        // original runs before every key wait. Without it the
                        // keypress that ended the wave is still queued and
                        // ends the banner on the frame it opens.
                        SDL_FlushEvent(SDL_KEYDOWN);
                    }
                } else if (bannerPhase == tubes::BannerPhase::kWait) {
                    if (bannerWaitsForMusic && music.songLooped()) {
                        leaveBannerWait();
                    }
                } else if (bannerTimer <= 0.0f) {
                    const tubes::StageTransition t =
                        tubes::advanceStage(sstage, flags, gameMode);
                    sstage = t.next;
                    banner = tubes::Banner::kNone;
                    if (sstage == tubes::SessionStage::kStats) enterStats();
                    if (sstage == tubes::SessionStage::kFinished) endSession();
                }
            }

            // The professor waves while any of the three screens is up -
            // `1b2e:0e37` steps `DS:0x20b0` every ten retraces, 1..5.
            if (sstage == tubes::SessionStage::kBriefing ||
                sstage == tubes::SessionStage::kStats ||
                sstage == tubes::SessionStage::kContinue) {
                profAccum += dt * tubes::kRetraceHz;
                while (profAccum >= tubes::kProfWaveRetraces) {
                    profAccum -= tubes::kProfWaveRetraces;
                    if (++profWave > tubes::kProfWaveFrames) profWave = 1;
                }
            } else {
                profWave = 0;
            }

            // `1000:9750`: the cursor colour walks 1..14 and back, one step a
            // frame, so it pulses rather than blinks. The hold after the name
            // is finished has no cursor at all.
            if (hsActive && hsHold <= 0.0f) {
                hsCursorAccum += dt * tubes::kRetraceHz;
                while (hsCursorAccum >= 1.0f) {
                    hsCursorAccum -= 1.0f;
                    hsCursor += hsCursorDir;
                    if (hsCursor >= tubes::kHsCursorMax) hsCursorDir = -1;
                    if (hsCursor <= tubes::kHsCursorMin) hsCursorDir = 1;
                }
            } else if (hsActive) {
                // The applause plays out, and only then is the record
                // committed and the file written.
                hsHold -= dt;
                if (hsHold <= 0.0f) {
                    hsHold = 0.0f;
                    hiScores[hsBank].rows[hsRow - 1].setName(hsName);
                    saveHiScores();
                    hsActive = false;
                    stage = Stage::kTitle;
                    menu.raise();
                    playSong("TUBES.MUS");
                }
            }

            // `1b2e:0a11`'s slide drop: six frames, each held for ten
            // vertical retraces, and then it is done for the whole run.
            if (briefingUp && !slideDropped) {
                slideAccum += dt * tubes::kRetraceHz;
                while (slideAccum >= tubes::kSlideDropRetraces) {
                    slideAccum -= tubes::kSlideDropRetraces;
                    if (++slideFrame >= tubes::kSlideDropFrames) {
                        slideDropped = true;
                        break;
                    }
                }
            }

            // `1000:8c38`'s countdown ticks on its own, so the prompt expires
            // whether or not the player touches anything.
            if (sstage == tubes::SessionStage::kContinue &&
                continuePrompt.active()) {
                continueAccum += dt;
                if (continueAccum >= kContinueTickSeconds) {
                    continueAccum -= kContinueTickSeconds;
                    if (continuePrompt.tick(false, false) ==
                        tubes::ContinueResult::kDeclined) {
                        // Running out declines, and `1000:a69b` then leaves
                        // the loop with `gameOver` still set.
                        sstage = tubes::SessionStage::kFinished;
                        endSession();
                    }
                }
            }
            // One voice, so one sound a frame: a second event in the same
            // frame has already replaced the first inside Game, which is what
            // calling the driver's PlaySound twice does.
            const int8_t want = game->takeSound();
            // The sound is TAKEN either way, so F4 mutes without desyncing
            // anything - `1000:3859` toggles the driver, it does not stop the
            // game asking for sounds.
            if (soundOn && want >= 0 && want < tubes::sfx::kCount &&
                sounds[want].valid()) {
                music.playSound(&sounds[want]);
            }
        }

        // The viewer REPLACES the title screen while it is up, so it has to
        // come before the title draw - that path ends the frame with its own
        // `continue`.
        if (hsViewing) {
            // `1b2e:63f2`: whenever the effects voice reports itself idle the
            // clap starts again, so the applause carries the whole screen.
            if (soundOn && haveClapSound && !music.soundBusy()) {
                music.playSound(&clapSound);
            }
            // The give-up does exactly what a key does.
            hsViewTimer -= dt;
            if (hsViewTimer <= 0.0f) {
                if (hsViewPage == 0) {
                    hsViewPage = 1;
                    hsViewTimer = tubes::kHsViewSeconds;
                } else {
                    hsViewing = false;
                    playSong("TUBES.MUS");
                }
            }
            const tubes::HiScoreBank viewBank =
                hsViewPage == 0 ? tubes::HiScoreBank::kEndurance
                                : tubes::HiScoreBank::kWave;
            drawHiScoreViewer(screen, hiScores[viewBank],
                              tubes::kHsViewTitle[hsViewPage], &blackboard,
                              haveBlackboard, &slideBar, haveBar, headingFont,
                              haveHeading, scriptFont, haveScript);
            presentFrame();
            continue;
        }

        if (stage == Stage::kTitle) {
            drawTitle(screen, titleBg, titleFgScene, haveTitleBg && haveTitleFg,
                      menu, titleAtom, atoms, haveAtom, titleBall, stars,
                      haveStar, headingFont, haveHeading, titleFg, smallFont,
                      haveSmall);
            presentFrame();
            continue;
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
            return cell == tubes::kFlashium ? game->flashColour() : cell;
        };

        // `-0x189`, the hidden-atom modifier: "live through N atoms that are
        // HIDDEN UNTIL THEY LEAVE A TUBE". All six network draw sites carry
        //
        //     if hidden = 0 then Draw(ball[type], x, y)
        //                   else Draw(MYSTBALL,   x, y)
        //
        // and those six are the ONLY places in `1000:3a67` that test it - not
        // the tube's contents, not records 7..12, not the beaker grid. So the
        // atom keeps its real type underneath and the concealment ends the
        // moment it is caught, which is exactly what the briefing promises.
        // `MYSTBALL` is therefore a rendering state, not a nineteenth ball.
        const bool hideAtoms = game->objective().hiddenAtoms;

        auto drawAtom = [&](int col) {
            const tubes::Falling& a = game->atom(col);
            // `state > 2` is the original's own test, made at every one of the
            // six draw sites. States 0..2 are a free or parked slot.
            if (!a.drawn() || a.colour == tubes::kEmpty) return;
            const int8_t sprite = hideAtoms ? static_cast<int8_t>(tubes::kMystery)
                                            : ball(a.colour);
            if (!haveAtom[sprite]) return;
            screen.draw(atoms[sprite], a.x, a.y);
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
        const int tubeX = game->tubeX();
        if (haveFurn[kTestTubeShadow]) {
            screen.draw(furn[kTestTubeShadow], tubeX, kTubeY);
        }

        // The tube's contents carry their OWN positions now. They used to be
        // computed from the index, which was right at rest and impossible
        // during the tip - the animation moves the slots, and a caught atom
        // slides down to its own before that.
        for (const tubes::Falling& s : game->tubeAtoms()) {
            if (!haveAtom[ball(s.colour)]) continue;
            screen.draw(atoms[ball(s.colour)], s.x, s.y);
        }

        // `1000:5922` draws the tube from a four-entry sprite table indexed by
        // the animation phase. Only three sprites exist because phase 4 resets
        // to 1 before the frame is drawn, so it can never be the one selected.
        const int phase = game->tubePhase();
        if (haveTube[phase]) screen.draw(testTube[phase], tubeX, kTubeY);

        // Beaker shadow, then its contents, then the glass FRONT last - the
        // original draws BEAKER.CSP after the settled atoms, so the glass
        // overlaps the balls. The port used to draw it first.
        if (haveFurn[kBeakerShadow]) screen.draw(furn[kBeakerShadow], 186, 135);

        // The beaker grid, then the MARKER overlay, in `1000:598b`'s order.
        // The cell is used as the sprite index directly - that is the whole
        // point of the `type + 19 * fadeFrame` encoding, and it is why a
        // clearing atom animates with no branch anywhere in the draw.
        const tubes::Board& b = game->board();
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
            const tubes::Falling& f = game->atom(n);
            if (!f.drawn() || !haveAtom[ball(f.colour)]) continue;
            screen.draw(atoms[ball(f.colour)], f.x, f.y);
        }

        if (haveBeaker) screen.draw(beaker, kGridX - 4, kGridY);

        drawHud(screen, *game, bigFont, smallFont, haveBig, haveSmall);
        drawTaskDisplay(screen, *game, bigFont, haveBig, atoms, haveAtom, furn,
                        haveFurn, smallBall, haveSmallBall);

        // `1000:a5d2` shows the briefing INSTEAD of the play field, before
        // `1000:3a67` ever runs, so it simply replaces everything above.
        if (briefingUp) {
            const tubes::SlideFrame sp = slidePos();
            drawBriefing(screen, *game, &blackboard, haveBlackboard, headingFont,
                         smallFont, haveBig, haveSmall, atoms, haveAtom, furn,
                         haveFurn, briefDecor, sceneArt, sp.x, sp.y,
                         tubes::pointerFrameFor(profWave));
        }

        // The stats screen replaces the field; the banner, the Continue prompt
        // and the pause overlay go OVER whatever is already drawn, because
        // that is what the original does - none of the three clears first.
        if (sstage == tubes::SessionStage::kStats ||
            sstage == tubes::SessionStage::kContinue) {
            drawStats(screen, statsRows, &blackboard, haveBlackboard, sceneArt,
                      headingFont, smallFont, bigFont, haveHeading, haveSmall,
                      haveBig, tubes::pointerFrameFor(profWave));
        }
        if (sstage == tubes::SessionStage::kBanner) {
            // `1000:5ec9`: the F2 hint appears only on the abort arm, and only
            // when the mode is not attract and saving is enabled.
            drawBanner(screen, banner, headingFont, haveHeading, smallFont,
                       haveSmall,
                       banner == tubes::Banner::kAborted && gameMode != 0);
        }
        if (sstage == tubes::SessionStage::kContinue) {
            drawContinue(screen, continuePrompt.ticksLeft(), headingFont,
                         haveHeading, bigFont, haveBig);
        }
        if (hsActive) {
            drawScene(screen, &blackboard, haveBlackboard, sceneArt,
                      tubes::kSlideX, tubes::kSlideY, 0);
            drawHiScores(screen, hiScores[hsBank], headingFont, haveHeading,
                         scriptFont, haveScript, hsRow, hsName,
                         hsHold > 0.0f ? 0 : hsCursor);
        }
        if (paused) drawPaused(screen, headingFont, haveHeading);


        presentFrame();
    }

    music.stop();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
