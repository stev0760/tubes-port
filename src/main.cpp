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
#include <sstream>
#include <iterator>
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include "font.h"
#include "game.h"
#include "textscreen.h"
#include "gfx.h"
#include "hiscore.h"
#include "input.h"
#include "cutscene.h"
#include "ending.h"
#include "instructions.h"
#include "save.h"
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
    std::string dumpAnm;        // print a .ANM's runs, to diff the decoder
    std::string dumpSpr;        // print a .SPR's frame dimensions
    // Decode a TUBES.SAV and print every field, so the C++ decoder can be
    // diffed against `tools/sav_decode.py` rather than trusted.
    std::string dumpSave;
    bool playDemo = false;      // replay DEMO.SCR through the live loop
    bool demoTrace = false;     // run DEMO.SCR headless and print the spawns
    int randomTrace = 0;        // with --demo-trace: print the first N rolls
    std::string demoCsv;        // with --demo-trace: per-frame state, for the rig
    std::string gameBg = "GAMEBG1.GFX";   // backdrop, for matching a capture
    // Which build of Tubes to be. `--shareware` is the 25-wave edition, and
    // `--preview` opens its Preview Registered mode - menu arm 3, and the same
    // flag View Demo and the attract loop set. See edition.h.
    tubes::EditionState edition{};
    // Open the shareware exit screen directly, for capture. Like every other
    // harness flag it must never write to the game directory.
    bool exitScreen = false;
    double renderSeconds = 0;   // 0 = one pass, songs loop forever
    int wave = 0;               // 0 = Endurance; 1..75 starts Wave mode there
    int titlePage = -1;         // -1 off; 0 the bare title; 1..7 a menu page
    int hsPage = -1;            // -1 off; 0 Endurance, 1 Wave in the viewer
    bool f2 = false;            // open the F2 save screen, for capture
    bool rebind = false;        // open the rebinding screen, for capture
    bool graphics = false;      // open the graphics screen, for capture
    int instr = -1;             // open the Instructions on slide N, for capture
    bool credits = false;       // open the Credits, for capture
    // Harness only. `--screenshot` captures the first frame drawn, which can
    // never show a screen that is reached by PLAYING - the banners, the stats
    // screen and the Continue prompt are all past a game over. These two run
    // the real loop to get there instead of adding entry points that the
    // original does not have.
    // `[DS:0x0ce6]` is 40 in the image, and the player has said outright that
    // the half second between screens may be sped up or turned off. So the
    // knob exists and its DEFAULT is the original's number; 0 cuts instead.
    int fadeSteps = tubes::kFadeSteps;
    bool noSplash = false;      // skip the boot splashes outright
    int splashFrame = -1;       // capture this .ANM frame of the first splash
    int splash2Step = -1;       // capture this step of the second splash
    int cutscenePage = -1;      // capture this page of the opening cutscene
    int cutsceneTick = -1;      // ... at this tick of it, rather than midway
    int shotAfter = 0;          // present the screenshot after N live frames
    bool joke = false;          // force `1b2e:084e`, which is a 5% roll
    int ending = -1;            // open `1000:9499` at page 1 or 2
    std::string makeSave;       // write a TUBES.SAV for --wave N and exit
    bool hsEntry = false;       // open the high score ENTRY screen, 1000:96db
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
        } else if (a == "--dump-spr" && i + 1 < argc) {
            o.dumpSpr = argv[++i];
        } else if (a == "--dump-anm" && i + 1 < argc) {
            o.dumpAnm = argv[++i];
        } else if (a == "--dump-save" && i + 1 < argc) {
            o.dumpSave = argv[++i];
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
        } else if (a == "--f2") {
            o.f2 = true;
        } else if (a == "--rebind") {
            o.rebind = true;
        } else if (a == "--graphics") {
            o.graphics = true;
        } else if (a == "--credits") {
            o.credits = true;
        } else if (a == "--instructions") {
            o.instr = 0;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                o.instr = std::atoi(argv[++i]);
            }
        } else if (a == "--hiscores") {
            // The viewer's two pages, for capturing against the original:
            // `--hiscores` is Endurance and `--hiscores 1` is Wave.
            o.hsPage = 0;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                o.hsPage = std::atoi(argv[++i]);
            }
        } else if (a == "--hs-entry") {
            o.hsEntry = true;
        } else if (a == "--make-save" && i + 1 < argc) {
            o.makeSave = argv[++i];
        } else if (a == "--ending") {
            o.ending = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                o.ending = std::atoi(argv[++i]);
            }
        } else if (a == "--joke") {
            o.joke = true;
        } else if (a == "--screenshot-after" && i + 1 < argc) {
            o.shotAfter = std::atoi(argv[++i]);
        } else if (a == "--auto-advance") {
            o.autoAdvance = true;
        } else if (a == "--wave" && i + 1 < argc) {
            o.wave = std::atoi(argv[++i]);
        } else if (a == "--cutscene" && i + 1 < argc) {
            o.cutscenePage = std::atoi(argv[++i]);
        } else if (a == "--cutscene-tick" && i + 1 < argc) {
            o.cutsceneTick = std::atoi(argv[++i]);
        } else if (a == "--splash2" && i + 1 < argc) {
            o.splash2Step = std::atoi(argv[++i]);
        } else if (a == "--splash" && i + 1 < argc) {
            o.splashFrame = std::atoi(argv[++i]);
        } else if (a == "--exit-screen") {
            o.exitScreen = true;
            o.edition.edition = tubes::Edition::kShareware;
        } else if (a == "--shareware") {
            o.edition.edition = tubes::Edition::kShareware;
        } else if (a == "--preview") {
            // The Preview only exists in the shareware build, so asking for it
            // implies the edition rather than needing both flags.
            o.edition.edition = tubes::Edition::kShareware;
            o.edition.preview = true;
        } else if (a == "--no-splash") {
            o.noSplash = true;
        } else if (a == "--fade-steps" && i + 1 < argc) {
            o.fadeSteps = std::atoi(argv[++i]);
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
        "  --scale N         integer scale factor for this run, overriding the\n"
        "                    saved one (default: fit the display)\n"
        "  --graphics        open the port's Graphics Options screen\n"
        "  --screenshot FILE render one frame to a BMP and exit\n"
        "  --screenshot-after N  with it, capture after N frames of the LIVE\n"
        "                    loop at a fixed step - the only way to reach a\n"
        "                    screen that is past a game over\n"
        "  --auto-advance    press RETURN periodically, so a headless run\n"
        "                    walks through the screens that hold for a key\n"
        "  --auto N          simulate N scripted frames first (for testing)\n"
        "  --dump-anm NAME   print a .ANM's runs, to diff against\n"
        "                    tools/anm_decode.py RUNS\n"
        "  --dump-spr NAME   print a .SPR strip's frames\n"
        "                    tools/anm_decode.py RUNS\n"
        "  --no-splash       go straight to the title screen\n"
        "  --shareware       play the 25-wave shareware edition\n"
        "  --exit-screen     show TUBESEND.BIN, the shareware sign-off\n"
        "  --preview         its Preview Registered mode (implies --shareware)\n"
        "  --splash N        run the first splash and, with --screenshot,\n"
        "                    capture its Nth animation frame\n"
        "  --splash2 N       the same for the second: 0-5 the logo frames,\n"
        "                    6-10 the lightning, 11 the writing\n"
        "  --cutscene N      run the opening cutscene and, with --screenshot,\n"
        "                    capture page N (0-4)\n"
        "  --cutscene-tick T with it, capture at tick T of that page rather\n"
        "                    than midway - a tick is 10 retraces\n"
        "  --fade-steps N    length of the screen fade, in 70 Hz frames\n"
        "                    (default 40, the original's; 0 cuts instead)\n"
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
        "  --make-save FILE  with --wave N, write a TUBES.SAV holding that\n"
        "                    wave in Wave slot 1 and exit. Writes ONLY to the\n"
        "                    path given - never to the game directory.\n"
        "  --hs-entry        open the high score ENTRY screen, `1000:96db`\n"
        "  --ending [N]      open the wave-75 ending `1000:9499` at page N\n"
        "                    (1 the text, 2 the prize); implies a session\n"
        "  --joke            force `1b2e:084e`'s joke slide, which is a one in\n"
        "                    twenty roll per slide and fires at most once a run\n"
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
    // `TALK1..5.GFX`, the five mouths `1b2e:0cd1` cycles over his face.
    const tubes::Image* talk = nullptr;
    const bool* haveTalk = nullptr;
    // `1b2e:084e`'s joke slide: the wrong transparency and the face that
    // goes with it.
    const tubes::Image* flash = nullptr;
    bool haveFlash = false;
    const tubes::Image* pointerT = nullptr;
    bool havePointerT = false;
    // `JUMP1..3.GFX`, reached only by `1b2e:0656`'s `DS:0x20e3` arm - which
    // only the wave-75 ending ever sets.
    const tubes::Image* jump = nullptr;
    const bool* haveJump = nullptr;
};

// What the scene is doing right now, as opposed to what it is made of. Three
// of these move: the projector screen rolls down (`1b2e:0510`), the slide
// wobbles into place once per run (`1b2e:0a11`), and the professor waves
// (`1b2e:0e37`). Passing them as one struct keeps the four screens that share
// `drawScene` from each growing another argument every time one is found.
struct ScenePose {
    int frameH = tubes::kFrameH;      // the projector screen's rolled height
    int slideX = tubes::kSlideX;
    int slideY = tubes::kSlideY;
    int profFrame = 0;                // 0 standing, 1..3 POINTER1..3
    int mouthFrame = 0;               // 0 none, 1..5 TALK1..5 - `1b2e:0cd1`
    bool jokeSlide = false;           // FLASH.GFX is up  - `1b2e:084e`
    bool jokeFace = false;            // and POINTERT with it
    // `DS:0x20fc`, 1..3, and 0 for "he is not jumping". `1b2e:0656`'s third
    // arm - the one `DS:0x20e3` selects - stands him on his books somewhere
    // else and hops him, and the ending is the only caller that reaches it.
    int jumpFrame = 0;

    // Nothing a screen wants to write belongs on the slide in either of these:
    // `1b2e:0510` returns before its caller writes a word, and `1b2e:084e`
    // runs inside `1b2e:0a11` before the caller is reached at all.
    bool slideIsBusy() const { return frameH < tubes::kFrameH || jokeSlide; }
};

void drawScene(tubes::Screen& screen, const tubes::Image* board, bool haveBoard,
               const SceneArt& art, const ScenePose& pose) {
    const int slideX = pose.slideX;
    const int slideY = pose.slideY;
    const int profFrame = pose.profFrame;
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
    if (pose.jumpFrame > 0) {
        // `1b2e:0656`'s `DS:0x20e3` arm: BOOKS at its own place, then the hop
        // frame at `(267, 97 - 3 * frame)`. Both masked - the arm uses
        // `2321:0711` and `1b2e:0b8f` uses `2000:3921`, which is the same
        // routine. No `POINTER0` here at all; the jump frames are the whole
        // figure, 71 tall against the standing pose's 79.
        if (art.haveBooks) {
            screen.blit(*art.books, tubes::kJumpBooksX, tubes::kJumpBooksY);
        }
        const int f = pose.jumpFrame;
        if (art.haveJump && f >= 1 && f <= tubes::kJumpFrames &&
            art.haveJump[f - 1]) {
            screen.blit(art.jump[f - 1], tubes::kJumpX, tubes::jumpY(f));
        }
    } else {
    if (art.havePointer && art.havePointer[0]) {
        screen.blit(art.pointer[0], tubes::kProfX, tubes::kProfY);
    }
    if (art.havePointer && profFrame > 0 && profFrame < 4 &&
        art.havePointer[profFrame]) {
        screen.blit(art.pointer[profFrame], tubes::kProfX, tubes::kProfY);
    }
    }
    // `1b2e:084e` stamps `POINTERT` over whatever pose is up, which is why it
    // comes after both of the draws above and not instead of them.
    if (pose.jokeFace && art.havePointerT) {
        screen.blit(*art.pointerT, tubes::kProfX, tubes::kProfY);
    }
    // The mouth, `1b2e:0cd1`, stamped over his face at (276, 133) - a third
    // draw on top of the two above, and only while he is talking. The wave
    // frames carry their own mouth, so `ProfessorIdle` reports 0 for this the
    // moment the gesture starts.
    // ...and only while he is standing. The mouth's (276, 133) is nine right
    // and twelve down from the STANDING pose's origin; the jump arm puts him
    // at (267, 94..88) and there is nothing under (276, 133) but blackboard.
    // Structurally it cannot happen in the original either - the mouth is
    // drawn by `1b2e:0cd1` and the jump by `1b2e:0b8f`, and no screen runs
    // both - but the port drives one professor from two clocks, so it has to
    // be said. Reported from play: a mouth left floating beside him.
    if (pose.jumpFrame == 0 && art.haveTalk && pose.mouthFrame >= 1 &&
        pose.mouthFrame <= 5 && art.haveTalk[pose.mouthFrame - 1]) {
        screen.blit(art.talk[pose.mouthFrame - 1], tubes::kTalkX,
                    tubes::kTalkY);
    }

    // The frame behind the slide. Its HEIGHT is the one thing about it that
    // moves: `1b2e:0656` reads `DS:0xbba` for it and `1b2e:0510` steps that
    // word down the `kRollDown` table when the scene is first built.
    fillRect(tubes::kFrameX, tubes::kFrameY, tubes::kFrameW, pose.frameH,
             tubes::kFrameColour);
    // The roller bar rides the frame's bottom edge - `Draw(57, DS:0xbba + 26)`
    // in `1b2e:0656`, where `DS:0xbba` is the frame height as it rolls down.
    if (art.haveBar) {
        screen.blit(*art.bar, tubes::kBarX, pose.frameH + tubes::kBarDY);
    }
    // While the screen is still rolling there is no slide on it - `1b2e:0510`
    // draws only the growing rect and the bar, and the slide arrives with the
    // `1b2e:0a11` that follows. Putting it down early leaves a white panel
    // hanging in front of a screen that has not reached it yet.
    if (pose.frameH < tubes::kFrameH) return;

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

    // The wrong slide goes ON the blank one, in `1b2e:084e`'s own order:
    // FillRect, the four corners, then `FLASH.GFX` at the slide's origin.
    if (pose.jokeSlide && art.haveFlash) {
        screen.blit(*art.flash, slideX, slideY);
    }
}

void drawBriefing(tubes::Screen& screen, const tubes::Game& game,
                  const tubes::Image* bg, bool haveBg,
                  const tubes::Font& big, const tubes::Font& small,
                  bool haveBigF, bool haveSmallF,
                  const tubes::Sprite* atoms, const bool* haveAtom,
                  const tubes::Sprite* furn, const bool* haveFurn,
                  int8_t decorBall, const SceneArt& art,
                  const ScenePose& pose) {
    drawScene(screen, bg, haveBg, art, pose);
    if (pose.slideIsBusy()) return;

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
               bool haveNumber, const ScenePose& pose) {
    // `1000:8db4` blits the HELD image through `2321:068d` at (0, 12) and then
    // calls `1b2e:0a11`, the same classroom scene the briefing uses - so the
    // stats land on the blackboard's white slide, not on the play backdrop.
    // The held image is `BLACKBRD.GFX`: 48644 bytes is exactly 320 x 152 plus
    // a header, and 12 + 152 = 164 is the only placement that fits.
    //
    // The slide is always at rest here - the drop animation belongs to the
    // first briefing and `DS:0x210e` has long since been set by this point.
    // So is the projector screen: `1000:8da5` reaches the scene through
    // `1b2e:0656`, which reads `DS:0xbba` at rest, not through `1b2e:0510`.
    drawScene(screen, board, haveBoard, art, pose);
    if (pose.slideIsBusy()) return;

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

// The wave-75 ending, `1000:9499`. Two screens over the same classroom the
// stats and briefing use, with `DS:0x20e3` set so the professor hops.
//
//     page 1   `1b2e:0656`; `1b2e:0a11`; the heading and the story text
//     page 2   `1b2e:0a11` again - which wipes the slide - then PRIZE.GFX at
//              `((320 - w) div 2, (200 - h) div 2)`
//
// Each held by `1b2e:0b8f(0x1e)`, the jump wait: thirty seconds or a key.
void drawEnding(tubes::Screen& screen, int page, const tubes::Image* board,
                bool haveBoard, const SceneArt& art, const ScenePose& pose,
                const tubes::Font& heading, bool haveHeading,
                const tubes::Font& small, bool haveSmall,
                const tubes::Image* prize, bool havePrize) {
    drawScene(screen, board, haveBoard, art, pose);
    if (page >= 2) {
        // `1000:9644`: centred by size, not by a literal - 52 x 125 lands at
        // (134, 37), and computing it is what the original does.
        if (havePrize) {
            screen.blit(*prize, (tubes::kScreenWidth - prize->width) / 2,
                        (tubes::kScreenHeight - prize->height) / 2);
        }
        return;
    }
    for (int i = 0; i < tubes::kEndingLineCount; ++i) {
        const tubes::EndingLine& l = tubes::kEndingLines[i];
        const bool big = l.font == tubes::EndingFont::kHeading;
        if (big ? !haveHeading : !haveSmall) continue;
        const tubes::Font& f = big ? heading : small;
        if (l.centred) {
            tubes::drawTextCentred(screen, f, l.left, l.right, l.y, l.colour,
                                   l.mode, l.text);
        } else {
            tubes::drawText(screen, f, l.left, l.y, l.colour, l.mode, l.text);
        }
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

// `1000:9757`'s typing cursor: a 4 x 4 block whose colour walks 0x91..0x9e and
// back, one step a frame, so it pulses rather than blinks. The save screen's
// description field uses the same loop and therefore the same cursor.
void drawTypingCursor(tubes::Screen& screen, int cx, int cy, int phase) {
    uint8_t* px = screen.pixelsMutable();
    const uint8_t col = static_cast<uint8_t>(tubes::kHsCursorBase + phase);
    for (int yy = cy; yy < cy + tubes::kHsCursorSize; ++yy) {
        if (yy < 0 || yy >= tubes::kScreenHeight) continue;
        for (int xx = cx; xx < cx + tubes::kHsCursorSize; ++xx) {
            if (xx < 0 || xx >= tubes::kScreenWidth) continue;
            px[static_cast<size_t>(yy) * tubes::kScreenWidth + xx] = col;
        }
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
            drawTypingCursor(screen,
                             static_cast<int>(name.size()) * 8 +
                                 tubes::kHsCursorDX,
                             y + tubes::kHsCursorDY, cursorPhase);
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

// The F2 save screen, `1000:2dd0`'s save arm. It draws over the play field the
// frame loop left up - the original flips to the other video page and puts this
// on it, so the caller supplies whatever background it likes.
//
// `1b2e:52bf`'s slot list and this share their mode split: Endurance counts
// CHAINS and Wave counts WAVES, right down to the heading's x, so the two words
// end in the same column.
void drawSaveScreen(tubes::Screen& screen, const tubes::SaveBankData& bank,
                    tubes::SaveBank which, int selected, bool typing,
                    const std::string& editText,
                    const tubes::Font& heading, bool haveHeading,
                    const tubes::Font& script, bool haveScript,
                    const tubes::Sprite* marker, bool haveMarker) {
    if (haveHeading) {
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kSaveTitleY,
                               tubes::kSaveTitleColour, tubes::textmode::kPeak,
                               tubes::kSaveTitle);
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kSaveRuleY,
                               tubes::kSaveTitleColour, tubes::textmode::kPeak,
                               tubes::kSaveTitleRule);
        // 1000:30b6. The headings are in the heading font; only the rows are
        // cursive.
        tubes::drawText(screen, heading, tubes::kSaveRowX, tubes::kSaveHeadY,
                        tubes::kSaveRowColour, tubes::textmode::kPeak,
                        tubes::kSaveHeadDesc);
        tubes::drawText(screen, heading, tubes::kSaveRowX,
                        tubes::kSaveHeadRuleY, tubes::kSaveRowColour,
                        tubes::textmode::kPeak, tubes::kSaveHeadDescRule);
        const bool wave = which == tubes::SaveBank::kWave;
        const int hx = wave ? tubes::kSaveWaveX : tubes::kSaveChainsX;
        tubes::drawText(screen, heading, hx, tubes::kSaveHeadY,
                        tubes::kSaveRowColour, tubes::textmode::kPeak,
                        wave ? tubes::kSaveHeadWave : tubes::kSaveHeadChains);
        tubes::drawText(screen, heading, hx, tubes::kSaveHeadRuleY,
                        tubes::kSaveRowColour, tubes::textmode::kPeak,
                        wave ? tubes::kSaveHeadWaveRule
                             : tubes::kSaveHeadChainsRule);
    }

    if (haveScript) {
        for (int i = 1; i <= tubes::kSaveSlotsShown; ++i) {
            const tubes::SaveSlot& rec = bank.slots[i - 1];
            const int y = tubes::saveRowY(i);
            // 1000:316b: a live record shows its description, an empty one the
            // "( Available )" literal. The row being typed shows the live text.
            std::string left;
            const bool editing = typing && i == selected;
            if (editing) left = editText;
            else if (rec.live()) left = rec.description;
            else left = tubes::kSaveAvailable;
            // `1000:34ba` on entry and `1000:35d1` on every keystroke both
            // pass colour 0x0f, where the list rows are drawn in
            // `kSaveRowColour`. So the row being typed turns WHITE, and that
            // is the only thing on the screen that says the editor is open -
            // there is no cursor (see below). The port drew it in the row
            // colour, so selecting a slot looked like it had done nothing,
            // and a player reported exactly that: no way to tell it was
            // waiting for a new name.
            // `1000:34ba` and `1000:35d1` pass colour $0f AND MODE 0 - flat,
            // no colour walk - where the list rows walk. Keeping the rows'
            // mode with the new colour is what turned the edited line into a
            // scrambled ramp instead of white: `2000:35ec` steps the palette
            // index per scanline, and index 15 is the top of its ramp, so it
            // walked straight out of the greys into whatever follows them.
            tubes::drawText(screen, script, tubes::kSaveRowX, y,
                            editing ? tubes::kSaveTypingColour
                                    : tubes::kSaveRowColour,
                            editing ? tubes::textmode::kFlat
                                    : tubes::textmode::kPeak, left);
            // 1000:31a8 sits AFTER the two arms join, so the number is drawn
            // for every row - an empty slot shows a right-justified 0. The
            // port guarded it on `live()` and the capture said otherwise.
            const int x = which == tubes::SaveBank::kWave
                              ? tubes::kSaveWaveX : tubes::kSaveChainsX;
            tubes::drawText(screen, script, x, y, tubes::kSaveRowColour,
                            tubes::textmode::kPeak,
                            tubes::saveSlotDetail(which, rec));
            // NO CURSOR. The high score screen pulses a 4x4 block at
            // `1000:9757`; this loop has nothing of the kind - it draws the
            // characters and erases an 8-wide cell on backspace, and that is
            // all. It was given one by analogy for one revision, which is
            // exactly the kind of invention the prime directive forbids.
        }
    }

    // 1000:3357: SRBALL either side of the selected row.
    if (haveMarker) {
        const int y = tubes::saveRowY(selected) + tubes::kSaveMarkerDY;
        screen.draw(*marker, tubes::kSaveMarkerLeftX, y);
        screen.draw(*marker, tubes::kSaveMarkerRightX, y);
    }
}

// What a binding is CALLED. SDL owns these names, which is the whole reason
// `input.h` stores opaque integers.
std::string bindingLabel(const tubes::Binding& x) {
    std::string s;
    if (x.key != tubes::kUnbound) {
        s = SDL_GetScancodeName(static_cast<SDL_Scancode>(x.key));
    }
    if (x.pad != tubes::kUnbound) {
        const char* p = SDL_GameControllerGetStringForButton(
            static_cast<SDL_GameControllerButton>(x.pad));
        if (p && *p) {
            if (!s.empty()) s += " / ";
            s += p;
        }
    }
    return s.empty() ? std::string("(unbound)") : s;
}

// One Instructions slide, `1b2e:2d63`. The background is the CLASSROOM -
// `1b2e:0a11`, the same scene the briefing uses, projector slide and all - and
// the slide's text goes on the white sheet. That is why every x in the table
// is between 76 and 244: the sheet is 74..246.
//
// The two navigation lines are the exception. They are centred over the whole
// screen at y 184 and 192, which is BELOW the sheet, on the black.
void drawInstructionSlide(tubes::Screen& screen,
                          const tubes::InstructionSlide* pages, int count,
                          int slide,
                          const tubes::Image* board, bool haveBoard,
                          const SceneArt& art, const ScenePose& pose,
                          const tubes::Font& small, bool haveSmall,
                          const tubes::Font& big, bool haveBig,
                          const tubes::Sprite* atoms, const bool* haveAtom,
                          const tubes::Sprite* tube, const bool* haveTube,
                          const tubes::Sprite* furn, const bool* haveFurn) {
    drawScene(screen, board, haveBoard, art, pose);
    if (pose.slideIsBusy()) return;
    if (slide < 0 || slide >= count) return;
    // The navigation is drawn once by the original and never cleared, so it
    // belongs to the screen rather than to a page.
    const tubes::InstructionSlide& s = pages[slide];
    auto item = [&](const tubes::InstructionItem& it) {
        const tubes::Font& f = it.font ? big : small;
        const bool have = it.font ? haveBig : haveSmall;
        switch (it.kind) {
        case tubes::InstructionItem::kText:
            if (have) {
                tubes::drawText(screen, f, it.x, it.y,
                                static_cast<uint8_t>(it.colour), it.mode,
                                it.text);
            }
            break;
        case tubes::InstructionItem::kCentred:
            if (have) {
                tubes::drawTextCentred(screen, f, it.x, 319, it.y,
                                       static_cast<uint8_t>(it.colour),
                                       it.mode, it.text);
            }
            break;
        case tubes::InstructionItem::kAtom:
            // The two negatives are the slideshow's own sprites; everything
            // else is an atom type out of the game's ball table.
            if (it.colour == tubes::kInstrTestTube1) {
                if (haveTube[tubes::tubephase::kUpright]) {
                    screen.draw(tube[tubes::tubephase::kUpright], it.x, it.y);
                }
            } else if (it.colour == tubes::kInstrTestTubeS) {
                // TESTUBES.CSP - the tube's shadow, which the play field
                // already loads as furniture.
                if (haveFurn[kTestTubeShadow]) {
                    screen.draw(furn[kTestTubeShadow], it.x, it.y);
                }
            } else if (it.colour > 0 && it.colour < kCellStates &&
                       haveAtom[it.colour]) {
                screen.draw(atoms[it.colour], it.x, it.y);
            }
            break;
        }
    };
    for (const tubes::InstructionItem& n : tubes::kInstructionNav) item(n);
    for (int i = 0; i < s.count; ++i) item(s.items[i]);
}

// Rebinding the six controls. THE PORT'S OWN SCREEN - the original's third
// Game Options item loads a driver, which SDL makes meaningless; see input.h.
// It is drawn in the menu's own language so it does not look bolted on: the
// page title where a page title goes, a rule under it, and one row per
// control with the same colour the menu items use.
//
// It borrows from BOTH the classroom scene and the high score viewer and is
// quite the same as neither, because six labelled rows want more room than
// either was built for:
//
//   from `1b2e:0a11`   the blackboard at (0, 12) and the professor at his own
//                      (267, 121) - the "whole classroom", which the high
//                      score viewer deliberately does not have
//   from `1b2e:61b6`   the chalk panel over the board, the roller bar above
//                      it, the cursive rows and the centred title
//   its own            a NARROWER panel, so the professor is not painted over,
//                      and two columns instead of the viewer's name-and-score
//
// The projector slide is left out on purpose. `drawScene` always lays it down
// and it is only 172 wide - fine for a briefing's small-font prose, hopeless
// for "Button A" against "Left Ctrl / A".
void drawRebindScreen(tubes::Screen& screen, const tubes::Bindings& bind,
                      int row, bool waiting, const tubes::Image* board,
                      bool haveBoard, const SceneArt& art,
                      const tubes::Font& big, bool haveBig,
                      const tubes::Font& script, bool haveScript,
                      const tubes::Font& small, bool haveSmall, int profFrame) {
    screen.clear(0);
    if (haveBoard) screen.blit(*board, 0, tubes::kBoardY);

    // The chalk panel, `2321:060b`'s colour, across the board's full width -
    // the equations are wiped everywhere, not just behind the rows. A panel
    // that stopped short left chalk showing beside the professor, which read
    // as a mistake rather than as a board.
    constexpr int kPanelY = 30;
    constexpr int kPanelH = 126;
    constexpr int kPanelRight = tubes::kHsPanelX + tubes::kHsPanelW;
    uint8_t* px = screen.pixelsMutable();
    for (int y = kPanelY; y < kPanelY + kPanelH; ++y) {
        if (y < 0 || y >= tubes::kScreenHeight) continue;
        for (int x = tubes::kHsPanelX; x < kPanelRight; ++x) {
            if (x < 0 || x >= tubes::kScreenWidth) continue;
            px[static_cast<size_t>(y) * tubes::kScreenWidth + x] =
                tubes::kHsPanelColour;
        }
    }

    // The professor goes on AFTER the panel, so he stands in front of a clean
    // board rather than being wiped off it. Same two draws the scene makes:
    // the standing pose masked, then the wave frame stamped over his top half.
    if (art.havePointer && art.havePointer[0]) {
        screen.blit(art.pointer[0], tubes::kProfX, tubes::kProfY);
    }
    if (art.havePointer && profFrame > 0 && profFrame < 4 &&
        art.havePointer[profFrame]) {
        screen.blit(art.pointer[profFrame], tubes::kProfX, tubes::kProfY);
    }
    // The roller bar rides the top of the BOARD rather than the top of the
    // panel. The viewer puts its title across the bar and gets away with it in
    // blue; in red on grey it is a struggle to read, and there is no room to
    // clear a 16-tall font between the board's top edge at 12 and the panel.
    // So the bar goes up and the title comes inside the panel.
    if (art.haveBar) {
        screen.blit(*art.bar, tubes::kHsViewBarX, tubes::kBoardY + 1);
    }

    if (haveBig) {
        // Centred over the PANEL rather than the screen, or the professor
        // pushes the title off-centre.
        tubes::drawTextCentred(screen, big, tubes::kHsPanelX, kPanelRight, 32,
                               tubes::kHsTitleColour, tubes::textmode::kPeak,
                               "Redefine Controls");
    }

    constexpr int kRow0 = 52;
    constexpr int kPitch = 15;
    constexpr int kNameX = 22;
    constexpr int kBindX = 112;
    for (int i = 0; i < tubes::kGameButtons; ++i) {
        const int y = kRow0 + i * kPitch;
        // The row being bound says so instead of showing its binding, which is
        // also how the player knows the next press is being taken.
        const bool armed = waiting && i == row;
        // `0x2f` is the game's red - it is what draws "Save Game", the abort
        // banner and the high score entry screen's own title, and measured off
        // a capture it is (215, 0, 0). Far more visible on the board's green
        // than `0x9f`, the blue the viewer's heading uses.
        const uint8_t colour = (i == row) ? tubes::kHsTitleColour
                                          : tubes::kHsRowColour;
        // Names in the viewer's cursive, because they are words on a
        // blackboard; bindings in the heading font, because "Left Ctrl" is a
        // label off a keyboard and has to be read exactly.
        if (haveScript) {
            tubes::drawText(screen, script, kNameX, y, colour,
                            tubes::textmode::kPeak, tubes::kGameButtonNames[i]);
        }
        if (haveBig) {
            tubes::drawText(screen, big, kBindX, y, colour,
                            tubes::textmode::kPeak,
                            armed ? "press a key" : bindingLabel(bind.b[i]));
        }
    }
    // The hint goes where the Instructions and Credits put theirs: on the
    // BLACK FLOOR below the board, in `0x76` cyan, in TINY6X8, centred over
    // the whole screen. That is the game's own convention for "how to work
    // this screen", and off the green it needs no help to be read.
    if (haveSmall) {
        tubes::drawTextCentred(screen, small, 0, 319, tubes::kInstrNavY,
                               tubes::kInstrNavColour, tubes::textmode::kFadeUp,
                               "Enter binds - Esc exits");
    }
}

// The display options. ALSO THE PORT'S OWN SCREEN, and for the same reason the
// rebinding screen is - see `input.h`. It is deliberately the rebinding
// screen's twin: the same blackboard, the same chalk panel, the same professor
// standing in front of it, the same two fonts doing the same two jobs, and the
// same hint on the black floor. Two screens the original never had should at
// least look like each other, and like the game.
//
// The only structural difference is that a row here has a VALUE that changes
// in place rather than a binding captured from a keypress, so Left and Right
// work the row and Enter is a synonym for Right. That is also why there is no
// armed state: nothing here waits on a second press.
void drawGraphicsScreen(tubes::Screen& screen, const tubes::GraphicsOptions& g,
                        int row, const tubes::Image* board, bool haveBoard,
                        const SceneArt& art, const tubes::Font& big,
                        bool haveBig, const tubes::Font& script,
                        bool haveScript, const tubes::Font& small,
                        bool haveSmall, int profFrame) {
    screen.clear(0);
    if (haveBoard) screen.blit(*board, 0, tubes::kBoardY);

    constexpr int kPanelY = 30;
    constexpr int kPanelH = 126;
    constexpr int kPanelRight = tubes::kHsPanelX + tubes::kHsPanelW;
    uint8_t* px = screen.pixelsMutable();
    for (int y = kPanelY; y < kPanelY + kPanelH; ++y) {
        if (y < 0 || y >= tubes::kScreenHeight) continue;
        for (int x = tubes::kHsPanelX; x < kPanelRight; ++x) {
            if (x < 0 || x >= tubes::kScreenWidth) continue;
            px[static_cast<size_t>(y) * tubes::kScreenWidth + x] =
                tubes::kHsPanelColour;
        }
    }

    if (art.havePointer && art.havePointer[0]) {
        screen.blit(art.pointer[0], tubes::kProfX, tubes::kProfY);
    }
    if (art.havePointer && profFrame > 0 && profFrame < 4 &&
        art.havePointer[profFrame]) {
        screen.blit(art.pointer[profFrame], tubes::kProfX, tubes::kProfY);
    }
    if (art.haveBar) {
        screen.blit(*art.bar, tubes::kHsViewBarX, tubes::kBoardY + 1);
    }

    if (haveBig) {
        tubes::drawTextCentred(screen, big, tubes::kHsPanelX, kPanelRight, 32,
                               tubes::kHsTitleColour, tubes::textmode::kPeak,
                               "Graphics Options");
    }

    // Five rows against the rebinding screen's six, so they start lower and
    // sit on the same pitch - the two screens should not appear to jump when
    // the player moves between them.
    constexpr int kRow0 = 60;
    constexpr int kPitch = 15;
    constexpr int kNameX = 22;
    constexpr int kValueX = 150;
    for (int i = 0; i < tubes::kGraphicsRows; ++i) {
        const int y = kRow0 + i * kPitch;
        const uint8_t colour = (i == row) ? tubes::kHsTitleColour
                                          : tubes::kHsRowColour;
        if (haveScript) {
            tubes::drawText(screen, script, kNameX, y, colour,
                            tubes::textmode::kPeak, tubes::kGraphicsRowNames[i]);
        }
        if (haveBig) {
            tubes::drawText(
                screen, big, kValueX, y, colour, tubes::textmode::kPeak,
                tubes::graphicsValueLabel(g, static_cast<tubes::GraphicsRow>(i))
                    .c_str());
        }
    }
    if (haveSmall) {
        tubes::drawTextCentred(screen, small, 0, 319, tubes::kInstrNavY,
                               tubes::kInstrNavColour, tubes::textmode::kFadeUp,
                               "Left and Right change - Esc exits");
    }
}

// The pause overlay, `1000:3916`. Same two rows as a banner, and the loop is
// blocked entirely while it is up.
// How the framebuffer gets put on the window. THE PORT'S, entirely - see the
// display options in `input.h` - and file-scope rather than a parameter
// because the blocking screens (both splashes, every fade) present without
// going round the frame loop, and threading one struct through all ten call
// sites would say nothing that this comment does not.
//
// It is written once, from the settings, and then only by the graphics screen.
tubes::GraphicsOptions g_display;

// The shareware exit screen, `TUBESEND.BIN`, presented at 640 x 400.
//
// The ONE screen this port presents that the original does not: the shareware
// build `Move`s this dump to 0xB800 and quits, leaving the banner on the shell
// with the DOS prompt landing in the two rows the file deliberately omits. A
// windowed port has no shell to leave it on, so it draws it and holds it until
// a key. That hold is the invented part and the only one - see textscreen.h.
//
// It goes through the SAME `presentRect` as everything else, and gets fullscreen,
// window scale, 4:3 correction and scanlines for free, because 640 x 400 and
// 320 x 200 have the identical 1.6 aspect and therefore the identical
// destination rectangle. Only the source texture differs.
//
// The reveal is `23e7:0097`'s fade, run on the text palette rather than a
// game one - the fade is a graphics-unit routine that every screen calls, so
// using it here is reusing the original's own mechanism rather than imitating
// it.
void runExitScreen(SDL_Window* win, SDL_Renderer* ren, const tubes::TextScreen& ts,
                   int fadeSteps, const std::string& screenshot) {
    std::vector<uint8_t> indexed;
    ts.render(indexed);

    // The text palette in the raw 6-bit form `fadePalette` expects, so the
    // ramp arithmetic is the game's and not a second implementation of it.
    tubes::Bytes raw(768, 0);
    for (int i = 0; i < 16; ++i) {
        raw[i * 3 + 0] = tubes::kTextPalette[i][0];
        raw[i * 3 + 1] = tubes::kTextPalette[i][1];
        raw[i * 3 + 2] = tubes::kTextPalette[i][2];
    }

    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         tubes::kTextScreenW, tubes::kTextScreenH);
    if (!tex) return;
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);

    std::vector<uint8_t> rgba(static_cast<size_t>(tubes::kTextScreenW) *
                              tubes::kTextScreenH * 4);
    auto present = [&](const tubes::Palette& pal) {
        for (size_t i = 0; i < indexed.size(); ++i) {
            const uint8_t* c = pal.rgb[indexed[i]];
            // 6-bit DAC to 8-bit, the same `v * 255 / 63` the rest of the port
            // uses - see the grey-tolerance note in docs/debug-rig.md for why
            // that expansion and not `v << 2`.
            rgba[i * 4 + 0] = static_cast<uint8_t>(c[0] * 255 / 63);
            rgba[i * 4 + 1] = static_cast<uint8_t>(c[1] * 255 / 63);
            rgba[i * 4 + 2] = static_cast<uint8_t>(c[2] * 255 / 63);
            rgba[i * 4 + 3] = 255;
        }
        SDL_UpdateTexture(tex, nullptr, rgba.data(), tubes::kTextScreenW * 4);
        int winW = 0, winH = 0;
        SDL_GetRendererOutputSize(ren, &winW, &winH);
        const tubes::DisplayRect r = tubes::presentRect(winW, winH, g_display);
        const SDL_Rect dst{r.x, r.y, r.w, r.h};
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        if (g_display.scanlines && dst.h >= tubes::kScreenHeight * 2) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 64);
            for (int y = dst.y + 1; y < dst.y + dst.h; y += 2) {
                SDL_Rect line{dst.x, y, dst.w, 1};
                SDL_RenderFillRect(ren, &line);
            }
        }
        SDL_RenderPresent(ren);
    };

    for (int i = 0; i <= fadeSteps; ++i) {
        present(tubes::fadePalette(raw, i, fadeSteps ? fadeSteps : 1));
        if (fadeSteps > 0) SDL_Delay(14);          // one 70 Hz retrace
    }
    const tubes::Palette lit = tubes::textPalette();
    present(lit);

    if (!screenshot.empty()) {
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
            rgba.data(), tubes::kTextScreenW, tubes::kTextScreenH, 32,
            tubes::kTextScreenW * 4, SDL_PIXELFORMAT_RGBA32);
        if (surf) {
            SDL_SaveBMP(surf, screenshot.c_str());
            SDL_FreeSurface(surf);
            std::printf("wrote %s\n", screenshot.c_str());
        }
        SDL_DestroyTexture(tex);
        return;
    }

    // Hold until a key. The original does not wait - it has already exited and
    // the banner simply persists - so this is the port's substitute for a
    // screen that outlives the program.
    bool waiting = true;
    while (waiting) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT || ev.type == SDL_KEYDOWN ||
                ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONDOWN) {
                waiting = false;
            }
            if (ev.type == SDL_WINDOWEVENT) present(lit);
        }
        SDL_Delay(16);
    }
    SDL_DestroyTexture(tex);
    (void)win;
}

void presentScreen(SDL_Renderer* ren, SDL_Texture* tex,
                   const tubes::Screen& screen, const tubes::Palette& pal,
                   std::vector<uint8_t>& rgba) {
    screen.toRgba(pal, rgba);
    SDL_UpdateTexture(tex, nullptr, rgba.data(), tubes::kScreenWidth * 4);

    int winW = 0, winH = 0;
    SDL_GetRendererOutputSize(ren, &winW, &winH);
    // The arithmetic is in `input.cpp`, with no SDL in it, so the tests get at
    // the scaling and letterbox rules directly.
    const tubes::DisplayRect r = tubes::presentRect(winW, winH, g_display);
    const SDL_Rect dst{r.x, r.y, r.w, r.h};

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, &dst);

    // Scanlines: one dark line per OUTPUT row pair, drawn over the image
    // rather than baked into it, so the framebuffer a screenshot captures is
    // untouched. Skipped below 2x, where every second row would be half the
    // picture.
    if (g_display.scanlines && dst.h >= tubes::kScreenHeight * 2) {
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 64);
        for (int y = dst.y + 1; y < dst.y + dst.h; y += 2) {
            SDL_Rect line{dst.x, y, dst.w, 1};
            SDL_RenderFillRect(ren, &line);
        }
    }
    SDL_RenderPresent(ren);
}

// Push the display options at the window and the renderer. Everything else in
// `GraphicsOptions` is read at present time; these two are state SDL holds.
void applyDisplayOptions(SDL_Window* win, SDL_Renderer* ren,
                         const tubes::GraphicsOptions& g) {
    g_display = g;
    if (!win || !ren) return;
    // Desktop fullscreen, not a mode set: the port has no business changing
    // the display's resolution for a 320x200 image it is going to letterbox
    // anyway, and a borderless desktop window alt-tabs cleanly.
    SDL_SetWindowFullscreen(win, g.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP
                                              : 0);
    if (!g.fullscreen && g.scale > 0) {
        SDL_SetWindowSize(win, tubes::kScreenWidth * g.scale,
                          tubes::displayUnitHeight(g) * g.scale);
    }
    SDL_RenderSetVSync(ren, g.vsync ? 1 : 0);
}

void saveBmp(std::vector<uint8_t>& rgba, const std::string& path) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        rgba.data(), tubes::kScreenWidth, tubes::kScreenHeight, 32,
        tubes::kScreenWidth * 4, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return;
    SDL_SaveBMP(surf, path.c_str());
    SDL_FreeSurface(surf);
    std::printf("wrote %s\n", path.c_str());
}

// ---- The boot splashes -----------------------------------------------------
//
// `1b2e:11b0` is the whole sequence and it is five lines:
//
//     SetMode13h;                                { 23df:0000 }
//     k := SoftwareCreations;                    { 21d5:007b }
//     if (k <> 1) and (k <> 2) then AbsoluteMagic;   { 2178:00eb }
//
// so a skip during the FIRST splash takes the second one with it. The codes
// are the input driver's: 1 is Enter or Space, 2 is ESC.
//
// `23e7:0024` is `Delay(n)` and its unit is the vertical retrace - the body is
// `23e7:0016` (wait for the end of one vblank, then for the start of the next)
// with `LOOP` around it. So every hold in these screens is n/70 s.

// One retrace, near enough. The blocking screens are the only place the port
// paces on the 70 Hz retrace rather than the 16.11 Hz game frame; everything
// else runs off the simulation clock.
void waitRetraces(int n) {
    if (n > 0) SDL_Delay(static_cast<Uint32>(n * 1000 / 70));
}

// The player's skip.
//
// The original reads its input driver only in the tail loop of each splash,
// but through a BUFFERED read - `[DS:0x234e]` (ClearKeyBuffer) immediately
// after the wait is the tell - so a press during the animation still counts,
// just not until the next poll. That made ESC feel dead for up to a second,
// and the player asked for it to cut in at once.
//
// So `pumped()` is checked everywhere the screen would otherwise block: the
// fade steps, the animation frames and the holds. This is a DELIBERATE
// departure from the original, agreed with the player, and the only one in
// these two screens - the pacing, the order and every literal are untouched.
struct SkipWatch {
    int key = 0;                        // 1 Enter/Space, 2 ESC, 0 nothing yet

    void pump() {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { key = 2; continue; }
            // A pad works here too, and it has to: these screens run before
            // the frame loop exists, so nothing else would see the button.
            // B and Back are the ESC of a controller; anything else is Enter.
            if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
                const int b = ev.cbutton.button;
                key = (b == SDL_CONTROLLER_BUTTON_B ||
                       b == SDL_CONTROLLER_BUTTON_BACK) ? 2 : 1;
                continue;
            }
            if (ev.type != SDL_KEYDOWN) continue;
            const SDL_Keycode k = ev.key.keysym.sym;
            if (k == SDLK_ESCAPE) key = 2;
            else if (k == SDLK_RETURN || k == SDLK_SPACE) key = 1;
        }
    }
    int take() {
        const int k = key;
        key = 0;
        return k;
    }
    // Pump and report. Anything that waits calls this rather than sleeping.
    bool pumped() {
        pump();
        return key != 0;
    }
};

// A hold that a keypress can cut short. `n` retraces, polled once each.
bool holdRetraces(int n, SkipWatch& skip) {
    for (int i = 0; i < n; ++i) {
        if (skip.pumped()) return true;
        SDL_Delay(1000 / 70);
    }
    return skip.pumped();
}

// `21ea:0690` is `SetFrameRate(fps)`: it computes `145 div fps` and programs
// that as the timer period, so what the game actually runs at is
// `145 / (145 div fps)`. That is where 16.11 Hz comes from - the session asks
// for 16 (`1000:44d8`), 145 div 16 is 9, and 145/9 is 16.11. Both of the
// Absolute Magic splash's phases set it, to 9 and to 4.
//
// The same hold as above, but for a whole game frame.
bool holdGameFrame(int fps, SkipWatch& skip) {
    return holdRetraces(70 * (145 / (fps > 0 ? fps : 1)) / 145, skip);
}

// The palette ramp both splashes fade with. They have their own .PAL, so this
// takes the raw bytes rather than reaching for the game's.
bool runSplashFade(SDL_Renderer* ren, SDL_Texture* tex,
                   const tubes::Screen& screen, const tubes::Bytes& palRaw,
                   std::vector<uint8_t>& rgba, int steps, bool in,
                   SkipWatch& skip) {
    if (steps <= 0) {
        // The fade is off. Cut to the screen, or to black on the way out.
        tubes::Palette pal;
        std::string err;
        if (in) tubes::loadPalette(palRaw, pal, err);
        presentScreen(ren, tex, screen, pal, rgba);
        return skip.pumped();
    }
    for (int i = 0; i <= steps; ++i) {
        const int n = in ? i : steps - i;
        presentScreen(ren, tex, screen, tubes::fadePalette(palRaw, n, steps),
                      rgba);
        // A skip cuts a fade-IN short - there is no sense revealing a screen
        // the player has already dismissed - but never a fade-OUT, which has
        // to reach black or the next screen starts up lit.
        if (skip.pumped() && in) return true;
        SDL_Delay(1000 / 70);
    }
    return skip.key != 0;
}

// `21d5:007b`, the Software Creations splash. Returns the key that ended it.
//
//     Clear;                                     { 23df:0012 }
//     SetPalette(SOFT.PAL);                      { blanks the DAC as it stores }
//     Draw(0, 0, SOFT.GFX);                      { 23df:0022 }
//     FadeIn;                                    { 23e7:0097 }
//     Delay(10);
//     PlayAnm(SOFT.ANM);                         { 21d5:0000, nested }
//     n := 7;
//     repeat Delay(10); Dec(n); k := ReadInput until (k in [1,2]) or (n = 0);
//     FadeOut;  ClearKeyBuffer;
//
// The whole body sits under `if [DS:0x2a04] = 0`, a flag every screen function
// in the game tests and none of them writes - so it is set elsewhere, most
// likely when a resource fails to load. The port's equivalent is simply that
// the resources are there: if any of the three is missing the splash is
// skipped rather than half drawn.
int runSoftwareCreationsSplash(const tubes::Archive& res, SDL_Renderer* ren,
                               SDL_Texture* tex, tubes::Screen& screen,
                               std::vector<uint8_t>& rgba, int fadeSteps,
                               SkipWatch& skip, int shotFrame,
                               const std::string& shotPath) {
    tubes::Bytes palRaw, gfxRaw, anmRaw;
    tubes::Image still;
    std::vector<tubes::AnimFrame> anim;
    std::string err;
    if (!res.read("SOFT.PAL", palRaw, err) || palRaw.size() != 768 ||
        !res.read("SOFT.GFX", gfxRaw, err) ||
        !tubes::decodeGfx(gfxRaw, still, err) ||
        !res.read("SOFT.ANM", anmRaw, err) ||
        !tubes::decodeAnm(anmRaw, anim, err)) {
        return 0;
    }

    screen.clear(0);
    screen.blit(still, 0, 0);

    tubes::Palette pal;
    tubes::loadPalette(palRaw, pal, err);
    // Every stage below ends the screen the moment a key arrives, and the
    // fade-out always runs - the DAC has to reach black either way.
    auto leave = [&]() {
        const int k = skip.take();
        runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
        return k == 0 ? 2 : k;
    };
    if (runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, true, skip)) {
        return leave();
    }
    if (holdRetraces(10, skip)) return leave();

    // `21d5:0000`: three retraces a frame, and each frame paints only what it
    // changes over the still image already on screen.
    for (const tubes::AnimFrame& frame : anim) {
        if (holdRetraces(3, skip)) return leave();
        uint8_t* px = screen.pixelsMutable();
        for (const tubes::AnimFrame::Run& r : frame.runs) {
            for (size_t i = 0; i < r.pixels.size(); ++i) {
                const int d = r.offset + static_cast<int>(i);
                if (d >= 0 && d < tubes::kScreenWidth * tubes::kScreenHeight) {
                    px[d] = r.pixels[i];
                }
            }
        }
        presentScreen(ren, tex, screen, pal, rgba);
        // `--splash N --screenshot FILE`: capture the animation's Nth frame,
        // which is how this screen gets diffed against the original.
        if (shotFrame >= 0 &&
            shotFrame == static_cast<int>(&frame - anim.data())) {
            saveBmp(rgba, shotPath);
            return 2;
        }
    }

    // Seven holds of ten retraces - one second - and the only place the
    // ORIGINAL looks at the keyboard.
    for (int n = 7; n > 0; --n) {
        if (holdRetraces(10, skip)) return leave();
    }

    runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
    return 0;
}

// `2178:00eb`, the Absolute Magic splash - the second and much the larger of
// the two. Nine resources: `INTRO.PAL`, `CLOUD.GFX`, `AMWRITE.GFX`,
// `AMLOGO.SPR`, `LIGHTN.SPR`, `AMTHEME.MUS` and the three sounds.
//
// The backdrop is built rather than loaded. `2178:0000` clears a 64,000 byte
// buffer, copies `CLOUD.GFX` (320x83) in at offset 0, and then writes the SAME
// bytes again DESCENDING from offset 63,999 - so the bottom of the screen is
// the cloud band rotated 180 degrees, and rows 83..116 stay black. `21d0:0000`
// then de-chunks it into Mode X planes and `2321:0792` blits it. The port
// composes the same picture straight into its chunky framebuffer.
//
// Three phases, and each one sets the frame rate first:
//
//   * the logo arrives - `WOOSH.SFX`, then `AMLOGO.SPR`'s six frames centred,
//     20x20 growing to 172x127, at 9 fps;
//   * five lightning strikes - `LIGHTN.SFX` and one `LIGHTN.SPR` frame each,
//     at five hardcoded positions, at 4 fps, each with a WHITE FLASH: the
//     splash allocates its own 768-byte palette, fills it with 63s, and
//     uploads it around the page flip before restoring `INTRO.PAL`;
//   * the writing - `ABSMAGIC.SFX`, the page cleared to black, the logo and
//     `AMWRITE.GFX` centred at (72, 88), then six holds of ten retraces.
//
// Index 0 is transparent, read out of `2321:09d0`'s inner loop (`OR AL,AL;
// JZ`) rather than assumed from the way the sprites look.
int runAbsoluteMagicSplash(const tubes::Archive& res, SDL_Renderer* ren,
                           SDL_Texture* tex, tubes::Screen& screen,
                           std::vector<uint8_t>& rgba, int fadeSteps,
                           SkipWatch& skip, tubes::MusicPlayer& music,
                           bool musicOn, bool soundOn, int shotStep,
                           const std::string& shotPath) {
    tubes::Bytes palRaw, cloudRaw, writeRaw, logoRaw, boltRaw, song;
    tubes::Image cloud, writing;
    std::vector<tubes::Image> logo, bolts;
    std::string err;
    if (!res.read("INTRO.PAL", palRaw, err) || palRaw.size() != 768 ||
        !res.read("CLOUD.GFX", cloudRaw, err) ||
        !tubes::decodeGfx(cloudRaw, cloud, err) ||
        !res.read("AMWRITE.GFX", writeRaw, err) ||
        // PLANAR, though it carries the 0xE5 prefix - `2321:0948` is what
        // draws it, and that routine walks four planes. See GfxLayout.
        !tubes::decodeGfx(writeRaw, writing, err,
                          tubes::GfxLayout::kPlanar) ||
        !res.read("AMLOGO.SPR", logoRaw, err) ||
        !tubes::decodeSpr(logoRaw, logo, err) ||
        !res.read("LIGHTN.SPR", boltRaw, err) ||
        !tubes::decodeSpr(boltRaw, bolts, err) ||
        logo.empty() || bolts.empty()) {
        return 0;
    }
    for (tubes::Image& im : logo) im.transparent = 0;
    for (tubes::Image& im : bolts) im.transparent = 0;
    writing.transparent = 0;

    // The three sounds are loaded here rather than from the atom-indexed
    // table.
    // The voice MUST be silenced before the sounds below go out of scope:
    // the audio callback holds a bare pointer at whichever one is playing.
    struct Silence {
        tubes::MusicPlayer& m;
        ~Silence() { m.stopSound(); }
    } silence{music};
    tubes::Sound woosh, bolt, magic;
    tubes::Bytes raw;
    const bool haveSfx =
        soundOn &&
        res.read("WOOSH.SFX", raw, err) && tubes::decodeSfx(raw, woosh, err) &&
        res.read("LIGHTN.SFX", raw, err) && tubes::decodeSfx(raw, bolt, err) &&
        res.read("ABSMAGIC.SFX", raw, err) && tubes::decodeSfx(raw, magic, err);

    tubes::Palette pal;
    tubes::loadPalette(palRaw, pal, err);
    // The all-63 palette the splash fills with `FillChar(p^, 768, 63)`.
    const tubes::Bytes whiteRaw(768, 0x3f);
    tubes::Palette white;
    tubes::loadPalette(whiteRaw, white, err);

    tubes::Screen bg;
    bg.clear(0);
    bg.blit(cloud, 0, 0);
    {
        uint8_t* px = bg.pixelsMutable();
        const int last = tubes::kScreenWidth * tubes::kScreenHeight - 1;
        for (size_t i = 0; i < cloud.pixels.size(); ++i) {
            const int d = last - static_cast<int>(i);
            if (d >= 0) px[d] = cloud.pixels[i];
        }
    }

    screen = bg;
    if (musicOn && music.isOpen() && res.read("AMTHEME.MUS", song, err)) {
        music.play(song, err);
    }
    auto leave = [&]() {
        const int k = skip.take();
        runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
        return k == 0 ? 2 : k;
    };
    if (runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, true, skip)) {
        return leave();
    }
    if (holdRetraces(30, skip)) return leave();

    const tubes::Image& big = logo.back();
    const int bigX = (tubes::kScreenWidth - big.width) / 2;
    const int bigY = (tubes::kScreenHeight - big.height) / 2;

    if (haveSfx) music.playSound(&woosh);
    for (size_t f = 0; f < logo.size(); ++f) {
        screen = bg;
        const tubes::Image& im = logo[f];
        screen.blit(im, (tubes::kScreenWidth - im.width) / 2,
                    (tubes::kScreenHeight - im.height) / 2);
        presentScreen(ren, tex, screen, pal, rgba);
        if (shotStep == static_cast<int>(f)) { saveBmp(rgba, shotPath); return 2; }
        if (holdGameFrame(9, skip)) return leave();   // `SetFrameRate(9)`
    }

    // `2178:0424` onward: five strikes, one arm each, coordinates as literals.
    static const int kBoltX[5] = {0x3f, 0x09, 0x11c, 0x2e, 0xb6};
    static const int kBoltY[5] = {0x07, 0xa4, 0x47, 0x32, 0x0f};
    for (size_t n = 0; n < bolts.size() && n < 5; ++n) {
        if (haveSfx) music.playSound(&bolt);
        screen = bg;
        screen.blit(bolts[n], kBoltX[n], kBoltY[n]);
        screen.blit(big, bigX, bigY);
        // `SetDAC(white)`, page flip, `SetDAC(INTRO.PAL)` - and `23e7:003d`
        // waits a retrace before each upload, so the flash is that long.
        presentScreen(ren, tex, screen, white, rgba);
        waitRetraces(1);
        presentScreen(ren, tex, screen, pal, rgba);
        if (shotStep == static_cast<int>(n) + 6) {
            saveBmp(rgba, shotPath);
            return 2;
        }
        if (holdGameFrame(4, skip)) return leave();   // `SetFrameRate(4)`
    }

    if (holdRetraces(15, skip)) return leave();
    if (haveSfx) music.playSound(&magic);
    screen.clear(0);                            // `2321:01e6`, the page clear
    screen.blit(big, bigX, bigY);
    screen.blit(writing, 0x48, 0x58);
    presentScreen(ren, tex, screen, pal, rgba);
    if (shotStep == 11) { saveBmp(rgba, shotPath); return 2; }
    for (int n = 6; n > 0; --n) {
        if (holdRetraces(10, skip)) return leave();
    }

    runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
    return 0;
}

// ---- The opening cutscene, `1b2e:1651` -------------------------------------
//
// Five pages of the game's own story. The page data is generated - see
// cutscene.h - so what is here is only the machinery: the two-track player,
// the panel, and the frame clock.

// `2321:0ac0`, the bevelled panel the story text sits in.
void drawPanel(tubes::Screen& screen, int x, int y, int w, int h) {
    uint8_t* px = screen.pixelsMutable();
    for (const tubes::PanelFill& f : tubes::kPanelFills) {
        const int rx = x + (f.fromRight ? w : 0) + f.dx;
        const int ry = y + (f.fromBottom ? h : 0) + f.dy;
        const int rw = f.useW ? w + f.dw : 1;
        const int rh = f.useH ? h + f.dh : 1;
        for (int yy = ry; yy < ry + rh; ++yy) {
            if (yy < 0 || yy >= tubes::kScreenHeight) continue;
            for (int xx = rx; xx < rx + rw; ++xx) {
                if (xx < 0 || xx >= tubes::kScreenWidth) continue;
                px[static_cast<size_t>(yy) * tubes::kScreenWidth + xx] =
                    f.colour;
            }
        }
    }
}

// The cutscene's own art: two frame lists of .GFX, loaded by name. Ten files
// fill 26 slots and sixteen fill 17, so the same Image is shared - which is
// exactly what the original does with its pointer copies.
struct CutsceneArt {
    std::vector<tubes::Image> writeFrames;   // 26
    std::vector<tubes::Image> blowFrames;    // 17
    tubes::Image base;                       // WRITE0, Lanny's lower half
    bool ok = false;
};

CutsceneArt loadCutsceneArt(const tubes::Archive& res) {
    CutsceneArt art;
    auto fill = [&](const char* const* names, int n,
                    std::vector<tubes::Image>& out) {
        out.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            // OPAQUE. `1b2e:0f46` draws every animation frame through
            // `2000:389d`, which is `2321:068d` - the opaque member of the
            // blit family, the same one `1b2e:0e37` stamps the professor's
            // wave frames with. So a frame REPLACES its whole w x h box,
            // index-0 pixels included, and those land as colour 0 rather than
            // as "leave what was there". That is the whole of the 144 pixels
            // this screen was out by: the port had these masked, so the base
            // pose showed through the bottom of a 28 x 66 frame where the
            // original had blacked it out.
            if (!loadImage(res, names[i], out[static_cast<size_t>(i)], -1)) {
                return false;
            }
        }
        return true;
    };
    art.ok = fill(tubes::kWriteFrames, tubes::kWriteFrameCount,
                  art.writeFrames) &&
             fill(tubes::kBlowFrames, tubes::kBlowFrameCount, art.blowFrames) &&
             loadImage(res, "WRITE0.GFX", art.base, 0);
    return art;
}

// `1b2e:1651`. Returns the key that ended it - `1000:b224` stores it, and the
// attract arm at `1000:b28b` skips the demo when it is 2.
int runCutscene(const tubes::Archive& res, SDL_Renderer* ren, SDL_Texture* tex,
                tubes::Screen& screen, std::vector<uint8_t>& rgba,
                const tubes::Bytes& palRaw, const tubes::Palette& pal,
                int fadeSteps, SkipWatch& skip, tubes::MusicPlayer& music,
                bool musicOn, bool soundOn, const tubes::Image* board,
                bool haveBoard, const tubes::Sprite* atoms,
                const bool* haveAtom, const tubes::Font& small, bool haveSmall,
                int shotPage, int shotTick, const std::string& shotPath) {
    const CutsceneArt art = loadCutsceneArt(res);
    if (!art.ok || !haveBoard || !haveSmall) return 0;

    // The three sounds are this screen's own.
    // The voice MUST be silenced before the sounds below go out of scope:
    // the audio callback holds a bare pointer at whichever one is playing.
    struct Silence {
        tubes::MusicPlayer& m;
        ~Silence() { m.stopSound(); }
    } silence{music};
    tubes::Sound sounds[3];
    const char* const kNames[3] = {"WHATTHE.SFX", "NOOOO.SFX", "BUBBLE.SFX"};
    bool haveSound[3] = {};
    for (int i = 0; i < 3; ++i) {
        tubes::Bytes raw;
        std::string err;
        haveSound[i] = soundOn && res.read(kNames[i], raw, err) &&
                       tubes::decodeSfx(raw, sounds[i], err);
    }
    auto play = [&](const char* name) {
        if (!name) return;
        for (int i = 0; i < 3; ++i) {
            if (haveSound[i] && std::strcmp(name, kNames[i]) == 0) {
                music.playSound(&sounds[i]);
            }
        }
    };

    // A capture runs the pages at full speed: the frame SEQUENCE is what
    // has to be right, not the wall clock, and `--cutscene 4` would
    // otherwise sit through 36 seconds of the earlier pages first.
    const bool capturing = shotPage >= 0;

    // `[DS:0x1d6e]` and `[DS:0x1d6f]`, the two GLOBAL frame counters. They
    // live across pages, which is what lets page 4 start part way in.
    int frameA = 0, frameB = 0;
    // The beaker is drawn once before the fade and then left on the page, so
    // it stays put through the pages where track B is driving something else
    // - page 2, where B is the eighth element's atom. The port recomposes
    // every frame, so it has to remember the last frame B left it on. The
    // diff found this: page 2 was missing the atoms inside the beaker.
    int beakerFrame = 0;
    // `1b2e:1e6b`: the fourth page ends by copying both animation rectangles
    // from the OTHER video page (`2321:024d`, a page-to-page rect copy). What
    // that leaves is not symmetric, and the capture is what says so: on page
    // 5 the original still shows Lanny in full and the beaker is GONE. So the
    // copy restored a page that still had him and no longer had it.
    //
    // The page bookkeeping behind that is NOT fully traced - `[0x2376]` is
    // flipped once before each Animate rather than per frame, and which page
    // holds what by then depends on the whole run. The OUTCOME is read off
    // seven captures that all agree; the mechanism is marked as unread.

    // The original draws onto a PAGE, and the only thing that keeps that
    // distinguishable from "an overlay over the board" is that its blits are
    // OPAQUE: a frame writes colour 0 where its art is transparent, and black
    // is not the same as letting the blackboard through. The port kept the
    // figures in a layer stamped with index 0 meaning "not painted", which is
    // exactly the difference the fourth page's 144 pixels measured. They are
    // drawn straight onto the composed page here instead.
    //
    // The base pose is the exception and stays MASKED: `1b2e:1a91` draws it
    // through `2000:3921`, the masked thunk, because it goes over the board.
    // A page's own furniture: the bevelled panel, the story text and, on page
    // 2, the eight elements' atoms. Drawn once, over whatever is there.
    auto layout = [&](const tubes::CutscenePage& page) {
        for (int i = 0; i < page.count; ++i) {
            const tubes::CutsceneItem& it = page.items[i];
            switch (it.kind) {
            case tubes::CutsceneItem::kBar:
                drawPanel(screen, it.x, it.y, it.a, it.b);
                break;
            case tubes::CutsceneItem::kText:
                tubes::drawText(screen, small, it.x, it.y,
                                static_cast<uint8_t>(it.a),
                                static_cast<uint8_t>(it.b), it.text);
                break;
            case tubes::CutsceneItem::kAtom:
                if (it.a > 0 && it.a < kCellStates && haveAtom[it.a]) {
                    screen.draw(atoms[it.a], it.x, it.y);
                }
                break;
            }
        }
    };

    // One tick's worth of figures, drawn onto the page after its furniture.
    auto drawFigures = [&](const tubes::CutscenePage& page, bool aLive,
                           bool bLive) {
        screen.blit(art.base, tubes::kCutsceneBaseX, tubes::kCutsceneBaseY);
        if (!aLive && !bLive) {
            // Past the fourth page. `1b2e:1e58`..`1e84` copy both animation
            // boxes from the other page, and each track's frozen frame is
            // what is left - which an opaque blit of those two frames over
            // the base pose reproduces exactly, box and all.
            screen.blit(art.writeFrames[tubes::kWriteFrameCount - 1], 86, 122);
            screen.blit(art.blowFrames[tubes::kBlowFrameCount - 1], 258, 119);
            return;
        }
        if (!bLive) {
            // The beaker is still on the page from the last tick that drew
            // it, even while B is driving the eighth element's atom.
            screen.blit(art.blowFrames[static_cast<size_t>(
                            beakerFrame < tubes::kBlowFrameCount
                                ? beakerFrame : 0)],
                        258, 119);
        }
        if (aLive) {
            screen.blit(art.writeFrames[static_cast<size_t>(
                            frameA < tubes::kWriteFrameCount ? frameA : 0)],
                        page.a.x, page.a.y);
        }
        if (bLive) {
            if (page.b.w == 16) {
                screen.blit(art.blowFrames[static_cast<size_t>(
                                beakerFrame < tubes::kBlowFrameCount
                                    ? beakerFrame : 0)],
                            258, 119);
                if (frameB > 0 && frameB < kCellStates && haveAtom[frameB]) {
                    screen.draw(atoms[frameB], page.b.x, page.b.y);
                }
            } else {
                screen.blit(art.blowFrames[static_cast<size_t>(
                                frameB < tubes::kBlowFrameCount ? frameB : 0)],
                            page.b.x, page.b.y);
            }
        }
    };

    // The scene with no page on it: `1b2e:1a80`..`1ab6`, which is what the
    // fade-in reveals. THREE literal draws, and they are spelled out here
    // rather than routed through `drawFigures` because the first page has no
    // B track at all - its whole `CutsceneTrack` is zeroed, so asking
    // `drawFigures` for it put the beaker at `(page.b.x, page.b.y)` = (0, 0)
    // for the one frame before the fade. Reported from play as a glitch in the
    // top-left corner, and it was exactly that.
    auto scene = [&]() {
        screen.clear(0);
        screen.blit(*board, 0, tubes::kCutsceneBoardY);
        screen.blit(art.base, tubes::kCutsceneBaseX, tubes::kCutsceneBaseY);
        screen.blit(art.writeFrames[0], 86, 122);      // `1b2e:1a96`
        screen.blit(art.blowFrames[0], 258, 119);      // `1b2e:1aa9`
    };

    auto compose = [&](const tubes::CutscenePage& page, bool aLive,
                       bool bLive) {
        screen.clear(0);
        screen.blit(*board, 0, tubes::kCutsceneBoardY);
        // The page's furniture, THEN the animation over it. That is the
        // original's order - `1b2e:1cdf` draws the panel, the text and the
        // eight elements' atoms once, and the Animate call then paints on top
        // every tick - and it is the whole reason Flashium flashes: the
        // static draw beside its name is a type 4 ball, and track B cycles a
        // ball through colours 1..7 at the same coordinates. Stamping the
        // figures first put the static ball back on top and it never moved.
        layout(page);
        drawFigures(page, aLive, bLive);
    };

    if (musicOn && music.isOpen()) {
        tubes::Bytes song;
        std::string err;
        if (res.read(tubes::kCutsceneMusic, song, err)) music.play(song, err);
    }

    // `1b2e:1ad4`: the fade reveals the board and the two figures, with no
    // page text up yet - the first page's panel is drawn after it. The scene
    // has to be COMPOSED first: without this the fade brought up whatever the
    // Absolute Magic splash had left on the screen, which is what a player
    // sees as the splash's last frame corrupting.
    scene();
    if (runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, true, skip)) {
        const int k = skip.take();
        runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
        return k;
    }
    if (!capturing && holdRetraces(tubes::kCutsceneOpenDelay, skip)) {
        const int k = skip.take();
        runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
        return k;
    }

    int ended = 0;
    for (int p = 0; p < tubes::kCutscenePageCount && !ended; ++p) {
        const tubes::CutscenePage& page = tubes::kCutscenePages[p];
        // `1b2e:1b00`, `1cda`, `1d4e`, `1e08`: each page seeds the counters
        // it is about to drive. The generator does not carry these because
        // they are stores, not arguments - see cutscene.cpp's header.
        if (p == 0) frameA = 0;
        if (p == 1) frameB = 0;
        if (p == 2) frameB = 1;
        if (p == 3) { frameA = 10; frameB = 4; }

        // `[BP+0x32] * 7` iterations of `Delay(10)`, so `seconds` seconds.
        const int ticks = page.seconds * 7;
        bool aDone = page.a.count == 0, bDone = page.b.count == 0;
        for (int t = 0; t < ticks; ++t) {
            compose(page, !aDone, !bDone);
            presentScreen(ren, tex, screen, pal, rgba);
            if (shotPage == p && t == (shotTick >= 0 ? shotTick : ticks / 2)) {
                saveBmp(rgba, shotPath);
                return 2;
            }
            if (!capturing &&
                holdRetraces(tubes::kCutsceneFrameRetraces, skip)) {
                // `1b2e:112a` onward, transliterated: Enter or Space sets the
                // page's countdown to 1, so the PAGE ends and the next one
                // begins; ESC does that AND sets the return code to 2, which
                // is what leaves the cutscene. The port had every key ending
                // the whole thing, which made Enter a skip button rather than
                // the page-turner the original gives you.
                if (skip.take() == 2) ended = 2;
                break;
            }
            // Advance, then wrap - the original increments after drawing.
            if (!aDone) {
                if (page.a.soundFrame == frameA && page.a.sound) {
                    play(page.a.sound);
                }
                if (++frameA > page.a.count) {
                    // A one-shot track STOPS. The original simply stops
                    // drawing and its last frame stays on the page, so the
                    // port - which recomposes every tick - has to hold that
                    // frame rather than wrap to 1. Without this the explosion
                    // snapped back to an intact beaker at its own climax.
                    if (page.a.count == 25) {
                        frameA = page.a.count;
                        aDone = true;
                    } else {
                        frameA = 1;
                    }
                }
            }
            if (!bDone) {
                if (page.b.sound &&
                    (page.b.soundFrame == frameB ||
                     (page.b.soundFrame < 0 && !music.soundBusy()))) {
                    play(page.b.sound);
                }
                if (page.b.w == 60) beakerFrame = frameB;
                if (++frameB > page.b.count) {
                    if (page.b.count == 16) {
                        frameB = page.b.count;
                        bDone = true;
                    } else {
                        frameB = 1;
                    }
                }
            }
        }
        if (!ended) play(page.soundAfter);
        // `1b2e:1e6b`: the fourth page ends with the two box copies, and
        // then each track's frozen frame is what is left on the page.
        (void)0;
    }

    runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
    return ended;
}

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
    // `menuPage`, not `kMenuPages`: page 6 carries the port's extra row and
    // every layout call below already goes through the same accessor.
    const tubes::MenuPage& page = tubes::menuPage(p);

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

    const tubes::StarPlacement s =
        tubes::placeStars(p, menu.item(), menu.itemText(menu.item()));
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

// The one byte the game asks for, built from whatever the player has bound.
// This IS the port's input driver: everything above it is the original's, and
// `DEMO.SCR` replay feeds the same byte from a file instead.
//
// Keyboard and pad are OR-ed rather than switched between. The original bound
// one driver at a time because DOS gave it no choice; SDL has no such reason,
// and a player with a controller plugged in should not have to visit a menu.
// What a controller button means on a screen that is WAITING - a menu, a
// slideshow, the save or high score screen, a banner, the cutscene.
//
// The original's menu tests joystick button `0x20` directly (`1b2e:4d80`),
// because DOS gave it no abstraction over a gameport; SDL is that
// abstraction, so this maps the player's OWN bindings onto the keys those
// screens already handle rather than porting a driver's button numbers. Same
// reasoning as the rebinding screen - see input.h.
//
// Live play does NOT come through here. `readInput` polls the pad directly,
// the way the original polls its driver, so A and B stay the test tube's
// controls and never leak out as RETURN and ESCAPE.
SDL_Keycode menuKeyForPad(const tubes::Bindings& bind, int button) {
    static const SDL_Keycode kAs[tubes::kGameButtons] = {
        SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_RETURN, SDLK_ESCAPE,
    };
    for (int i = 0; i < tubes::kGameButtons; ++i) {
        if (bind[static_cast<tubes::GameButton>(i)].pad == button) {
            return kAs[i];
        }
    }
    // Start and Back are not game buttons and cannot be bound to one, but
    // every pad has them and a player will try them first.
    if (button == SDL_CONTROLLER_BUTTON_START) return SDLK_RETURN;
    if (button == SDL_CONTROLLER_BUTTON_BACK) return SDLK_ESCAPE;
    return SDLK_UNKNOWN;
}

uint8_t readInput(const tubes::Bindings& bind, SDL_GameController* pad) {
    static const uint8_t kBit[tubes::kGameButtons] = {
        tubes::button::kUp,   tubes::button::kDown,
        tubes::button::kLeft, tubes::button::kRight,
        tubes::button::kA,    tubes::button::kB,
    };
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    uint8_t b = 0;
    for (int i = 0; i < tubes::kGameButtons; ++i) {
        const tubes::Binding& x = bind.b[i];
        if (x.key != tubes::kUnbound && k[static_cast<SDL_Scancode>(x.key)]) {
            b |= kBit[i];
        }
        if (pad && x.pad != tubes::kUnbound &&
            SDL_GameControllerGetButton(
                pad, static_cast<SDL_GameControllerButton>(x.pad))) {
            b |= kBit[i];
        }
    }
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

    // Reads the file the same way `1b2e:000a` does and prints what
    // `tools/sav_decode.py` prints, so the two decoders can be diffed. The
    // .SAV is the player's, so this never writes.
    if (!opt.dumpSave.empty()) {
        std::ifstream sf(opt.dumpSave, std::ios::binary);
        if (!sf) {
            std::fprintf(stderr, "cannot open %s\n", opt.dumpSave.c_str());
            return 1;
        }
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(sf)),
                                  std::istreambuf_iterator<char>());
        tubes::SaveFile saves;
        if (!tubes::decodeSaves(raw, saves)) {
            std::fprintf(stderr, "%s is not %d bytes (%zu)\n",
                         opt.dumpSave.c_str(), tubes::kSaveFileBytes,
                         raw.size());
            return 1;
        }
        static const char* kBankName[2] = {"Endurance", "Wave"};
        for (int b = 0; b < 2; ++b) {
            std::printf("=== bank %d: %s ===\n", b, kBankName[b]);
            std::printf("    nonce at +0x%03x: %#04x\n", tubes::kSaveNonceOffset,
                        saves.bank[b].slots[tubes::kSaveNonceSlot].interval);
            for (int i = 0; i < tubes::kSaveSlotsShown; ++i) {
                const tubes::SaveSlot& s = saves.bank[b].slots[i];
                if (!s.live()) {
                    std::printf("  slot %d: %s\n", i + 1, tubes::kSaveAvailable);
                    continue;
                }
                std::printf("  slot %d: '%s'\n", i + 1, s.description.c_str());
                std::printf("           score %u  continues left %d  total "
                            "chains %d  wave %d  drops remaining %d  chains "
                            "this wave %d  velocity %d  interval %d  chain "
                            "target %d  atom target %d  colour target %d  "
                            "crystals %d  marked %d  pre-fill %d\n",
                            s.score, s.continuesLeft, s.totalChains, s.wave,
                            s.drops, s.chainsThisWave, s.velocity, s.interval,
                            s.chainTarget, s.atomTarget, s.colourTarget,
                            s.crystals, s.marked, s.preFill);
            }
        }
        // The round trip is the real check: re-encoding what was read has to
        // give the file back byte for byte, tail residue included.
        const std::vector<uint8_t> back = tubes::encodeSaves(saves);
        size_t diff = 0;
        for (size_t i = 0; i < raw.size(); ++i) diff += back[i] != raw[i];
        std::printf("re-encoded: %zu of %zu bytes differ\n", diff, raw.size());
        return diff == 0 ? 0 : 1;
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

    // The .SPR strip, printed frame by frame. The format's own oracle is that
    // `offset[i+1] - offset[i]` is exactly `width * height + 4` for all eleven
    // sub-images across the two files, so a wrong header would not fit.
    if (!opt.dumpSpr.empty()) {
        tubes::Bytes raw;
        std::vector<tubes::Image> strip;
        if (!res.read(opt.dumpSpr, raw, err) ||
            !tubes::decodeSpr(raw, strip, err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        std::printf("%s: %zu frames\n", opt.dumpSpr.c_str(), strip.size());
        for (size_t i = 0; i < strip.size(); ++i) {
            std::printf("  frame %zu  %d x %d  %zu pixels\n", i,
                        strip[i].width, strip[i].height,
                        strip[i].pixels.size());
        }
        return 0;
    }

    // Prints what `tools/anm_decode.py RUNS` prints - per frame, the number of
    // runs, the bytes they carry and an FNV-1a over (offset, bytes). Two
    // interpreters of the same compiled x86 have to agree run for run, not
    // merely produce a picture that looks right: a wrong SI would still paint
    // something plausible, and the .CSP geometry bug this project already had
    // is exactly that failure mode.
    if (!opt.dumpAnm.empty()) {
        tubes::Bytes raw;
        std::vector<tubes::AnimFrame> anim;
        if (!res.read(opt.dumpAnm, raw, err) ||
            !tubes::decodeAnm(raw, anim, err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        for (size_t i = 0; i < anim.size(); ++i) {
            size_t bytes = 0;
            uint32_t h = 0x811c9dc5u;
            auto mix = [&h](uint8_t b) { h = (h ^ b) * 0x01000193u; };
            for (const tubes::AnimFrame::Run& r : anim[i].runs) {
                bytes += r.pixels.size();
                mix(static_cast<uint8_t>(r.offset));
                mix(static_cast<uint8_t>(r.offset >> 8));
                mix(static_cast<uint8_t>(r.pixels.size()));
                mix(static_cast<uint8_t>(r.pixels.size() >> 8));
                for (uint8_t b : r.pixels) mix(b);
            }
            std::printf("frame %2zu  %4zu runs  %6zu bytes  %08x\n", i,
                        anim[i].runs.size(), bytes, h);
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
    // The port's OWN settings - the toggles and the six bindings. Not in the
    // game directory: `SETUP.CFG` is the DOS install's hardware configuration
    // and belongs to `SETUP.EXE`, and the port does not read a byte of it.
    // `SDL_GetPrefPath` puts this where the platform keeps such things, which
    // also means the eventual non-desktop ports have somewhere to go.
    std::string settingsPath;
    {
        char* pref = SDL_GetPrefPath("", "tubes-port");
        if (pref) {
            settingsPath = std::string(pref) + "settings.cfg";
            SDL_free(pref);
        }
    }
    tubes::Settings settings;
    if (!settingsPath.empty()) {
        std::ifstream cf(settingsPath);
        if (cf) {
            std::stringstream ss;
            ss << cf.rdbuf();
            tubes::decodeSettings(ss.str(), settings);
        }
    }
    auto writeSettings = [&]() {
        if (harness || settingsPath.empty()) return;
        std::ofstream cf(settingsPath);
        if (cf) cf << tubes::encodeSettings(settings);
    };

    // One controller, the first one plugged in. Opened below, once SDL is
    // actually up - this used to enumerate here, which is BEFORE `SDL_Init`,
    // so `SDL_NumJoysticks` was asked on an uninitialised library and always
    // said zero. No pad was ever opened at startup and the only way to get
    // one was to unplug it and plug it back in. Reported from play.
    SDL_GameController* gamepad = nullptr;

    // `1b2e:000a`: read `TUBES.SAV` if it is there. Unlike the high score
    // table the game SHIPS one, zero-filled, and the reader zero-fills the
    // banks before reading anyway - so a missing or malformed file is simply
    // five empty slots per bank rather than an error.
    const std::string savePath = opt.gameDir + "/TUBES.SAV";
    tubes::SaveFile saves;
    {
        std::ifstream sf(savePath, std::ios::binary);
        if (sf) {
            std::vector<uint8_t> raw((std::istreambuf_iterator<char>(sf)),
                                      std::istreambuf_iterator<char>());
            if (!tubes::decodeSaves(raw, saves)) {
                std::fprintf(stderr,
                             "TUBES.SAV is malformed (%zu bytes); starting "
                             "with no saved games\n", raw.size());
                saves = tubes::SaveFile{};
            }
        }
    }

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

    // `1b2e:00ac`. Same harness guard as the high score table, and for the
    // same reason: this is the player's own game directory.
    auto writeSaves = [&]() {
        if (harness) return;
        const std::vector<uint8_t> raw = tubes::encodeSaves(saves);
        std::ofstream sf(savePath, std::ios::binary);
        if (sf) sf.write(reinterpret_cast<const char*>(raw.data()),
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
    // The five mouths, MASKED. `1b2e:0cd1` draws them through `2000:3921`,
    // which is the same thunk `1b2e:0510` uses for `POINTER0` and not the
    // `2000:389d` the wave frames are stamped with - so index 0 is
    // transparent. Loading them opaque left three black columns beside his
    // chin, because the 12 x 8 the call passes is wider than the mouth in the
    // art. Two call sites agree on which thunk is which, and a capture showed
    // it besides.
    tubes::Image talkFrame[5];
    bool haveTalk[5] = {false, false, false, false, false};
    for (int i = 0; i < 5; ++i) {
        haveTalk[i] = loadImage(res, tubes::kTalkNames[i], talkFrame[i], 0);
    }
    // `1b2e:084e`'s joke slide. Both go down through `2321:068d`, the OPAQUE
    // blit - `FLASH.GFX` because it is a whole 172 x 132 transparency and
    // covers the slide exactly, `POINTERT.GFX` because its 28 x 21 carries a
    // patch of blackboard green behind the head it replaces.
    tubes::Image flashArt, pointerTArt;
    const bool haveFlash = loadImage(res, "FLASH.GFX", flashArt, -1);
    const bool havePointerT = loadImage(res, "POINTERT.GFX", pointerTArt, -1);
    // The ending's hop, masked - `1b2e:0b8f` draws through `2000:3921`.
    tubes::Image jumpFrameArt[tubes::kJumpFrames];
    bool haveJump[tubes::kJumpFrames] = {false, false, false};
    for (int i = 0; i < tubes::kJumpFrames; ++i) {
        haveJump[i] = loadImage(res, tubes::kJumpNames[i], jumpFrameArt[i], 0);
    }
    tubes::Image prizeArt;
    const bool havePrize = loadImage(res, tubes::kPrizeArt, prizeArt, 0);

    SceneArt sceneArt;
    sceneArt.corners = slideCorner;
    sceneArt.haveCorner = haveCorner;
    sceneArt.pointer = pointerFrame;
    sceneArt.havePointer = havePointer;
    sceneArt.books = &booksArt;
    sceneArt.haveBooks = haveBooks;
    sceneArt.bar = &slideBar;
    sceneArt.haveBar = haveBar;
    sceneArt.talk = talkFrame;
    sceneArt.haveTalk = haveTalk;
    sceneArt.flash = &flashArt;
    sceneArt.haveFlash = haveFlash;
    sceneArt.pointerT = &pointerTArt;
    sceneArt.havePointerT = havePointerT;
    sceneArt.jump = jumpFrameArt;
    sceneArt.haveJump = haveJump;
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
    //
    // Loaded whether or not `--play-demo` asked for it, because View Demo and
    // the attract timeout both need it - `1000:5f4b` reads it inside the
    // session, and its two error strings ("Demo ResourceError", "Installing
    // Demo Error") say the original treats a missing one as fatal there. Here
    // a failure only costs the demo.
    tubes::Demo demo;
    bool haveDemo = false;
    {
        tubes::Bytes raw;
        std::string demoErr;
        haveDemo = res.read("DEMO.SCR", raw, demoErr) &&
                   tubes::decodeScr(raw, demo, demoErr);
        if (!haveDemo && opt.playDemo) {
            std::fprintf(stderr, "error: %s\n", demoErr.c_str());
            return 1;
        }
        if (opt.playDemo) {
            std::printf("DEMO.SCR: seed 0x%08x, %zu frames\n", demo.seed,
                        demo.input.size());
        }
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
    // `--make-save`, a test rig rather than a feature. The progression is
    // what makes this worth doing in the engine instead of by hand: a record
    // carries `WaveProgress`, so a save with wave 75 and wave-1 counters is
    // NOT a wave 75 - it is the warp the reversing notes warn about. Stepping
    // `advanceWave` is the only way to get the real numbers.
    //
    // It writes to the path it is given and nowhere else. `TUBES.SAV` is the
    // player's file and the game directory is not ours to write to.
    if (opt.wave > 0 && !opt.makeSave.empty()) {
        while (game->progress().wave < opt.wave) game->advanceWave();
        tubes::SaveFile out;
        tubes::SessionTotals t;
        t.continuesLeft = 3;
        tubes::SaveSlot& slot = out[tubes::SaveBank::kWave].slots[0];
        game->saveInto(slot, t);
        slot.setDescription("WAVE " + std::to_string(opt.wave) + " TEST");
        const std::vector<uint8_t> raw = tubes::encodeSaves(out);
        std::ofstream f(opt.makeSave, std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "cannot write %s\n", opt.makeSave.c_str());
            return 1;
        }
        f.write(reinterpret_cast<const char*>(raw.data()),
                static_cast<std::streamsize>(raw.size()));
        std::printf("wrote %s: Wave slot 1 = wave %d, %zu bytes\n",
                    opt.makeSave.c_str(), game->progress().wave, raw.size());
        return 0;
    }

    if (opt.wave > 0) {
        while (game->progress().wave < opt.wave) game->advanceWave();
        game->startWave();
        std::printf("Wave %d: mode %d, %d to go\n", game->progress().wave,
                    static_cast<int>(game->waveMode()), game->objective().counter);
    }
    if (!opt.renderState.empty() && !loadState(opt.renderState, *game)) return 1;

    // GAMECONTROLLER is not required: SDL_Init fails only on VIDEO, and a
    // machine with no controller support still plays on the keyboard.
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            gamepad = SDL_GameControllerOpen(i);
            if (gamepad) {
                std::printf("gamepad: %s\n",
                            SDL_GameControllerName(gamepad));
                break;
            }
        }
    }

    // `--scale` sizes the WINDOW for this run and is deliberately not written
    // into `settings.graphics`: a capture script must be able to pin a size
    // without changing what the player chose in the game, and the graphics
    // screen saves on every keystroke, so a flag that landed in the struct
    // would become permanent the moment the player toggled anything. Leaving
    // the setting at Fit inside a window sized to N costs nothing - Fit picks
    // the largest whole multiple that fits, which is N.
    int scale = opt.scale > 0 ? opt.scale : settings.graphics.scale;
    if (scale <= 0) {
        SDL_DisplayMode dm;
        scale = 3;
        if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
            int fit = std::min(dm.w / tubes::kScreenWidth,
                               dm.h / tubes::displayUnitHeight(settings.graphics));
            scale = std::max(1, std::min(fit - 1,
                                         tubes::GraphicsOptions::kMaxScale));
        }
    }

    SDL_Window* win = SDL_CreateWindow(
        "Tubes", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        tubes::kScreenWidth * scale,
        tubes::displayUnitHeight(settings.graphics) * scale,
        SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren =
        win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : nullptr;
    if (win && !ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!win || !ren) {
        std::fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Nearest neighbour, always: this is an indexed 320x200 image and a
    // filtered upscale of one is a blur, not a picture. The 4:3 option
    // stretches the destination rectangle instead - see `presentRect`.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    // Fullscreen, vsync and the rest of the saved display options, now that
    // there is a window and a renderer to put them on.
    applyDisplayOptions(win, ren, settings.graphics);
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
    // `SLIDE.SFX`, `DS:0x2124` - the projector advancing. `1b2e:084e` plays it
    // last, as the joke slide is moved off.
    tubes::Sound slideSound;
    bool haveSlideSound = false;
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
            {
                tubes::Bytes raw;
                std::string err;
                haveSlideSound = res.read("SLIDE.SFX", raw, err) &&
                                 tubes::decodeSfx(raw, slideSound, err);
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
    // The device is opened here but NOTHING is played yet. `TUBES.MUS` is the
    // TITLE screen's song - `1b2e:5238` loads it as one of that stage's own
    // four resources - and starting it at boot put it over the Software
    // Creations splash, which `21d5:007b` runs in silence: it loads three
    // resources and not one of them is a song. The boot sequence's music is
    // AMTHEME.MUS in the second splash and CLASS.MUS in the cutscene, each
    // started by the screen that owns it.
    bool haveMusicDevice = false;
    if (!opt.music.empty() && opt.screenshot.empty()) {
        std::string musicErr;
        if (!haveDrivers) {
            std::fprintf(stderr, "music disabled: %s\n", driverErr.c_str());
        } else if (!music.open(drivers, musicErr)) {
            std::fprintf(stderr, "music disabled: %s\n", musicErr.c_str());
        } else {
            haveMusicDevice = true;
        }
    }

    tubes::Screen screen;
    std::vector<uint8_t> rgba;
    // Started once the boot sequence is over, since that is where the screen
    // that owns this song begins.
    auto startBootMusic = [&]() {
        if (!haveMusicDevice || !settings.music) return;
        std::string musicErr;
        tubes::Bytes song;
        if (!res.read(opt.music, song, musicErr) ||
            !music.play(song, musicErr)) {
            std::fprintf(stderr, "music disabled: %s\n", musicErr.c_str());
            return;
        }
        std::printf("playing %s through Nuked-OPL3\n", opt.music.c_str());
    };

    // `1b2e:11b0`, the boot sequence. It runs before anything else the program
    // shows, and a skip in the first splash cancels the second - which is the
    // original's own behaviour, not a concession: `if k <> 1 and k <> 2`.
    //
    // Skipped under `harness` with every other timed screen, so no capture
    // waits three seconds to reach the frame it wants.
    if ((!harness || opt.splashFrame >= 0 || opt.splash2Step >= 0 ||
         opt.cutscenePage >= 0) && !opt.noSplash) {
        SkipWatch skip;
        const bool capturing = opt.splashFrame >= 0 || opt.splash2Step >= 0 ||
                               opt.cutscenePage >= 0;
        int k = 0;
        if (opt.splash2Step < 0) {
            k = runSoftwareCreationsSplash(
                res, ren, tex, screen, rgba,
                capturing ? 0 : opt.fadeSteps, skip, opt.splashFrame,
                opt.screenshot);
        }
        // `1b2e:11b0`: the second splash runs only if the first was not
        // skipped. One press gets past both, which is the original's design.
        if ((k != 1 && k != 2 && !capturing) || opt.splash2Step >= 0) {
            // `musicOn` / `soundOn` proper are declared with the frame loop;
            // the splash predates them, so it reads the same two settings.
            k = runAbsoluteMagicSplash(res, ren, tex, screen, rgba,
                                       capturing ? 0 : opt.fadeSteps, skip,
                                       music,
                                       !opt.music.empty() && settings.music,
                                       settings.sound, opt.splash2Step,
                                       opt.screenshot);
        }

        // `1000:b224`: the cutscene runs HERE - once, after the splashes and
        // immediately before the title screen is first shown. The main loop's
        // own `JMP 1000:b236` goes back to the title call and not to this, so
        // it is a boot-time screen and not part of the cycle.
        //
        // **It does not depend on the splashes.** `1b2e:11b0` is a `void`
        // procedure: it consumes each splash's return code to decide whether
        // to run the SECOND splash and then throws it away, so `1000:b224` has
        // nothing to test. Both this call and the splash call at `1000:ac21`
        // are gated on one thing and it is the same thing - `2000:70fa`, which
        // reads the command tail's length out of the PSP at `ES:[0x80]` and is
        // `ParamCount`. Starting the game with ANY argument skips both.
        //
        // The port gated the cutscene on the first splash's key, so Enter on a
        // splash dropped the player straight to the menu. Reported from play.
        //
        // ONE DEPARTURE, and it is the player's, agreed before it was written:
        // **ESC on a splash skips the cutscene as well.** The original runs the
        // cutscene whichever key ended the splash, and there is no way to say
        // "I have seen the intro" without also sitting through it. ESC already
        // means "leave this whole thing" inside the cutscene (`1b2e:112a`
        // returns 2 for it) and Enter already means "next", so this only
        // extends the two keys the screen after it already uses. Enter and
        // Space are unchanged and faithful: they get past the splashes and
        // into the cutscene.
        const bool escaped = (k == 2);
        if ((!escaped && !capturing) || opt.cutscenePage >= 0) {
            runCutscene(res, ren, tex, screen, rgba, palRaw, pal,
                        capturing ? 0 : opt.fadeSteps, skip, music,
                        !opt.music.empty() && settings.music, settings.sound,
                        haveBlackboard ? &blackboard : nullptr, haveBlackboard,
                        atoms, haveAtom, smallFont, haveSmall,
                        opt.cutscenePage, opt.cutsceneTick, opt.screenshot);
        }

        // The capture flag is an exit, like every other one: without this the
        // frame loop runs on and overwrites the BMP with the game.
        if (capturing && !opt.screenshot.empty()) {
            music.stop();
            SDL_DestroyTexture(tex);
            SDL_DestroyRenderer(ren);
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 0;
        }
    }

    // `1b2e:5238`: the title stage loads TUBES.MUS for itself, so the song
    // starts when the boot sequence is over and not before it.
    startBootMusic();

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
    // `1b2e:5427` onward: the slot pages are built from the file every time the
    // title screen is entered, which is why a game saved this session shows up
    // without a restart.
    auto refreshSaveSlots = [&]() {
        for (int mode = 1; mode <= 2; ++mode) {
            const tubes::SaveBank b = mode == 1 ? tubes::SaveBank::kEndurance
                                                : tubes::SaveBank::kWave;
            for (int slot = 1; slot <= tubes::kSaveSlotsShown; ++slot) {
                const tubes::SaveSlot& rec = saves[b].slots[slot - 1];
                menu.setSaveSlotLive(mode, slot, rec.live());
                menu.setSaveSlotText(mode, slot, tubes::saveSlotLabel(b, rec));
            }
        }
    };
    refreshSaveSlots();
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
    // The palette actually shown. It is `pal` except while a fade is running,
    // when it is `pal` scaled by the step counter - the original's `DS:0x2702`
    // scratch, which is what `23e7:003d` uploads.
    tubes::Palette shownPal = pal;

    auto blitAndPresent = [&]() {
        presentScreen(ren, tex, screen, shownPal, rgba);
    };

    // `23e7:0097` and `23e7:00ce`, transliterated: 41 palette uploads, each
    // one vertical retrace after the last. The screen contents do not change
    // during a fade - the original is blocking here too, reading no input and
    // running no game logic, which is why the whole thing is a plain loop
    // rather than a state in the frame loop.
    //
    // Skipped wholesale under `harness`: every capture and every pixel diff
    // presents the first composed frame, and 41 dimmed copies of it in front
    // would change which frame `--screenshot` writes.
    const int fadeSteps = harness ? 0 : opt.fadeSteps;
    auto runFade = [&](bool in) {
        if (fadeSteps > 0) {
            for (int i = 0; i <= fadeSteps; ++i) {
                const int step = in ? i : fadeSteps - i;
                shownPal = tubes::fadePalette(palRaw, step, fadeSteps);
                blitAndPresent();
                SDL_Delay(14);              // one 70 Hz retrace
            }
        }
        // Fading out leaves the DAC black, so the next screen is drawn while
        // nothing is visible and only its own fade-in reveals it.
        shownPal = in ? pal : tubes::fadePalette(palRaw, 0, tubes::kFadeSteps);
    };
    // `TUBESEND.BIN`, the shareware sign-off. Loaded once - it is 3,680 bytes,
    // and 23 rows of an 80 x 25 screen. Present in the REGISTERED archive too,
    // because `TUBES.RES` is byte-identical between the editions, but that
    // build never names it and the port does not show it there either.
    tubes::TextScreen exitBanner;
    {
        tubes::Bytes blob;
        std::string berr;
        if (res.read("TUBESEND.BIN", blob, berr)) exitBanner.loadBin(blob);
    }

    // `--exit-screen` opens it directly and leaves, so a capture never has to
    // walk a whole session to reach the one screen that ends one.
    if (opt.exitScreen) {
        if (exitBanner.rowsLoaded() == 0) {
            std::fprintf(stderr, "TUBESEND.BIN not in this archive\n");
            return 1;
        }
        runExitScreen(win, ren, exitBanner, fadeSteps, opt.screenshot);
        return 0;
    }

    // Set at a screen change; the next composed frame is revealed rather than
    // cut to. `presentFrame` consumes it, so every one of the loop's present
    // sites gets this without repeating the call.
    bool pendingFadeIn = false;

    // One screen change, both halves. Every screen function in the original
    // ends with `CALLF [0x22de]` and then the palette fade-out - the title at
    // `1b2e:6172`, Instructions at `1b2e:3ef1`, Credits at `1b2e:4600`, the
    // high score viewer at `1b2e:6498`, the cutscene at `1b2e:1f46`, the
    // session at `1000:5efb` - and every screen reveals itself with the
    // fade-in once it has drawn. `[0x22de]` is the music half: it appears
    // exactly once in each of those six functions, always immediately before
    // the palette fade, and it is NOT `StopMusic` (`[0x22da]`, which is used
    // in other places). Its driver entry is unread, so the port fades the
    // picture and leaves the music alone rather than guessing.
    auto changeScreen = [&]() {
        runFade(false);
        pendingFadeIn = true;
    };

    auto presentFrame = [&]() {
        blitAndPresent();
        if (pendingFadeIn) {
            pendingFadeIn = false;
            runFade(true);
        }

        if (!opt.screenshot.empty() && shotCountdown-- <= 0) {
            saveBmp(rgba, opt.screenshot);
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

    // `1b2e:0510` builds the classroom from nothing, and the projector screen
    // ROLLS DOWN while it does. Which screens take that path is not a guess:
    // `1000:86b8` reads `if (wave = 1) and not replay then 1b2e:0510 else
    // 1b2e:0656`, so a briefing rolls the screen only at the top of a session
    // and never after a Continue; `1b2e:2d63` and `1b2e:411b` call it
    // unconditionally, so the Instructions and the Credits roll every time.
    tubes::ScreenRoll screenRoll;

    // The professor's idle. Every screen that waits runs TWO waits in order -
    // `1b2e:0cd1(bursts)` while he talks, and then, only if that timed out,
    // `1b2e:0e37(seconds)` while he waves - so `ProfessorIdle` owns both
    // phases and reports a mouth frame or a pointer frame, never both.
    //
    // The original draws these from the program-global `RandSeed`; the port's
    // generator is a `Game` member and the Instructions screen has no Game, so
    // the scene gets a stream of its own off the boot seed. See the note above
    // `PascalRandom` for what that does and does not change.
    tubes::ProfessorIdle profIdle;
    tubes::PascalRandom sceneRng{bootSeed ? bootSeed : 1u};
    // `1000:9499`. 0 is off, 1 the text and 2 the prize; `1b2e:0b8f` holds
    // each for thirty seconds or a key. `DS:0x20fc` is the hop frame, 1..3 on
    // the same ten-retrace clock the wave uses.
    int endingPage = opt.ending > 0 ? opt.ending : 0;
    float endingTimer = tubes::kEndingHoldSeconds;
    int jumpFrame = 1;
    float jumpAccum = 0.0f;

    // `1b2e:084e`, rolled from every `1b2e:0a11` and good for at most one
    // showing per run. `raiseScene` is where the port calls it, because that
    // is every place the original reaches `1b2e:0a11` from.
    tubes::JokeSlide joke;

    auto raiseBriefing = [&](bool replay) {
        briefingUp = true;
        briefDecor = static_cast<int8_t>(game->rollForTest(8) + 1);
        rollBackdrop();
        if (game->progress().wave == 1 && !replay) screenRoll.restart();
        // `1000:86b8`: `k := 1b2e:0cd1($17)`, then `1b2e:0e37($1e)`.
        profIdle.restart(tubes::kTalkBurstsBriefing, sceneRng);
        joke.maybeStart(sceneRng);
    };
    if (briefingUp) raiseBriefing(false);

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
    // F3 and F4, `DS:0x215f` and `DS:0x215e`. The Game Options page edits the
    // same two, which is why they are seeded from the settings file - and why
    // toggling either in game persists it, exactly as the original's page did
    // via SETUP.CFG.
    bool musicOn = !opt.music.empty() && settings.music;
    bool soundOn = settings.sound;
    auto persistAudio = [&]() {
        settings.music = musicOn;
        settings.sound = soundOn;
        writeSettings();
    };

    // Page 6's two toggle rows carry their state. The captured page reads
    // "Toggle Music <yes/no>", so the row is built at runtime the same way a
    // save slot's is.
    auto refreshOptionRows = [&]() {
        menu.setOptionText(1, std::string("Toggle Music  ") +
                                  (musicOn ? "Yes" : "No"));
        menu.setOptionText(2, std::string("Toggle Sound FX  ") +
                                  (soundOn ? "Yes" : "No"));
    };
    refreshOptionRows();

    // The rebinding screen. Port-only, so it is a flag beside the others
    // rather than a `Menu::Page` - the page machine is the original's and
    // there is no page 8 in it.
    bool rebindOpen = opt.rebind;

    // The Instructions slideshow, `1b2e:2d63` - a straight run of 21 slides
    // rather than a dispatch, so the state is just which one is up.
    bool instrOpen = opt.instr >= 0 || opt.credits;
    // `--instructions` / `--credits` open the screen the way the menu does,
    // roll-down and all, so a capture of the animation needs no other flag.
    if (instrOpen) {
        screenRoll.restart();
        profIdle.restart(tubes::kTalkBurstsSlide, sceneRng);
        if (opt.joke) joke.phase = 1;   // harness: show it without the roll
        else joke.maybeStart(sceneRng);
    }
    int instrSlide = opt.instr > 0 ? opt.instr : 0;
    // The Credits, `1b2e:411b`: the same screen with a different table.
    bool instrCredits = opt.credits;
    int rebindRow = 0;                 // 0..5, the control being pointed at
    bool rebindWaiting = false;        // armed, waiting for the press
    bool graphicsOpen = opt.graphics;  // the port's display options
    int graphicsRow = 0;               // 0..kGraphicsRows-1

    // `1b2e:0a11`'s slide drop, gated on `DS:0x210e` - it runs the FIRST time
    // the scene is shown and never again, so this is a program-lifetime flag
    // and not a per-briefing one.
    bool slideDropped = false;
    int slideFrame = 0;          // index into kSlideDrop while dropping
    float slideAccum = 0.0f;
    auto slidePos = [&]() {
        if (slideDropped || slideFrame >= tubes::kSlideDropFrames) {
            return tubes::SlideFrame{tubes::kSlideX, tubes::kSlideY};
        }
        return tubes::kSlideDrop[slideFrame];
    };
    // Everything about the classroom that moves, gathered once a frame. The
    // briefing is the only screen that also wobbles the slide, so the other
    // callers take the pose with the slide left at rest.
    auto scenePose = [&](bool wobble) {
        ScenePose p;
        p.frameH = screenRoll.height();
        p.profFrame = tubes::pointerFrameFor(profIdle.wave);
        // `1b2e:084e` is a blocking routine called from inside `1b2e:0a11`,
        // which itself runs BEFORE the key wait - so while the gag is up the
        // professor is not talking, and no mouth is stamped over the face it
        // replaces.
        p.mouthFrame = joke.active() ? 0 : profIdle.mouthFrame();
        p.jokeSlide = joke.showFlash();
        p.jokeFace = joke.showFace();
        if (wobble) {
            const tubes::SlideFrame s = slidePos();
            p.slideX = s.x;
            p.slideY = s.y;
        }
        return p;
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
    bool hsActive = opt.hsEntry;
    std::string hsName;
    int hsRow = opt.hsEntry ? 3 : 0;   // 1-based, as the original's display
                                       // loop is
    int hsCursor = tubes::kHsCursorMin;
    int hsCursorDir = 1;
    float hsCursorAccum = 0.0f;
    // Counts out the applause between finishing the name and committing it.
    float hsHold = 0.0f;

    // The F2 save screen, `1000:2dd0`'s save arm. It blocks the frame loop the
    // same way Pause does - the original calls it from inside the loop body
    // and does not come back until it is done.
    bool saveScreen = opt.f2;
    int saveSlotSel = 1;              // `DS:0x1d4d`
    bool saveTyping = false;
    std::string saveDesc;
    float saveWritten = 0.0f;         // `1000:3722`'s 20-retrace hold
    tubes::HiScoreBank hsBank = tubes::HiScoreBank::kWave;

    // The standalone viewer the menu opens, `1b2e:61b6` - now decompiled, so
    // the layout is the original's rather than borrowed. Page 0 is Endurance
    // and page 1 is Wave; the original pre-renders both onto the two video
    // pages and flips between them, and redrawing gives the same picture.
    // `1000:b268`: View Demo and the attract timeout run the SAME thing - a
    // session in mode 0 at difficulty 2, replaying DEMO.SCR. This flag is what
    // tells the frame loop to feed it the recording instead of the keyboard.
    bool attractDemo = false;

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
            changeScreen();
            return;
        }
        changeScreen();
        stage = Stage::kTitle;
        // `1000:b2ac`: the ATTRACT arm clears `DS:0x1d42` on the way back, so
        // the demo returns to a bare title screen with the menu down. Every
        // other route leaves the menu up, which is what `1b2e:52bf`'s own
        // flag does when it is not cleared.
        if (gameMode == 0) attractDemo = false;
        else menu.raise();
        playSong("TUBES.MUS");
    };

    // `1b2e:0b8f` returning ends a page; the SECOND one ends the ending, and
    // `1000:9676` clears `DS:0x20e3` on the way out so the professor stops
    // hopping. The session is already over - the ending set `gameOver` when it
    // began - so this goes straight to the finish.
    auto endEndingPage = [&]() {
        if (endingPage == 1) {
            endingPage = 2;
            endingTimer = tubes::kEndingHoldSeconds;
            return;
        }
        endingPage = 0;
        sstage = tubes::SessionStage::kFinished;
        endSession();
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
        // Only an arm that plays something can end on `[DS:0x22ce]`.
        bannerWaitsForMusic = *bt.music != 0;
    };

    // `1000:5e23`/`5e90`: the wait ends, the music is stopped, and
    // `1000:5ef0`'s second Delay runs before anything else is drawn.
    auto leaveBannerWait = [&]() {
        music.stop();                                  // [DS:0x22da]
        bannerPhase = tubes::BannerPhase::kOutro;
        bannerTimer = tubes::kBannerHoldSeconds;
    };

    // Where the recording is up to. Declared here because `startDemo` below
    // rewinds them and the frame loop consumes them.
    float demoAccum = 0.0f;
    size_t demoFrame = 0;

    // `1000:b268`, and `1000:b29a` is the same three stores again for the
    // timeout. Mode 0, a new game, difficulty 2 - and the session then loads
    // DEMO.SCR itself at `1000:5f4b`. The difficulty is not decoration: it
    // sets the dispense interval the recording was made against, and playing
    // the demo at 101 desynchronises it within a few spawns.
    auto startDemo = [&]() {
        if (!haveDemo) return false;
        newSession(kDemoDifficulty, demo.seed);   // DS:0x1d4f := 2
        gameMode = 0;                             // DS:0x1d4e := 0
        flags = tubes::SessionFlags{};
        totals = tubes::SessionTotals{};
        banner = tubes::Banner::kNone;
        paused = false;
        briefingUp = false;
        sstage = tubes::SessionStage::kPlay;
        demoFrame = 0;
        demoAccum = 0.0f;
        attractDemo = true;
        stage = Stage::kPlay;
        playSong(tubes::playMusicFor(game->dropsRemaining()));
        return true;
    };

    // Entering the stats screen is what accumulates the running chain total,
    // so it happens exactly once per visit - never in the draw path. It is
    // also what RESETS the per-wave count, in the game as well as on the
    // screen: `-0x17c` is one byte in the original and `enterStatsScreen`
    // holds both of the port's copies of it together.
    auto enterStats = [&]() {
        int liveChains = game->chains();
        statsRows = tubes::enterStatsScreen(totals, liveChains,
                                            game->progress().wave,
                                            game->score(), false);
        game->setChains(liveChains);
        playSong(tubes::kStatsMusic);
        // `1000:8da5`: `k := 1b2e:0cd1(10)`, then `1b2e:0e37($1e)`.
        profIdle.restart(tubes::kTalkBurstsStats, sceneRng);
        joke.maybeStart(sceneRng);
    };
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
            // Hot-plug: take the first controller that appears and let go of
            // it when it leaves. A player should be able to plug one in mid
            // game.
            if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                if (!gamepad) gamepad = SDL_GameControllerOpen(ev.cdevice.which);
                continue;
            }
            if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
                if (gamepad &&
                    ev.cdevice.which ==
                        SDL_JoystickInstanceID(
                            SDL_GameControllerGetJoystick(gamepad))) {
                    SDL_GameControllerClose(gamepad);
                    gamepad = nullptr;
                }
                continue;
            }
            // A controller press binds too, which is the whole point of the
            // screen accepting either.
            if (ev.type == SDL_CONTROLLERBUTTONDOWN && rebindOpen &&
                rebindWaiting) {
                settings.bindings.bindPad(
                    static_cast<tubes::GameButton>(rebindRow),
                    ev.cbutton.button);
                rebindWaiting = false;
                writeSettings();
                continue;
            }
            // Live play polls the pad instead - see `menuKeyForPad`.
            const bool livePlay = stage == Stage::kPlay &&
                                  sstage == tubes::SessionStage::kPlay &&
                                  !saveScreen && !hsActive && !paused;
            SDL_Keycode k = SDLK_UNKNOWN;
            if (ev.type == SDL_KEYDOWN) {
                k = ev.key.keysym.sym;
            } else if (ev.type == SDL_CONTROLLERBUTTONDOWN && !livePlay) {
                k = menuKeyForPad(settings.bindings, ev.cbutton.button);
            }
            if (k == SDLK_UNKNOWN) continue;

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

            // `1000:9499`'s two pages, each a `1b2e:0b8f(0x1e)` wait. The
            // jump wait returns the same codes `1b2e:0e37` does, and the
            // ending acts on none of them - it just stops waiting - so any of
            // the three keys turns the page.
            if (endingPage > 0) {
                if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_ESCAPE) {
                    endEndingPage();
                }
                continue;
            }

            // `1b2e:2f27`: ESC leaves, Up goes back a slide - clamped at the
            // first, whose own arm jumps to its own wait - and anything else
            // goes forward. Running off the end leaves too.
            if (instrOpen) {
                if (k == SDLK_ESCAPE) {
                    instrOpen = false;
                } else if (k == SDLK_UP) {
                    if (instrSlide > 0) --instrSlide;
                } else if (++instrSlide >= (instrCredits
                                               ? tubes::kCreditPageCount
                                               : tubes::kInstructionSlideCount)) {
                    instrOpen = false;
                }
                // Each slide is its own `1b2e:0cd1(35)` / `1b2e:0e37(30)`
                // pair, so turning the page starts him talking again.
                if (instrOpen) {
                    profIdle.restart(tubes::kTalkBurstsSlide, sceneRng);
                    joke.maybeStart(sceneRng);
                }
                // Only LEAVING fades. Moving between slides does not - the
                // original changes the slide inside one screen function and
                // its fade-out is at the very end, on the way back.
                if (!instrOpen) changeScreen();
                continue;
            }

            // The rebinding screen owns the keyboard while it is up. When it
            // is ARMED the next press is the binding, ESC included - there is
            // no other way to bind Escape, and no reason to forbid it.
            if (rebindOpen) {
                const auto g = static_cast<tubes::GameButton>(rebindRow);
                if (rebindWaiting) {
                    settings.bindings.bindKey(g, ev.key.keysym.scancode);
                    rebindWaiting = false;
                    writeSettings();
                } else if (k == SDLK_UP) {
                    rebindRow = rebindRow == 0 ? tubes::kGameButtons - 1
                                               : rebindRow - 1;
                } else if (k == SDLK_DOWN) {
                    rebindRow = rebindRow == tubes::kGameButtons - 1
                                    ? 0 : rebindRow + 1;
                } else if (k == SDLK_RETURN) {
                    rebindWaiting = true;
                } else if (k == SDLK_ESCAPE) {
                    rebindOpen = false;
                    // The port's own screen, so this follows the house rule
                    // rather than a call site: it borrows the classroom the
                    // way Instructions does, and Instructions fades.
                    changeScreen();
                }
                continue;
            }

            // The graphics screen, on the same terms. Left and Right work the
            // row, Enter is a synonym for Right so a player who only ever
            // presses Enter still gets round every value, and each change is
            // applied and saved AT ONCE - the point of a display option is
            // seeing what it does, and there is nothing here that can leave
            // the game in a state the player cannot get out of.
            if (graphicsOpen) {
                const auto r = static_cast<tubes::GraphicsRow>(graphicsRow);
                if (k == SDLK_UP) {
                    graphicsRow = graphicsRow == 0 ? tubes::kGraphicsRows - 1
                                                   : graphicsRow - 1;
                } else if (k == SDLK_DOWN) {
                    graphicsRow = graphicsRow == tubes::kGraphicsRows - 1
                                      ? 0 : graphicsRow + 1;
                } else if (k == SDLK_LEFT || k == SDLK_RIGHT ||
                           k == SDLK_RETURN) {
                    tubes::cycleGraphics(settings.graphics, r,
                                         k == SDLK_LEFT ? -1 : 1);
                    applyDisplayOptions(win, ren, settings.graphics);
                    writeSettings();
                } else if (k == SDLK_ESCAPE) {
                    graphicsOpen = false;
                    changeScreen();
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
                    changeScreen();
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
                            raiseBriefing(false);
                        }
                        sstage = tubes::firstStage(gameMode);
                        playSong(sstage == tubes::SessionStage::kBriefing
                                     ? tubes::kBriefingMusic
                                     : tubes::playMusicFor(
                                           game->dropsRemaining()));
                        stage = Stage::kPlay;
                        changeScreen();
                        break;
                    }
                    case tubes::MenuResult::kLoad: {
                        // `1b2e:4f70`: only a live record leaves the menu, and
                        // `1000:a525` then restores the session from it.
                        const tubes::MenuChoice& c = menu.choice();
                        const tubes::SaveBank b =
                            c.mode == 1 ? tubes::SaveBank::kEndurance
                                        : tubes::SaveBank::kWave;
                        const tubes::SaveSlot& rec =
                            saves[b].slots[c.slot - 1];
                        // The load path never visits the Difficulty page, so
                        // the session takes its difficulty from the record's
                        // own numbers instead: drops, interval and velocity
                        // are all restored below. k101 is only what the Game
                        // is built with before they are overwritten.
                        newSession(tubes::Difficulty::k101,
                                   bootSeed ^ 0x5bf03635u);
                        gameMode = c.mode;
                        flags = tubes::SessionFlags{};
                        totals = tubes::SessionTotals{};
                        banner = tubes::Banner::kNone;
                        paused = false;
                        briefingUp = false;
                        game->loadFrom(rec, totals);
                        if (c.mode == 2) {
                            // A save resumes at the START of its wave, so the
                            // briefing runs exactly as it would have.
                            game->startWave();
                            raiseBriefing(false);
                        }
                        sstage = tubes::firstStage(gameMode);
                        playSong(sstage == tubes::SessionStage::kBriefing
                                     ? tubes::kBriefingMusic
                                     : tubes::playMusicFor(
                                           game->dropsRemaining()));
                        stage = Stage::kPlay;
                        changeScreen();
                        break;
                    }
                    case tubes::MenuResult::kInstructions:
                        instrOpen = true;
                        instrCredits = false;
                        instrSlide = 0;
                        profIdle.restart(tubes::kTalkBurstsSlide, sceneRng);
                        joke.maybeStart(sceneRng);
                        // `1b2e:2d63` builds the scene with `1b2e:0510`, so
                        // the projector screen comes down every time - no
                        // `DS:0x210e`-style gate on this one.
                        screenRoll.restart();
                        changeScreen();
                        break;
                    case tubes::MenuResult::kCredits:
                        // `1000:b280`. Same screen, same keys, four pages.
                        instrOpen = true;
                        instrCredits = true;
                        instrSlide = 0;
                        profIdle.restart(tubes::kTalkBurstsSlide, sceneRng);
                        joke.maybeStart(sceneRng);
                        screenRoll.restart();
                        changeScreen();
                        break;
                    case tubes::MenuResult::kViewDemo:
                        if (startDemo()) changeScreen();
                        break;
                    case tubes::MenuResult::kToggleMusic:
                        musicOn = !musicOn;
                        if (musicOn) playSong("TUBES.MUS");
                        else music.stop();
                        persistAudio();
                        refreshOptionRows();
                        break;
                    case tubes::MenuResult::kToggleSound:
                        soundOn = !soundOn;
                        persistAudio();
                        refreshOptionRows();
                        break;
                    case tubes::MenuResult::kRedefine:
                        // The port's own screen - see input.h for why this is
                        // re-implemented rather than transliterated.
                        rebindOpen = true;
                        rebindRow = 0;
                        rebindWaiting = false;
                        changeScreen();
                        break;
                    case tubes::MenuResult::kGraphics:
                        // The port's own row AND its own screen - input.h
                        // again, one layer over.
                        graphicsOpen = true;
                        graphicsRow = 0;
                        changeScreen();
                        break;
                    case tubes::MenuResult::kQuit:
                        // `1000:abf7`: the shareware's Exit is menu item 10,
                        // and it does not quit - it calls the Ordering Info
                        // deck and only then Halts, at which point a Turbo
                        // Pascal exit procedure dumps TUBESEND.BIN over the
                        // text screen. The port fades to that screen and holds
                        // it, because it has no shell to leave it on.
                        //
                        // The registered build has no such path: `1b2e`'s exit
                        // arm quits outright, and its executable never names
                        // TUBESEND. So this is gated on the edition, not
                        // offered to everyone.
                        if (opt.edition.edition == tubes::Edition::kShareware &&
                            exitBanner.rowsLoaded() > 0) {
                            runFade(false);
                            runExitScreen(win, ren, exitBanner, fadeSteps,
                                          std::string());
                        }
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
                        changeScreen();
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

            // The save screen owns the keyboard while it is up, exactly as
            // the typing loops do - `1000:2dd0` does not return until ESC or
            // a completed save. `1000:3382` is the navigation and `1000:3423`
            // the two keys that end it.
            if (saveScreen) {
                if (saveWritten > 0.0f) continue;      // the written hold
                if (!saveTyping) {
                    if (k == SDLK_DOWN) {
                        // 1000:3393: five slots, and it WRAPS.
                        saveSlotSel = saveSlotSel == tubes::kSaveSlotsShown
                                          ? 1 : saveSlotSel + 1;
                    } else if (k == SDLK_UP) {
                        saveSlotSel = saveSlotSel == 1
                                          ? tubes::kSaveSlotsShown
                                          : saveSlotSel - 1;
                    } else if (k == SDLK_RETURN) {
                        // 1000:3448: the record is copied out and its
                        // description becomes the line being edited, so
                        // re-saving over a slot starts from what was there.
                        const tubes::SaveBank b =
                            gameMode == 1 ? tubes::SaveBank::kEndurance
                                          : tubes::SaveBank::kWave;
                        saveDesc = saves[b].slots[saveSlotSel - 1].description;
                        saveTyping = true;
                    } else if (k == SDLK_ESCAPE) {
                        saveScreen = false;            // 1000:3435
                    }
                    continue;
                }
                // The typing loop, the same one `1000:9744` runs for a high
                // score name - bounded here by the field's own 30 rather than
                // the high score screen's 25.
                if (k == SDLK_ESCAPE) {
                    // `1000:3635` jumps straight to the exit: ESC out of the
                    // description abandons the save, it does not commit it
                    // with whatever has been typed.
                    saveScreen = false;
                    saveTyping = false;
                } else if (k == SDLK_RETURN) {
                    const tubes::SaveBank b =
                        gameMode == 1 ? tubes::SaveBank::kEndurance
                                      : tubes::SaveBank::kWave;
                    tubes::SaveSlot& rec = saves[b].slots[saveSlotSel - 1];
                    // 1000:3654 onward: the description first, then the
                    // fourteen session fields, then the whole record into the
                    // bank and the file out.
                    rec.setDescription(saveDesc.empty()
                                           ? tubes::kSaveUndescribed
                                           : saveDesc);
                    game->saveInto(rec, totals);
                    // `1b2e:00ac` rolls two random numbers before writing, so
                    // saving perturbs the sequence - in the original too.
                    tubes::stampSaveNonces(
                        saves, game->rollForTest(tubes::kSaveNonceMax) + 1,
                        game->rollForTest(tubes::kSaveNonceMax) + 1);
                    writeSaves();
                    saveTyping = false;
                    saveWritten = tubes::kSaveWrittenSeconds;
                } else if (k == SDLK_BACKSPACE) {
                    if (!saveDesc.empty()) saveDesc.pop_back();
                } else if (k >= 0x20 && k <= 0x7e &&
                           static_cast<int>(saveDesc.size()) <
                               tubes::kSaveDescTyped) {
                    const bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                    char c = static_cast<char>(k);
                    if (shift && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
                    saveDesc.push_back(c);
                }
                continue;
            }

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
                // `1000:86b8` ends with the fade-out and `1000:44d3` fades the
                // playfield in, so the briefing and the game are two screens.
                changeScreen();
                continue;

            case tubes::SessionStage::kBanner:
                // `1000:5eec`: the ABORT arm draws its hint and then calls
                // `1000:2dd0` - the whole in-game key dispatch - a SECOND
                // time, which is what makes the offer real. "F2 to Save Game,
                // ESC for Main Menu!" is not decoration: F2 there opens the
                // save screen, and it is the last chance to save a session
                // the player has just abandoned.
                //
                // The port dismissed the banner on any key at all, so the
                // hint pointed at nothing and the only way to save was to
                // remember F2 BEFORE aborting. Reported from play.
                if (banner == tubes::Banner::kAborted &&
                    bannerPhase == tubes::BannerPhase::kWait &&
                    code == tubes::gamekey::kF2 && gameMode != 0 &&
                    !tubes::kSaveDisabled) {
                    saveScreen = true;
                    saveTyping = false;
                    saveWritten = 0.0f;
                    if (saveSlotSel < 1 ||
                        saveSlotSel > tubes::kSaveSlotsShown) {
                        saveSlotSel = 1;
                    }
                    continue;
                }
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
                // `1000:a657`, and it is two instructions sitting INSIDE the
                // progression block - so it is reached only when the wave is
                // being advanced, which is what `t.advanceWave` means here:
                //
                //     if wave >= 75 then RegisteredEnding;
                //     wave := wave + 1
                //
                // The ending's own first act is to set the session's game-over
                // flag through the static link (`SS:[DI + 0xfe02] := 1`), so
                // the session is over the moment it is.
                if (t.advanceWave &&
                    game->progress().wave >= tubes::kEndingWave) {
                    endingPage = 1;
                    endingTimer = tubes::kEndingHoldSeconds;
                    jumpFrame = 1;
                    jumpAccum = 0.0f;
                    // `1000:9499` waits with `1b2e:0b8f` only - it never
                    // reaches the talk loop, so the stats screen's
                    // `1b2e:0cd1(10)` has to stop here rather than run on
                    // underneath the hop.
                    profIdle.restart(0, sceneRng);
                    flags.gameOver = true;
                    continue;
                }
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
                    const bool wasReplay = flags.replay;
                    game->startWave(flags.replay);
                    flags.replay = false;   // `1000:86b8`'s tail clears it
                    raiseBriefing(wasReplay);
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
                        const bool wasReplay = flags.replay;
                        game->startWave(flags.replay);
                        flags.replay = false;
                        raiseBriefing(wasReplay);
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
            //
            // `DS:0x1d4b` is the save-disabled flag, and it is written in
            // exactly one place - `1000:b1e4`, which sets it to ZERO - and
            // read in three: this F2 gate, the abort banner's F2 hint, and the
            // high-score offer. So **saving is always enabled** in the shipped
            // build; the flag looks like a switch for an edition that never
            // came. The port hard-coded `true` here while the save screen did
            // not exist, which quietly made F2 dead once it did.
            switch (tubes::classifyGameKey(code, gameMode == 0,
                                           tubes::kSaveDisabled)) {
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
                persistAudio();
                break;
            case tubes::GameAction::kSoundToggle:
                soundOn = !soundOn;
                persistAudio();
                break;
            case tubes::GameAction::kSave:
                // `1000:3062`: the screen comes up on the slot the player last
                // used, and the game is frozen until it is done.
                saveScreen = true;
                saveTyping = false;
                saveWritten = 0.0f;
                if (saveSlotSel < 1 || saveSlotSel > tubes::kSaveSlotsShown) {
                    saveSlotSel = 1;
                }
                break;
            case tubes::GameAction::kHelp:
                // `1b2e:2d63`'s help body is read as a dispatch but its screen
                // is not decompiled. Left inert rather than invented.
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

        // `1b2e:0510` runs before the first slide is written, so the screen
        // finishes coming down while the slideshow waits. This is outside the
        // stage dispatch on purpose: the Instructions and the Credits are
        // their own screen, and `--instructions` opens them from the harness
        // path, where `stage` is `kPlay` rather than `kTitle`.
        if (instrOpen) screenRoll.tick(dt);
        // And the professor's idle with it. Same reason it sits outside the
        // stage dispatch: these two screens are reachable with `stage` set to
        // either, and the session's own clock below is the briefing's.
        if ((instrOpen || rebindOpen || graphicsOpen) && !joke.active()) {
            profIdle.tick(dt, sceneRng);
        }
        // `1b2e:0b8f` steps `DS:0x20fc` 1..3 every ten retraces while the
        // ending waits, and each page gives up after thirty seconds.
        if (endingPage > 0) {
            jumpAccum += dt * tubes::kRetraceHz;
            while (jumpAccum >= tubes::kJumpRetraces) {
                jumpAccum -= tubes::kJumpRetraces;
                if (++jumpFrame > tubes::kJumpFrames) jumpFrame = 1;
            }
            endingTimer -= dt;
            if (endingTimer <= 0.0f) endEndingPage();
        }

        // `1b2e:084e` runs to its own three delays and plays `SLIDE.SFX` as it
        // ends. It is held while the screen is still coming down because it
        // lives inside `1b2e:0a11`, and `1b2e:0510` has returned before its
        // caller reaches that - the port arms both at the same instant, so
        // without this the gag would expire behind the rolling screen.
        if (!screenRoll.rolling() && joke.tick(dt) && soundOn &&
            haveSlideSound) {
            music.playSound(&slideSound);
        }

        // --demo drives the REAL loop with the scripted player, so the render
        // path gets exercised on every frame of a whole session rather than
        // only on the one frame --auto screenshots. That distinction matters:
        // a crash that needs both a full beaker and a live render is invisible
        // to --auto, which simulates first and draws once at the end.
        if (stage == Stage::kTitle) {
            // The Instructions, the Credits and the rebinding screen all
            // borrow the classroom, so they borrow the professor's clock too -
            // `1b2e:0e37` steps him every ten retraces, and he waves his
            // pointer while any of the three waits for a key.
            //
            // This used to read `if (instrOpen)` around a COPY of the render
            // section's slide draw, which ended the frame with its own
            // `continue` before the clock below could run - so the professor
            // stood still on the two screens the original animates him on, and
            // the render section's own block was unreachable. The draw is gone
            // from here; the clock is what belongs in the update.
            // `1b2e:52bf`'s loop body, at the TITLE screen's own rate - see
            // kTitleHz. Everything in it is counted in frames: 4 px a frame
            // along a leg, a star frame every three, 720 frames to attract.
            titleAccum += dt * tubes::kTitleHz;
            int steps = static_cast<int>(titleAccum);
            titleAccum -= static_cast<float>(steps);
            if (steps > 8) steps = 8;      // a stall must not teleport the atom
            // The countdown belongs to the TITLE screen and to nothing else.
            // Instructions, the Credits, the high score viewer and the
            // rebinding screen are separate screens with waits of their own -
            // `1b2e:0e37(30)` for the slideshows, `kHsViewSeconds` for the
            // viewer - and the original's countdown is inside `1b2e:52bf`,
            // which is not running while any of them is up. The port ran it
            // regardless, so a player halfway through binding a key could be
            // dropped into the demo. Reported from play.
            const bool onTitleProper =
                !instrOpen && !rebindOpen && !graphicsOpen && !hsViewing;
            for (int k = 0; k < steps; ++k) {
                titleAtom.step();
                if (menu.up()) menu.tick();
                if (onTitleProper) --attractTimer;
            }
            if (onTitleProper && attractTimer <= 0) {
                attractTimer = tubes::kAttractTimeout;
                // `1000:b287`: the arm runs the blackboard cutscene FIRST and
                // skips the demo entirely if it returns 2 - which is ESC.
                SkipWatch attractSkip;
                const int k = runCutscene(
                    res, ren, tex, screen, rgba, palRaw, pal, fadeSteps,
                    attractSkip, music, musicOn, soundOn,
                    haveBlackboard ? &blackboard : nullptr, haveBlackboard,
                    atoms, haveAtom, smallFont, haveSmall, -1, -1,
                    std::string());
                // The cutscene ends on black, so there is nothing to fade
                // OUT of - whatever comes next just fades in.
                if (k != 2) startDemo();
                pendingFadeIn = true;
            }
        } else if (opt.screenshot.empty() || opt.shotAfter > 0) {
            // `--screenshot` alone captures the opening frame and exits, so it
            // deliberately does not simulate. `--screenshot-after N` does, or
            // it could never reach a screen that is past a game over.
            if (opt.playDemo || attractDemo) {
                // The recording is consumed at the fixed game step rather than
                // through `update`'s real-time conversion - same accumulator,
                // driving an index instead. One byte per frame the tube was
                // IDLE, not per frame: see Game::acceptsInput.
                demoAccum += dt * tubes::kFrameHz;
                int steps = static_cast<int>(demoAccum);
                demoAccum -= static_cast<float>(steps);
                if (steps > 8) steps = 8;
                for (int k = 0; k < steps; ++k) {
                    if (demoFrame >= demo.input.size()) {
                        // `--play-demo` is the oracle and stops the program
                        // when the tape runs out. Attract mode just goes back
                        // to the title, the way running out of drops would.
                        if (opt.playDemo) running = false;
                        else flags.aborted = true;
                        break;
                    }
                    game->stepOnce(game->acceptsInput() ? demo.input[demoFrame++]
                                                      : 0);
                }
            } else if (sstage == tubes::SessionStage::kPlay && !paused &&
                       !saveScreen) {
                game->update(opt.demo ? scriptedInput(*game)
                                      : readInput(settings.bindings, gamepad),
                             dt);
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
                    // `[DS:0x22ce]` asks the driver whether the song has been
                    // round once. With music switched off there is no song, so
                    // it has - which is also what keeps attract mode turning
                    // over with the music off: nobody is there to press a key.
                    const bool musicDone = !musicOn || !music.isOpen() ||
                                           music.songLooped();
                    if (bannerWaitsForMusic && musicDone) leaveBannerWait();
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
            if (instrOpen || rebindOpen || graphicsOpen) {
                // Already ticked above - the screen on top owns him.
            } else if (joke.active()) {
                // `1b2e:084e` blocks - nothing else on the screen moves.
            } else if (sstage == tubes::SessionStage::kBriefing ||
                       sstage == tubes::SessionStage::kStats ||
                       sstage == tubes::SessionStage::kContinue) {
                profIdle.tick(dt, sceneRng);
            } else {
                profIdle.restart(0, sceneRng);
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
                    changeScreen();
                }
            }

            // `1000:3722`: twenty retraces after the file is written, deaf,
            // and then the game resumes where it left off.
            if (saveWritten > 0.0f) {
                saveWritten -= dt;
                if (saveWritten <= 0.0f) {
                    saveWritten = 0.0f;
                    saveScreen = false;
                    refreshSaveSlots();
                }
            }

            // `1b2e:0510`'s roll-down. It is armed by `raiseBriefing` only for
            // wave 1 of a session that is not a replay, which is the condition
            // `1000:86b8` itself tests before choosing between `1b2e:0510` and
            // `1b2e:0656`.
            if (briefingUp) screenRoll.tick(dt);

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
            // Drain the frame's whole queue. The original would have had
            // each of these cut the last one off - one voice - and the port
            // now lets them overlap instead; see sfx.h for why that departure
            // was taken and what it does NOT change.
            //
            // The sounds are TAKEN either way, so F4 mutes without desyncing
            // anything - `1000:3859` toggles the driver, it does not stop the
            // game asking for sounds.
            for (int8_t want = game->takeSound(); want != tubes::sfx::kNone;
                 want = game->takeSound()) {
                if (soundOn && want >= 0 && want < tubes::sfx::kCount &&
                    sounds[want].valid()) {
                    music.playSound(&sounds[want]);
                }
            }
        }

        // The Instructions, the Credits, the high score viewer and the
        // rebinding screen each REPLACE the title while they are up, so each
        // ends the frame with its own `continue` before the title is drawn.
        if (instrOpen) {
            const bool cr = instrCredits;
            drawInstructionSlide(screen,
                                 cr ? tubes::kCreditPages
                                    : tubes::kInstructionSlides,
                                 cr ? tubes::kCreditPageCount
                                    : tubes::kInstructionSlideCount,
                                 instrSlide, &blackboard, haveBlackboard,
                                 sceneArt, scenePose(false),
                                 smallFont, haveSmall, headingFont,
                                 haveHeading, atoms, haveAtom, testTube,
                                 haveTube, furn, haveFurn);
            presentFrame();
            continue;
        }

        if (rebindOpen) {
            drawRebindScreen(screen, settings.bindings, rebindRow,
                             rebindWaiting, &blackboard, haveBlackboard,
                             sceneArt, headingFont, haveHeading, scriptFont,
                             haveScript, smallFont, haveSmall,
                             tubes::pointerFrameFor(profIdle.wave));
            presentFrame();
            continue;
        }

        if (graphicsOpen) {
            drawGraphicsScreen(screen, settings.graphics, graphicsRow,
                               &blackboard, haveBlackboard, sceneArt,
                               headingFont, haveHeading, scriptFont,
                               haveScript, smallFont, haveSmall,
                               tubes::pointerFrameFor(profIdle.wave));
            presentFrame();
            continue;
        }

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
                    changeScreen();
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
            drawBriefing(screen, *game, &blackboard, haveBlackboard, headingFont,
                         smallFont, haveBig, haveSmall, atoms, haveAtom, furn,
                         haveFurn, briefDecor, sceneArt, scenePose(true));
        }

        // `1000:9499` replaces the field the same way the stats screen does,
        // and comes first because the session it ends is still notionally on
        // the stats stage when it starts.
        if (endingPage > 0) {
            ScenePose p = scenePose(false);
            p.jumpFrame = jumpFrame;
            drawEnding(screen, endingPage, &blackboard, haveBlackboard,
                       sceneArt, p, headingFont, haveHeading, smallFont,
                       haveSmall, &prizeArt, havePrize);
        }

        // The stats screen replaces the field; the banner, the Continue prompt
        // and the pause overlay go OVER whatever is already drawn, because
        // that is what the original does - none of the three clears first.
        if (endingPage == 0 &&
            (sstage == tubes::SessionStage::kStats ||
             sstage == tubes::SessionStage::kContinue)) {
            drawStats(screen, statsRows, &blackboard, haveBlackboard, sceneArt,
                      headingFont, smallFont, bigFont, haveHeading, haveSmall,
                      haveBig, scenePose(false));
        }
        if (sstage == tubes::SessionStage::kBanner) {
            // `1000:5ec9`: the F2 hint appears only on the abort arm, and only
            // when the mode is not attract and saving is enabled.
            drawBanner(screen, banner, headingFont, haveHeading, smallFont,
                       haveSmall,
                       banner == tubes::Banner::kAborted && gameMode != 0 &&
                           !tubes::kSaveDisabled);
        }
        if (sstage == tubes::SessionStage::kContinue) {
            drawContinue(screen, continuePrompt.ticksLeft(), headingFont,
                         haveHeading, bigFont, haveBig);
        }
        if (hsActive) {
            // `1000:96db` does NOT call `1b2e:0656` or `1b2e:0a11`. It draws
            // three things and they are all here:
            //
            //     Draw(0, 12, BLACKBRD)          { 2321:068d, opaque }
            //     Draw(57, 26, SLIDEBAR)         { 2321:0711, masked }
            //     FillRect(10, 37, 299, 118, 111)
            //
            // and `drawHiScores` makes the third. So there is no projector
            // screen on this screen, no slide, no corners and no professor -
            // the port put the whole classroom behind it and the screen showed
            // through the panel. Reported from play. The bar sits at y 26, the
            // height it has before the roll-down, which is the same place the
            // VIEWER parks it.
            screen.clear(0);
            if (haveBlackboard) screen.blit(blackboard, 0, tubes::kBoardY);
            if (haveBar) {
                screen.blit(slideBar, tubes::kHsViewBarX, tubes::kHsViewBarY);
            }
            drawHiScores(screen, hiScores[hsBank], headingFont, haveHeading,
                         scriptFont, haveScript, hsRow, hsName,
                         hsHold > 0.0f ? 0 : hsCursor);
        }
        if (saveScreen) {
            const tubes::SaveBank b = gameMode == 1
                                          ? tubes::SaveBank::kEndurance
                                          : tubes::SaveBank::kWave;
            drawSaveScreen(screen, saves[b], b, saveSlotSel, saveTyping,
                           saveDesc, headingFont, haveHeading, scriptFont,
                           haveScript, &smallBall[1], haveSmallBall[1]);
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
