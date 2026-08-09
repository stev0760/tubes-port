// tubes-port - an SDL reimplementation of Tubes (Absolute Magic, 1994).
//
// Ships no game data. Assets are read at runtime from the user's own copy of
// the original game; point --gamedir at the directory holding `TUBES.RES`.

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
#include "uistate.h"
#include "gfx.h"
#include "hiscore.h"
#include "input.h"
#include "cutscene.h"
#include "ending.h"
#include "ordering.h"
#include "instructions.h"
#include "save.h"
#include "menu.h"
#include "mus.h"
#include "opl.h"
#include "res.h"
#include "screen.h"
#include "boot.h"
#include "present.h"
#include "screens.h"
#include "session.h"
#include "wave_text.h"
#include "scr.h"
#include "sfx.h"

namespace {

// Playfield geometry, all measured from the draw loop in `1000:3a67`.
//
// The playfield is 6 x 5, not the 7 x 10 the manual implied. Columns are
// pitched 18 apart while the sprites are 16 wide, which is why nothing lined
// up when the pitch was assumed equal to the cell size. The grid spans
// x 107..212, centred on 160 - exactly the centre of the x 74..245 gap
// between the tube walls in `GAMEFG.GFX`. See docs/reversing-notes.md.
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
// sit consecutively at `1000:9d07`:
//
//     RFADE GFADE BFADE CFADE PFADE YFADE PNKFADE FFADE AFADE GLDFADE CRFADE
//
// followed immediately by the same eleven with `.SFX` - one sound per family,
// which is why the clear sound follows the colour the stack matched, rather
// than each ball's own type.
//
// Types 11..17 and 19 have null entries: they are never cleared by matching,
// so they have no fade of their own. A fade family is an effect, not a "can be
// cleared" marker - `AFADE` is AntiMatter's blast applied to everything caught
// in it, and `CRFADE` is the Crystal's teleport, forward then reverse.
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
    nullptr,      // 19 `MYSTBALL` is a rendering state, not a ball
};

// The sound table, indexed exactly as the original's is - by atom type, with
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
    nullptr,            // 19  `MYSTBALL` is a rendering state, not a ball
    "HITGLASS.SFX",     // 20  landing on the beaker floor
    "HITATOM.SFX",      // 21  landing on another atom, and the beaker settling
    "SELECT.SFX",       // 22  the wave-mode element cycle
};



// The Task Display's half-size balls, `DS:0x200a`, loaded by name at
// `1000:ad41`. Same seven colours in the same order as the full-size table,
// and `1000:2894` is their only consumer in the game session.
const char* kSmallBallFiles[7] = {
    "SRBALL.CSP", "SGBALL.CSP", "SBBALL.CSP", "SCBALL.CSP",
    "SPBALL.CSP", "SYBALL.CSP", "SPNKBALL.CSP",
};

const char* kFurnFiles[tubes::kFurnCount] = {
    "TUBEH.CSP",  "TUBEHS.CSP",  "TUBEHR.CSP",
    "TUBEV.CSP",  "TUBEVS.CSP",  "TUBEVR.CSP", "TUBEVRS.CSP",
    "TUBEVL.CSP", "TUBEVLS.CSP",
    "BEAKER.CSP", "BEAKERS.CSP", "TESTUBES.CSP", "MARKER.CSP",
};

struct FurnDraw {
    uint8_t sprite;
    int16_t y, x;
};

// Transcribed in order from the decompiled draw sequence. The two `ATOMS`
// marks in that sequence split this into three groups; see kFurnGroup* below.
const FurnDraw kFurniture[] = {
    // --- group 0: back layers, drawn before any atom ---
    {tubes::kTubeH,   26,  34}, {tubes::kTubeH,   26, 270}, {tubes::kTubeH,   26,  58},
    {tubes::kTubeH,   26, 246}, {tubes::kTubeH,   26, 107}, {tubes::kTubeH,   26, 197},
    {tubes::kTubeHR,  26, 125}, {tubes::kTubeH,   26, 179},
    {tubes::kTubeHS,  13,  58}, {tubes::kTubeHS,  13, 246}, {tubes::kTubeHS,  13, 107},
    {tubes::kTubeHS,  13, 197},
    {tubes::kTubeVLS, 26,  34}, {tubes::kTubeVRS, 26, 270}, {tubes::kTubeVRS, 26, 125},
    {tubes::kTubeVLS, 26, 179},
    // --- group 1: mid layers ---
    {tubes::kTubeH,   13,  58}, {tubes::kTubeH,   13, 246}, {tubes::kTubeHR,  13, 107},
    {tubes::kTubeH,   13, 197},
    {tubes::kTubeVL,  26,  34}, {tubes::kTubeVR,  26, 270}, {tubes::kTubeVR,  26, 125},
    {tubes::kTubeVL,  26, 179},
    {tubes::kTubeVLS, 13,  58}, {tubes::kTubeVRS, 13, 246}, {tubes::kTubeVRS, 13, 107},
    {tubes::kTubeVLS, 13, 197},
    {tubes::kTubeVS,  26,  58}, {tubes::kTubeVS,  26, 246}, {tubes::kTubeVS,  26, 107},
    {tubes::kTubeVS,  26, 197},
    // --- group 2: front layers ---
    {tubes::kTubeVL,  13,  58}, {tubes::kTubeVR,  13, 246}, {tubes::kTubeVR,  13, 107},
    {tubes::kTubeVL,  13, 197},
    {tubes::kTubeV,   26,  58}, {tubes::kTubeV,   26, 246}, {tubes::kTubeV,   26, 107},
    {tubes::kTubeV,   26, 197},
};
constexpr int kFurnGroup0 = 16;   // atoms are drawn after this many
constexpr int kFurnGroup1 = 32;   // and again after this many
constexpr int kFurnTotal = static_cast<int>(sizeof(kFurniture) /
                                            sizeof(kFurniture[0]));

// `DEMO.SCR` is recorded at TUBES 301, not 101.
//
// `DS:0x1d4f` is the difficulty index - `1000:a483` switches on it and is the
// only writer of the three constants the session runs on:
//
//     0 -> drops 9, velocity 0x100, spawn interval 0x46 (70)     Tubes 101
//     1 -> drops 6, velocity 0x180, spawn interval 0x3c (60)     Tubes 201
//     2 -> drops 3, velocity 0x200, spawn interval 0x32 (50)     Tubes 301
//
// and the menu's View Demo arm sets `[0x1d4f] := 2` at `1000:b272` before
// calling the session. It was read as a mode flag at first because the same
// arm also sets `[0x1d4e]` and `[0x1d4c]`, and because `1000:b1ee` presets the
// difficulty block to the 101 values before the menu loop - which made 101
// look like what the demo inherits. It is not: a483 rewrites the block on
// entry.
//
// Confirmed live: the running demo reads 3 at the drops counter (`0x245bc`)
// before the session has made its first `Random` call. Only arm 2 produces a
// 3.
//
// This matters far more than "the demo starts with fewer lives". The interval
// sets how often an atom is dispensed and the velocity how fast it travels, so
// at 101 the port was dispensing on a 70-frame beat against a recording made
// on a 50-frame one. The recorded player was reaching for atoms that were not
// there yet - which is exactly the symptom the oracle reported.
constexpr tubes::Difficulty kDemoDifficulty = tubes::Difficulty::k301;

// Derived. `23e7:0024` is a vertical-retrace wait - it polls port 0x3da bit 3
// low-then-high `n` times - so `1b2e:0e37(param)`, which runs `param * 7`
// iterations of `23e7:0024(10)`, waits `param * 70` retraces. At Mode X's
// 70 Hz that is `param` seconds exactly, and the round number is what
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
    bool dumpSfx = false;       // print every `.SFX` header and exit
    std::string dumpAnm;        // print a `.ANM`'s runs, to diff the decoder
    std::string dumpSpr;        // print a `.SPR`'s frame dimensions
    // Decode a `TUBES.SAV` and print every field, so the C++ decoder can be
    // diffed against `tools/sav_decode.py` rather than trusted.
    std::string dumpSave;
    bool playDemo = false;      // replay `DEMO.SCR` through the live loop
    bool demoTrace = false;     // run `DEMO.SCR` headless and print the spawns
    int randomTrace = 0;        // with --demo-trace: print the first N rolls
    std::string demoCsv;        // with --demo-trace: per-frame state, for the rig
    std::string gameBg = "GAMEBG1.GFX";   // backdrop, for matching a capture
    // True when the edition came from the command line rather than from the
    // settings file. The flags are the developer's and the harness's override:
    // they win, they do not persist, and they suppress the first-run prompt -
    // so a capture script never blocks on a question and never rewrites the
    // player's answer.
    bool editionFromFlag = false;
    // Which build of Tubes to be. `--shareware` is the 25-wave edition, and
    // `--preview` opens its Preview Registered mode - menu arm 3, and the same
    // flag View Demo and the attract loop set. See edition.h.
    tubes::EditionState edition{};
    // Open the shareware exit screen directly, for capture. Like every other
    // harness flag it must never write to the game directory.
    bool exitScreen = false;
    // Open the shareware's Ordering Info deck at page N, for capture.
    int ordering = -1;
    // Open the shareware's wave-25 end screen, for capture.
    bool registration = false;
    double renderSeconds = 0;   // 0 = one pass, songs loop forever
    int wave = 0;               // 0 = Endurance; 1..75 starts Wave mode there
    int titlePage = -1;         // -1 off; 0 the bare title; 1..7 a menu page
    int hsPage = -1;            // -1 off; 0 Endurance, 1 Wave in the viewer
    bool f1 = false;            // open the F1 help overlay, for capture
    bool f2 = false;            // open the F2 save screen, for capture
    bool rebind = false;        // open the rebinding screen, for capture
    bool graphics = false;      // open the graphics screen, for capture
    // Open the first-run edition prompt on answer N, for capture.
    int editionPrompt = -1;
    int instr = -1;             // open the Instructions on slide N, for capture
    bool credits = false;       // open the Credits, for capture
    // Harness only. `--screenshot` captures the first frame drawn, which can
    // never show a screen that is reached by playing - the banners, the stats
    // screen and the Continue prompt are all past a game over. These two run
    // the real loop to get there instead of adding entry points that the
    // original does not have.
    // `[DS:0x0ce6]` is 40 in the image, and the player has said outright that
    // the half second between screens may be sped up or turned off. So the
    // knob exists and its default is the original's number; 0 cuts instead.
    int fadeSteps = tubes::kFadeSteps;
    bool noSplash = false;      // skip the boot splashes outright
    int splashFrame = -1;       // capture this .ANM frame of the first splash
    int splash2Step = -1;       // capture this step of the second splash
    int cutscenePage = -1;      // capture this page of the opening cutscene
    int cutsceneTick = -1;      // ... at this tick of it, rather than midway
    int shotAfter = 0;          // present the screenshot after N live frames
    bool joke = false;          // force `1b2e:084e`, which is a 5% roll
    int ending = -1;            // open `1000:9499` at page 1 or 2
    std::string makeSave;       // write a `TUBES.SAV` for --wave N and exit
    bool hsEntry = false;       // open the high score entry screen, `1000:96db`
    bool autoAdvance = false;   // synthesise `RETURN` whenever a stage waits
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
// because that index is the column for the six network atoms, and the index
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
                // falling into the beaker. They used to be skipped, because
                // the engine settled a tip instantly and had nowhere to put
                // one; now they are records like any other and load straight
                // in.
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
        } else if (a == "--f1") {
            o.f1 = true;
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
        } else if (a == "--registration") {
            o.registration = true;
            o.edition.edition = tubes::Edition::kShareware;
        } else if (a == "--ordering") {
            o.ordering = (i + 1 < argc && argv[i + 1][0] != '-')
                             ? std::atoi(argv[++i]) : 0;
            o.edition.edition = tubes::Edition::kShareware;
        } else if (a == "--exit-screen") {
            o.exitScreen = true;
            o.edition.edition = tubes::Edition::kShareware;
        } else if (a == "--edition-prompt") {
            o.editionPrompt = 0;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                o.editionPrompt = std::atoi(argv[++i]);
            }
        } else if (a == "--shareware") {
            o.edition.edition = tubes::Edition::kShareware;
            o.editionFromFlag = true;
        } else if (a == "--registered") {
            // The other half of `--shareware`, and it is not decoration. Once
            // the answer is remembered, a flag that can only say "shareware"
            // is a one-way door: a player who answered shareware, or who wants
            // one registered run against a registered install, would have had
            // to hand-edit the settings file to get back. Both directions or
            // neither.
            o.edition.edition = tubes::Edition::kRegistered;
            o.editionFromFlag = true;
        } else if (a == "--preview") {
            // The Preview only exists in the shareware build, so asking for it
            // implies the edition rather than needing both flags.
            o.edition.edition = tubes::Edition::kShareware;
            o.edition.preview = true;
            o.editionFromFlag = true;
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
        "  --edition-prompt [N]  open the first-run edition prompt, for capture\n"
        "  --shareware       play the 25-wave shareware edition\n"
        "  --registered      play the 75-wave registered edition\n"
        "  --exit-screen     show TUBESEND.BIN, the shareware sign-off\n"
        "  --ordering [N]    open the shareware Ordering Info deck at page N\n"
        "  --registration    open the shareware wave-25 end screen\n"
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









// The task display, `1000:2a4a` - the small ball and the number in the top
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
// font's advance, so the number is centred about x = 14 rather than moved.
//
// `1000:2894`, the modes 2 and 3 arm, is not decompiled: it draws the chain
// illustration that shows which orientation is wanted. Left undrawn rather
// than invented, so those waves show their count and nothing else.










    // The F2 save screen, `1000:2dd0`'s save arm. It draws over the play field
    // the frame loop left up - the original flips to the other video page and
    // puts this on it, so the caller supplies whatever background it likes.

// What a binding is called. SDL owns these names, which is the whole reason
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





// The shareware exit screen, `TUBESEND.BIN`, presented at 640 x 400.
//
// A screen this port presents that the original does not: the shareware
// build `Move`s this dump to 0xB800 and quits, leaving the banner on the shell
// with the DOS prompt landing in the two rows the file deliberately omits. A
// windowed port has no shell to leave it on, so it draws the banner and holds
// it until a key. That hold is the invented part - see `textscreen.h`.
//
// It goes through the same `presentRect` as everything else, and gets
// fullscreen, window scale, 4:3 correction and scanlines for free, because 640
// x 400 and 320 x 200 have the identical 1.6 aspect and therefore the
// identical destination rectangle. Only the source texture differs.
//
// The reveal is `23e7:0097`'s fade, run on the text palette rather than a
// game one - the fade is a graphics-unit routine that every screen calls, so
// using it here is reusing the original's own mechanism rather than imitating
// it.
// The port's own, and invented rather than read out of anything - see the
// comment at the call site. `C:\TUBES>` and a blinking underline, put where
// DOS would have put them.
//
// The two rows exist because the prompt lands there: 3,680 bytes is 23 rows of
// a 25-row screen, and the dump stops short so the shell's next line does not
// scroll the art. Drawing the prompt fills the gap the file was shaped around,
// which is why it reads as finished rather than as cropped.
using tubes::kPromptRow;
using tubes::kPromptAttr;
using tubes::kCursorBlinkRetraces;
using tubes::kCursorTopRow;







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
// This is the port's input driver: everything above it is the original's, and
// `DEMO.SCR` replay feeds the same byte from a file instead.
//
// Keyboard and pad are OR-ed rather than switched between. The original bound
// one driver at a time because DOS gave it no choice; SDL has no such reason,
// and a player with a controller plugged in should not have to visit a menu.
// What a controller button means on a screen that is waiting - a menu, a
// slideshow, the save or high score screen, a banner, the cutscene.
//
// The original's menu tests joystick button `0x20` directly (`1b2e:4d80`),
// because DOS gave it no abstraction over a gameport; SDL is that
// abstraction, so this maps the player's own bindings onto the keys those
// screens already handle rather than porting a driver's button numbers. Same
// reasoning as the rebinding screen - see input.h.
//
// Live play does not come through here. `readInput` polls the pad directly,
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
    // stream against the Python tool rather than by listening; this is the
    // same check for the digital side, and it is what caught the rate being a
    // WORD.
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

    // Prints what `tools/anm_decode.py` prints - per frame, the number of
    // runs, the bytes they carry and an FNV-1a over (offset, bytes). Two
    // interpreters of the same compiled x86 have to agree run for run, not
    // merely produce a picture that looks right: a wrong SI would still paint
    // something plausible, and the .CSP geometry bug this project already had
    // is that failure mode.
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
    // stores only the player's buttons, so which colour appears in which
    // column is decided entirely by `Random` - by the generator and by how
    // many times each frame calls it. Nothing else in the port cross-checks
    // that.
    //
    // It is also robust to timing. A trace captured off the original by
    // polling its memory cannot be aligned frame for frame, but the Nth atom
    // it dispenses is the Nth either way.
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
        // so how often it is pressed sets how long a record stays occupied,
        // which decides how often the spawn re-rolls its column.
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
        // the two disagree is the divergence and the original's log names the
        // call site that produced it.
        std::vector<std::pair<int, uint32_t>> rolls;
        // Per-frame state, in the form the rig diffs against the original.
        // `idx` is the key that matters: the original's demo reader keeps its
        // stream index at `24c1:001e` (linear 0x24c2e) and `read` increments
        // it, so the two sides align on the byte being consumed rather than
        // on any notion of time.
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
        // One byte per idle frame - see Game::acceptsInput. The frame count is
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
                // The six network records: state and y. A catch turns on both.
                for (int c = 1; c <= tubes::kAtomSlots; ++c) {
                    std::fprintf(csv, "%d:%d;", g.atom(c).state, g.atom(c).y);
                }
                std::fprintf(csv, ",");
                // The beaker, in the original's own row-major order, so the
                // two sides diff cell for cell. A catch that goes the other
                // way shows up here long before it shows up in the score.
                //
                // The raw cell, `type + 19*fadeFrame`, is what the original's
                // beaker plane holds. Writing typeAt() here instead strips the
                // fade and makes every clearing cell look like an unmatched
                // one; it produced a confident false report of the matcher
                // missing a diagonal.
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
                    // without any notion of time: `RandSeed` is one orbit of
                    // an injective LCG, so reading it off the original
                    // converts straight back into "how many times Random has
                    // called". Spawn N is spawn N in both runs, so comparing
                    // the count at each spawn needs no frame alignment and no
                    // breakpoint. That is what made this the usable instrument
                    // after Z0 on the RTL turned out not to trap.
                    std::printf("spawn %4d frame %5zu col %d type %2d rolls %zu\n",
                                ++spawns, f, c, g.atom(c).colour, rolls.size());
                    born[c] = static_cast<int>(f);
                }
                if (g.dropsRemaining() != drops) {
                    // A bonus caught gives one back - `1000:180c` - so this is
                    // not always a loss, and calling every change a miss made
                    // the trace read as three misses where one was a gain.
                    const bool gained = g.dropsRemaining() > drops;
                    drops = g.dropsRemaining();
                    std::printf("%-5s       frame %5zu  drops now %d\n",
                                gained ? "BONUS" : "MISS", f, drops);
                }
                if (!now && wasActive[c]) {
                    // The record going free is half the spawn rule: the column
                    // is re-rolled up to ten times looking for a free slot, so
                    // how long an atom occupies its record decides how often
                    // the original retries, and the retry count is exactly
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
    bool haveBg = tubes::loadImage(res, opt.gameBg, background, -1);
    // The briefing is not drawn over the play backdrop. `1000:86b8` loads
    // GAMEBG for the wave that is about to start - into `[BP-0x86]`, which
    // `1000:3a67` blits at `1000:3c0b` - and draws its own text over a
    // blackboard scene instead. Confirmed by capturing the original.
    // The title screen's pair, and the four star frames the Pascal main
    // program loads as shared sprites (`1000:aaba`) - which is why the title
    // screen itself only names four files and not these.
    tubes::Image titleBg, titleFg;
    const bool haveTitleBg = tubes::loadImage(res, "TUBESBG.GFX", titleBg, 0);
    const bool haveTitleFg = tubes::loadImage(res, "TUBESFG.GFX", titleFg, 0);
    tubes::Image stars[tubes::kStarFrames + 1];
    bool haveStar[tubes::kStarFrames + 1] = {};
    for (int i = 1; i <= tubes::kStarFrames; ++i) {
        haveStar[i] = tubes::loadImage(res, "STAR" + std::to_string(i) + ".GFX",
                                stars[i], 0);
    }

    // Every harness entry point - a screenshot, a scripted run, a recorded
    // demo, a captured state, an explicit wave - must stay deterministic, so
    // only interactive play gets a clock seed. It also must not write to the
    // player's game directory; see `saveHiScores`.
    const bool harness = !opt.screenshot.empty() || opt.autoFrames > 0 ||
                         opt.demo || opt.playDemo || !opt.renderState.empty() ||
                         opt.wave > 0;

    // `1b2e:0243`: read `TUBES.HSC` if it is there, otherwise fill both banks
    // with the twenty names the binary ships. The file lives beside the game
    // data, which is where the original writes it.
    // The port's own settings - the toggles and the six bindings. Not in the
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

    // Which edition to be, resolved once and before anything opens a file -
    // because the edition names the save and high-score files. See
    // `edition.h`.
    //
    // Three sources, in this order:
    //
    //   1. a `--shareware` / `--preview` flag, which wins and does not
    //      persist;
    //   2. the settings file, if the question has been answered before;
    //   3. the player, asked once - `tubes::runEditionPrompt`.
    //
    // The prompt is skipped under `harness` along with every other timed
    // screen. That is not a convenience: a capture script that stopped on a
    // question would hang, and `writeSettings` already refuses to write under
    // the harness, so an answer given there could not be remembered anyway.
    if (opt.editionFromFlag) {
        // Nothing to store and nothing to ask. The flag is an override, so it
        // deliberately leaves `editionChosen` alone - running `--shareware`
        // once must not answer the question on the player's behalf.
    } else if (settings.editionChosen) {
        opt.edition.edition = settings.edition;
    }
    // The third source, the prompt, needs a renderer and so cannot run here.
    // It is below, and everything that depends on the edition - the two
    // filenames - is below it.
    //
    // Shown on every interactive start, not only the first - the player's
    // call, and the wording follows from that: this is "which would you like
    // to play?", not "which do you own?". Both editions are on archive.org
    // now, so owning one is no longer the question, and a launcher choice is
    // a fair thing to ask every time as long as it costs one keypress.
    //
    // It is still skipped for a flag and under `harness`, for the same reasons
    // as before: a capture that stopped on a question would hang.
    const bool mustAskEdition =
        opt.editionPrompt >= 0 || (!opt.editionFromFlag && !harness);

    // One controller, the first one plugged in. Opened below, once SDL is
    // actually up - this used to enumerate here, which is before `SDL_Init`,
    // so `SDL_NumJoysticks` was asked on an uninitialised library and always
    // said zero. No pad was ever opened at startup; the only way to get one
    // was to unplug it and plug it back in. Reported from play.
    SDL_GameController* gamepad = nullptr;

    // `1b2e:000a`: read `TUBES.SAV` if it is there. Unlike the high score
    // table the game ships one, zero-filled, and the reader zero-fills the
    // banks before reading anyway - so a missing or malformed file is simply
    // five empty slots per bank rather than an error.

    tubes::Image blackboard;
    const bool haveBlackboard = tubes::loadImage(res, "BLACKBRD.GFX", blackboard, -1);

    // The slide's four corner clips, in the order `1b2e:097a` places them:
    // upper-left, upper-right, lower-left, lower-right. Each is 20 bytes - a
    // header plus 4 x 4 - which pins them to the `2321:0711(4, 4, ...)` calls.
    // Index 0 is transparent, since `2321:0711` is a masked blit.
    tubes::Image slideCorner[4];
    bool haveCorner[4] = {false, false, false, false};
    for (int i = 0; i < 4; ++i) {
        haveCorner[i] = tubes::loadImage(res, tubes::kCornerNames[i], slideCorner[i], 0);
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
            tubes::loadImage(res, tubes::kPointerNames[i], pointerFrame[i], i ? -1 : 0);
    }
    tubes::Image booksArt, slideBar;
    const bool haveBooks = tubes::loadImage(res, "BOOKS.GFX", booksArt, 0);
    const bool haveBar = tubes::loadImage(res, "SLIDEBAR.GFX", slideBar, 0);
    // The five mouths, masked. `1b2e:0cd1` draws them through `2000:3921`,
    // the same thunk `1b2e:0510` uses for `POINTER0` and not the `2000:389d`
    // the wave frames are stamped with - so index 0 is transparent. Loading
    // them opaque left three black columns beside his chin, because the 12 x 8
    // the call passes is wider than the mouth in the art. Two call sites agree
    // on which thunk is which, and a capture showed it.
    tubes::Image talkFrame[5];
    bool haveTalk[5] = {false, false, false, false, false};
    for (int i = 0; i < 5; ++i) {
        haveTalk[i] = tubes::loadImage(res, tubes::kTalkNames[i], talkFrame[i], 0);
    }
    // `1b2e:084e`'s joke slide. Both go down through `2321:068d`, the opaque
    // blit - `FLASH.GFX` because it is a whole 172 x 132 transparency and
    // covers the slide exactly, and `POINTERT.GFX` because its 28 x 21
    // carries a patch of blackboard green behind the head it replaces.
    tubes::Image flashArt, pointerTArt;
    const bool haveFlash = tubes::loadImage(res, "FLASH.GFX", flashArt, -1);
    const bool havePointerT = tubes::loadImage(res, "POINTERT.GFX", pointerTArt, -1);
    // The ending's hop, masked - `1b2e:0b8f` draws through `2000:3921`.
    tubes::Image jumpFrameArt[tubes::kJumpFrames];
    bool haveJump[tubes::kJumpFrames] = {false, false, false};
    for (int i = 0; i < tubes::kJumpFrames; ++i) {
        haveJump[i] = tubes::loadImage(res, tubes::kJumpNames[i], jumpFrameArt[i], 0);
    }
    tubes::Image prizeArt;
    const bool havePrize = tubes::loadImage(res, tubes::kPrizeArt, prizeArt, 0);

    tubes::SceneArt sceneArt;
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
    bool haveFg = tubes::loadImage(res, "GAMEFG.GFX", foreground, 0);

    // One table, indexed by a beaker cell's raw value. The original's is at
    // DS:0x1da6 and is indexed by `type + 19 * fadeFrame`, so the same lookup
    // serves a settled atom and a fading one and the drawing code never
    // branches on whether a cell is clearing. Frames past 6 are null and draw
    // nothing, matching the original.
    tubes::Sprite atoms[tubes::kCellStates];
    bool haveAtom[tubes::kCellStates] = {};
    int loaded = 0;
    int drawable = 0;
    for (int i = 0; i < tubes::kTypeCount; ++i) {
        if (!kAtomSprites[i]) continue;
        ++drawable;
        if (tubes::loadSprite(res, kAtomSprites[i], atoms[i])) {
            haveAtom[i] = true;
            ++loaded;
        }
    }
    int fadesLoaded = 0;
    for (int frame = 1; frame <= tubes::kFadeFrames; ++frame) {
        for (int type = 0; type < tubes::kTypeCount; ++type) {
            if (!kFadeFamilies[type]) continue;
            const int slot = type + tubes::kFadeStride * frame;
            if (slot >= tubes::kCellStates) continue;
            const std::string name =
                std::string(kFadeFamilies[type]) + std::to_string(frame) + ".CSP";
            if (tubes::loadSprite(res, name, atoms[slot])) {
                haveAtom[slot] = true;
                ++fadesLoaded;
            }
        }
    }
    std::printf("loaded %d/66 fade sprites\n", fadesLoaded);
    // TESTUBE1/2/3 are the tipping animation's three frames, indexed by the
    // tube's phase - upright, tilted, pouring. Their heights of 65/42/27 are
    // a tube going over, which is what they were for all along; the port read
    // them as three difficulty capacities for several sessions, but the flat
    // capacity of five already contradicted that without explaining why.
    tubes::Sprite testTube[tubes::tubephase::kRelease + 1];
    bool haveTube[tubes::tubephase::kRelease + 1] = {false, false, false, false,
                                                     false};
    haveTube[tubes::tubephase::kUpright] =
        tubes::loadSprite(res, "TESTUBE1.CSP", testTube[tubes::tubephase::kUpright]);
    haveTube[tubes::tubephase::kTilted] =
        tubes::loadSprite(res, "TESTUBE2.CSP", testTube[tubes::tubephase::kTilted]);
    haveTube[tubes::tubephase::kPoured] =
        tubes::loadSprite(res, "TESTUBE3.CSP", testTube[tubes::tubephase::kPoured]);

    tubes::Sprite furn[tubes::kFurnCount];
    bool haveFurn[tubes::kFurnCount] = {};
    int furnLoaded = 0;
    for (int i = 0; i < tubes::kFurnCount; ++i) {
        haveFurn[i] = tubes::loadSprite(res, kFurnFiles[i], furn[i]);
        if (haveFurn[i]) ++furnLoaded;
    }
    std::printf("loaded %d/%d tube network sprites\n", furnLoaded,
                static_cast<int>(tubes::kFurnCount));

    // Indexed 1..7 by colour, so slot 0 stays empty the way the original's
    // table does.
    tubes::Sprite smallBall[8];
    bool haveSmallBall[8] = {};
    for (int i = 1; i <= 7; ++i) {
        haveSmallBall[i] = tubes::loadSprite(res, kSmallBallFiles[i - 1], smallBall[i]);
    }
    // The beaker's interior is exactly the grid: 106 x 65, with 4px walls, so
    // it sits 4px left of column 1 and level with row 1.
    tubes::Sprite beaker;
    const bool haveBeaker = tubes::loadSprite(res, "BEAKER.CSP", beaker);

    // The two fonts the game session swaps between, with the numbers
    // `1000:42ae` and `1000:42e8` pass to `2000:3fab`: the small one for the
    // labels and the pop-ups, the large one for every number.
    //
    // Which resources they are is settled, not guessed. The small one has to
    // be `TINY6X8.88` because it is the only 8 x 8 font in the archive and the
    // setup selects a cell height of 8. The large one was identified by
    // pulling the digit `0` out of a captured HUD and comparing it against all
    // four `.816` fonts: `FUTURE.816` matches byte for byte and the other
    // three are not close.
    tubes::Font bigFont, smallFont;
    const bool haveBig = loadFont(res, "FUTURE.816", 8, 7, bigFont);
    // The "big font" is a slot, `DS:0x2110`, not one font: each stage loads
    // what it wants into it. The HUD's is `FUTURE.816`, proven byte for byte
    // against a captured digit. The menu's and the briefing title's is
    // `STARTREK.816`, matched the same way against captures of each - 344 lit
    // pixels hit / 7 missed for `Start Game`, and 187 / 187 with 27 missed for
    // `Wave 1` at exactly the y the port already used.
    tubes::Font headingFont;
    const bool haveHeading = loadFont(res, "STARTREK.816", 8, 8, headingFont);
    const bool haveSmall = loadFont(res, "TINY6X8.88", 6, 4, smallFont);
    // The fourth `.816`, and the last slot to be identified. `1000:aaba` loads
    // it into `DS:0x2114`, and `1000:96db` selects that slot for the high
    // score list - so the table is written in `SCRIPT.816`, the cursive the
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
    // Demo Error") say the original treats a missing one as fatal there. A
    // failure here only costs the demo.
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

    // `1000:9e53` is the session: the menu leaves the title screen and the
    // session is entered fresh, with the difficulty the player chose. The
    // Game is owned rather than a local - starting a second game after a
    // Game Over has to build a new one, not reset the old one in place.
    // `harness` is computed above, where the high score table is loaded.
    std::unique_ptr<tubes::Game> game;
    auto newSession = [&](tubes::Difficulty diff, uint32_t seed,
                          const tubes::EditionState& ed) {
        game = std::make_unique<tubes::Game>(kCols, kRows, diff, seed);
        game->setFallHeight(kFallHeight);
        game->applyEdition(ed);
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
               opt.playDemo ? demo.seed : bootSeed, opt.edition);
    // `1000:9e53`'s new-game arm seeds the wave number from `DS:0x1d50` and
    // then loops brief-play-advance. Only the first half of that exists here:
    // there is no briefing screen and no stats blackboard, so the loop below
    // just steps to the next wave when one is cleared.
    // `--make-save`, a test rig rather than a feature. The progression is
    // why this is done in the engine instead of by hand: a record carries
    // `WaveProgress`, so a save with wave 75 and wave-1 counters is not a
    // wave 75 - it is the warp the reversing notes warn about. Stepping
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

    // GameController is not required: SDL_Init fails only on video, and a
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

    // `--scale` sizes the window for this run and is deliberately not written
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
    tubes::applyDisplayOptions(win, ren, settings.graphics);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         tubes::kScreenWidth,
                                         tubes::kScreenHeight);

    // The edition's third and last source: ask the player, once. It lives here
    // rather than beside the other two because it needs a renderer, and the
    // save and high-score files below need the answer - the edition names
    // them. That ordering is the whole reason this is a start-up question and
    // not a menu item; see PLAN.md, "Where the edition switch should live".
    if (mustAskEdition) {
        bool cancelled = false;
        // The remembered answer is the row the cursor starts on, so the common
        // case is Enter and nothing else. `editionChosen` is false only before
        // the first answer, and then the default is registered - which is both
        // the larger edition and what this port has always defaulted to.
        const int startOn =
            opt.editionPrompt >= 0
                ? (opt.editionPrompt > 0 ? 1 : 0)
                : (settings.editionChosen
                       ? tubes::editionAnswerIndex(settings.edition)
                       : tubes::editionAnswerIndex(tubes::Edition::kRegistered));
        const tubes::Edition chosen = tubes::runEditionPrompt(
            ren, opt.editionPrompt >= 0 ? 0 : opt.fadeSteps, cancelled,
            opt.editionPrompt >= 0 ? opt.screenshot : std::string(), startOn);
        // Closing the window is not an answer. Defaulting to registered and
        // carrying on would file this player's saves under a choice they never
        // made, and the point of the question is that the port cannot work it
        // out for itself.
        if (cancelled) {
            if (tex) SDL_DestroyTexture(tex);
            SDL_DestroyRenderer(ren);
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 0;
        }
        opt.edition.edition = chosen;
        settings.edition = chosen;
        settings.editionChosen = true;
        writeSettings();
    }

    // The name is edition-dependent and the port's own rule - `TUBES.SAV`
    // registered, `TUBESSW.SAV` shareware. The two files have identical
    // formats, so a cross-load would be silent; distinct names make it
    // impossible instead of detectable. `edition.h` carries the reasoning.
    //
    // `opt.edition` is right here and `game->edition()` would not be: this
    // runs before any session exists, and it is the edition that picks the
    // file, not the Preview flag - a Preview run writes nothing at all.
    const std::string savePath =
        opt.gameDir + "/" + opt.edition.saveFileName();
    tubes::SaveFile saves;
    {
        std::ifstream sf(savePath, std::ios::binary);
        if (sf) {
            std::vector<uint8_t> raw((std::istreambuf_iterator<char>(sf)),
                                      std::istreambuf_iterator<char>());
            if (!tubes::decodeSaves(raw, saves)) {
                std::fprintf(stderr,
                             "%s is malformed (%zu bytes); starting "
                             "with no saved games\n",
                             opt.edition.saveFileName(), raw.size());
                saves = tubes::SaveFile{};
            }
        }
    }

    const std::string hiScorePath =
        opt.gameDir + "/" + opt.edition.hiScoreFileName();
    tubes::HiScoreFile hiScores = tubes::defaultHiScores();
    {
        std::ifstream hf(hiScorePath, std::ios::binary);
        if (hf) {
            std::vector<uint8_t> raw((std::istreambuf_iterator<char>(hf)),
                                      std::istreambuf_iterator<char>());
            if (!tubes::decodeHiScores(raw, hiScores)) {
                std::fprintf(stderr,
                             "%s is malformed (%zu bytes); using the "
                             "shipped table\n",
                             opt.edition.hiScoreFileName(), raw.size());
                hiScores = tubes::defaultHiScores();
            }
        }
    }
    auto saveHiScores = [&]() {
        // A harness run must not write to the game directory. `--auto-advance`
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
    // Declaration order matters. The audio callback holds a bare pointer into
    // `sounds` while a voice is playing, and locals are destroyed in reverse,
    // so `sounds` has to be declared first - then `music` closes the device in
    // its destructor while the samples are still alive. The other way round is
    // a use-after-free on the audio thread on the way out.
    tubes::Sound sounds[tubes::sfx::kCount];
    // `CLAP.SFX` is not in the atom-indexed table - that table is keyed by
    // atom type - so it is loaded on its own for the high score viewer.
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
    // The device is opened here but nothing is played yet. `TUBES.MUS` is the
    // title screen's song - `1b2e:5238` loads it as one of that stage's own
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
        tubes::SkipWatch skip;
        const bool capturing = opt.splashFrame >= 0 || opt.splash2Step >= 0 ||
                               opt.cutscenePage >= 0;
        int k = 0;
        if (opt.splash2Step < 0) {
            k = tubes::runSoftwareCreationsSplash(
                res, ren, tex, screen, rgba,
                capturing ? 0 : opt.fadeSteps, skip, opt.splashFrame,
                opt.screenshot);
        }
        // `1b2e:11b0`: the second splash runs only if the first was not
        // skipped. One press gets past both, which is the original's design.
        if ((k != 1 && k != 2 && !capturing) || opt.splash2Step >= 0) {
            // `musicOn` / `soundOn` proper are declared with the frame loop;
            // the splash predates them, so it reads the same two settings.
            k = tubes::runAbsoluteMagicSplash(res, ren, tex, screen, rgba,
                                       capturing ? 0 : opt.fadeSteps, skip,
                                       music,
                                       !opt.music.empty() && settings.music,
                                       settings.sound, opt.splash2Step,
                                       opt.screenshot);
        }

        // `1000:b224`: the cutscene runs here - once, after the splashes and
        // immediately before the title screen is first shown. The main loop's
        // own `JMP 1000:b236` goes back to the title call and not to this, so
        // it is a boot-time screen and not part of the cycle.
        //
        // It does not depend on the splashes. `1b2e:11b0` is a `void`
        // procedure: it consumes each splash's return code to decide whether
        // to run the second splash and then throws it away, so `1000:b224` has
        // nothing to test. Both this call and the splash call at `1000:ac21`
        // are gated on one thing and it is the same thing - `2000:70fa`, which
        // reads the command tail's length out of the PSP at `ES:[0x80]` and is
        // `ParamCount`. Starting the game with any argument skips both.
        //
        // The port gated the cutscene on the first splash's key, so Enter on a
        // splash dropped the player straight to the menu. Reported from play.
        //
        // One departure, and it is the player's, agreed before it was written:
        // ESC on a splash skips the cutscene as well. The original runs the
        // cutscene whichever key ended the splash, and there is no way to say
        // "I have seen the intro" without also sitting through it. ESC already
        // means "leave this whole thing" inside the cutscene (`1b2e:112a`
        // returns 2 for it) and Enter already means "next", so this only
        // extends the two keys the screen after it already uses. Enter and
        // Space are unchanged and faithful: they get past the splashes and
        // into the cutscene.
        const bool escaped = (k == 2);
        if ((!escaped && !capturing) || opt.cutscenePage >= 0) {
            tubes::runCutscene(res, ren, tex, screen, rgba, palRaw, pal,
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
    // same atom rising at x=246 is not, because GAMEFG is transparent inside
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
    // The title screen is the one place the two editions' menus differ, and
    // they differ by two inserted items - see `SharewareMainItem`.
    menu.setEdition(opt.edition.edition);
    // `1b2e:5427` onward: the slot pages are built from the file every time
    // the title screen is entered, so a game saved this session shows up
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
        // Navigate there the way a player would, rather than setting the
        // page directly - so a capture can only show a page the menu really
        // reaches.
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
        tubes::presentScreen(ren, tex, screen, shownPal, rgba);
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
    // and 23 rows of an 80 x 25 screen. Present in the registered archive too,
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
        tubes::runExitScreen(win, ren, exitBanner, fadeSteps, opt.screenshot);
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
    // the palette fade, and it is not `StopMusic` (`[0x22da]`, which is used
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
            tubes::saveBmp(rgba, opt.screenshot);
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
    // the one about to start, not the briefing being shown, which never blits
    // it. `--gamebg` pins it so a capture can be matched.
    int lastBackdrop = tubes::kNoLastBackdrop;      // DS:0x2056, seeded 0xff
    const bool backdropPinned = opt.gameBg != "GAMEBG1.GFX";
    auto rollBackdrop = [&]() {
        if (backdropPinned) return;
        int n;
        // Edition state is read from the session, not the CLI option, because
        // the Preview flag is set by the menu arm and `--preview` is only one
        // way to reach it.
        const tubes::EditionState& ed = game->edition();
        if (ed.preview) {
            // The preview does not roll at all: `1000:7fc7` loads a fixed
            // background per wave, which is what the five `GAMEBG` literals in
            // the shareware image are and why it names them beside the
            // constructed prefix. Waves outside 1..5 cannot happen here - the
            // preview list is five arms - but fall back to the roll rather
            // than indexing off the end.
            const int w = game->progress().wave;
            n = (w >= 1 && w <= tubes::kPreviewWaveCount) ? tubes::kPreviewBackdrop[w] : 1;
        } else {
            // `1000:86b8` registered, `1000:7fc7` shareware. The two builds
            // differ by one operand - `PUSH 0xa` against `PUSH 0x5` - so this
            // is one call with an edition-dependent bound, not two code paths.
            const int bound = ed.backdropCount();
            n = lastBackdrop;
            while (n == lastBackdrop) n = game->rollForTest(bound) + 1;
        }
        lastBackdrop = n;
        tubes::Image next;
        if (tubes::loadImage(res, "GAMEBG" + std::to_string(n) + ".GFX", next, -1)) {
            background = std::move(next);
            haveBg = true;
        }
    };

    // `1b2e:0510` builds the classroom from nothing, and the projector screen
    // rolls down while it does. Which screens take that path is not a guess:
    // `1000:86b8` reads `if (wave = 1) and not replay then 1b2e:0510 else
    // 1b2e:0656`, so a briefing rolls the screen only at the top of a session
    // and never after a Continue; `1b2e:2d63` and `1b2e:411b` call it
    // unconditionally, so the Instructions and the Credits roll every time.
    tubes::ScreenRoll screenRoll;

    // The professor's idle. Every screen that waits runs two waits in order -
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
    // only F5 releases it and the simulation does not advance.
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
    bool instrOpen = opt.instr >= 0 || opt.credits || opt.ordering >= 0 ||
                     opt.registration;
    // `--instructions` / `--credits` open the screen the way the menu does,
    // roll-down and all, so a capture of the animation needs no other flag.
    if (instrOpen) {
        screenRoll.restart();
        profIdle.restart(tubes::kTalkBurstsSlide, sceneRng);
        if (opt.joke) joke.phase = 1;   // harness: show it without the roll
        else joke.maybeStart(sceneRng);
    }
    int instrSlide = opt.ordering > 0 ? opt.ordering
                                     : (opt.instr > 0 ? opt.instr : 0);
    // The Credits, `1b2e:411b`: the same screen with a different table.
    // Which of the three decks is up. They differ only in their page table -
    // Instructions `1b2e:2d63`, Credits `1b2e:411b`, and the shareware's
    // Ordering Info `1ac3:4889` - so the screen is one code path with a
    // different table rather than three screens.
    const tubes::InstructionSlide* instrPages = tubes::kInstructionSlides;
    int instrPageCount = tubes::kInstructionSlideCount;
    bool instrNav = true;
    auto openDeck = [&](const tubes::InstructionSlide* pages, int count,
                        bool nav = true) {
        instrPages = pages;
        instrPageCount = count;
        instrNav = nav;
        instrSlide = 0;
    };
    if (opt.credits) { instrPages = tubes::kCreditPages;
                       instrPageCount = tubes::kCreditPageCount; }
    if (opt.ordering >= 0) { instrPages = tubes::kOrderingPages;
                             instrPageCount = tubes::kOrderingPageCount; }
    if (opt.registration) { instrPages = tubes::kRegistrationPages;
                            instrPageCount = tubes::kRegistrationPageCount;
                            instrNav = false; }
    // `1000:ac01`: the shareware's Exit does not exit. It runs the Ordering
    // Info deck first and only then Halts, at which point the exit banner is
    // dumped over the text screen. So a quit that has been asked for waits for
    // the deck to finish; it is an ordinary screen in the frame loop.
    bool quitAfterOrdering = false;
    int rebindRow = 0;                 // 0..5, the control being pointed at
    bool rebindWaiting = false;        // armed, waiting for the press
    bool graphicsOpen = opt.graphics;  // the port's display options
    int graphicsRow = 0;               // 0..kGraphicsRows-1

    // `1b2e:0a11`'s slide drop, gated on `DS:0x210e`. The flag is cleared in
    // exactly one place - `entry`, at `1000:b1c6` - and set by `1b2e:0a11`
    // itself at `1b2e:0a44`, so the drop plays on the first classroom scene
    // of the program run and never again, whichever screen that happens to
    // be. It is a program-lifetime flag, not a per-briefing one.
    //
    // The port used to arm it from the briefing alone, which is right only
    // when a briefing is what the player reaches first. It is not the only
    // caller: the Instructions `CALL 1b2e:0a11` twenty-one times, once a
    // slide, and the Credits four - counted in the disassembly, not assumed -
    // so opening either from a cold start is the first call, and the port
    // showed a slide already at rest. Reported from play.
    bool slideDropped = false;
    int slideFrame = 0;          // index into kSlideDrop while dropping
    float slideAccum = 0.0f;
    auto slideIsDropping = [&]() {
        return !slideDropped && slideFrame < tubes::kSlideDropFrames;
    };
    auto slidePos = [&]() {
        if (!slideIsDropping()) {
            return tubes::SlideFrame{tubes::kSlideX, tubes::kSlideY};
        }
        return tubes::kSlideDrop[slideFrame];
    };
    // Everything about the classroom that moves, gathered once a frame. The
    // slide's own position is in here rather than being a caller's business,
    // because `1b2e:0a11` is the same routine on every screen that shows it.
    auto scenePose = [&]() {
        tubes::ScenePose p;
        p.frameH = screenRoll.height();
        p.slideDropping = slideIsDropping();
        p.profFrame = tubes::pointerFrameFor(profIdle.wave);
        // `1b2e:084e` is a blocking routine called from inside `1b2e:0a11`,
        // which itself runs before the key wait - so while the gag is up the
        // professor is not talking, and no mouth is stamped over the face it
        // replaces.
        p.mouthFrame = joke.active() ? 0 : profIdle.mouthFrame();
        p.jokeSlide = joke.showFlash();
        p.jokeFace = joke.showFace();
        const tubes::SlideFrame s = slidePos();
        p.slideX = s.x;
        p.slideY = s.y;
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
    // `1000:a6c1` runs it when the wave loop falls out and the session was not
    // aborted, the mode is not attract, and `DS:0x1d4b` is clear. The score is
    // then offered to the bank for the mode just played.
    tubes::NameEntry hs;
    hs.active = opt.hsEntry;
    hs.row = opt.hsEntry ? 3 : 0;

    // The F2 save screen, `1000:2dd0`'s save arm. It blocks the frame loop the
    // same way Pause does - the original calls it from inside the loop body
    // and does not come back until it is done.
    // The F1 help overlay, `1000:2e1c`. Blocks the loop like the save screen
    // and Pause do, and leaves on any key at all.
    bool helpScreen = opt.f1;

    tubes::SaveScreen saveUi;
    saveUi.open = opt.f2;

    // The standalone viewer the menu opens, `1b2e:61b6` - now decompiled, so
    // the layout is the original's rather than borrowed. Page 0 is Endurance
    // and page 1 is Wave; the original pre-renders both onto the two video
    // pages and flips between them, and redrawing gives the same picture.
    // `1000:b268`: View Demo and the attract timeout run the same thing - a
    // session in mode 0 at difficulty 2, replaying DEMO.SCR. This flag is what
    // tells the frame loop to feed it the recording instead of the keyboard.
    bool attractDemo = false;

    tubes::HiScoreViewer hsView;
    hsView.viewing = opt.hsPage >= 0;
    hsView.page = opt.hsPage > 0 ? 1 : 0;

    // Every route out of a session goes through here, so the offer cannot be
    // skipped on one path and taken on another.
    auto endSession = [&]() {
        hs.bank = (gameMode == 1) ? tubes::HiScoreBank::kEndurance
                                 : tubes::HiScoreBank::kWave;
        const uint32_t sc = static_cast<uint32_t>(game->score());
        if (!flags.aborted && gameMode != 0 && game->edition().canEnterHiScore() &&
            tubes::qualifies(hiScores[hs.bank], sc)) {
            // Seeded with the sentinel and typed over, exactly as the original
            // does - which is why the sentinel's tail survives in the file.
            hs.row = tubes::insertHiScore(hiScores[hs.bank], "", sc) + 1;
            hs.name.clear();
            hs.cursor = tubes::kHsCursorMin;
            hs.cursorDir = 1;
            hs.hold = 0.0f;
            hs.active = true;
            music.stop();
            changeScreen();
            return;
        }
        changeScreen();
        stage = Stage::kTitle;
        // `1000:b2ac`: the attract arm clears `DS:0x1d42` on the way back, so
        // the demo returns to a bare title screen with the menu down. Every
        // other route leaves the menu up, which is what `1b2e:52bf`'s own
        // flag does when it is not cleared.
        if (gameMode == 0) attractDemo = false;
        else menu.raise();
        playSong("TUBES.MUS");
    };

    // `1b2e:0b8f` returning ends a page; the second one ends the ending, and
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
        // `1000:abf7` arm 7 (View Demo) and arm 11 (attract timeout) both set
        // the Preview flag in the shareware build, restoring the registered
        // special-atom rates so the single byte-identical DEMO.SCR stays in
        // sync. The registered build has no Preview path, so its demo runs as
        // registered.
        newSession(kDemoDifficulty, demo.seed,
                   opt.edition.edition == tubes::Edition::kShareware
                       ? tubes::EditionState{tubes::Edition::kShareware, true}
                       : tubes::EditionState{tubes::Edition::kRegistered, false});
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
    // also what resets the per-wave count, in the game as well as on the
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
        // Harness only: press Return periodically so a headless run walks
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
                                  !saveUi.open && !hs.active &&
                                  !paused && !helpScreen;
            SDL_Keycode k = SDLK_UNKNOWN;
            // Whether the input driver would have claimed this - see
            // `bindsKey` in `input.h`. Only the help overlay cares, but it has
            // to be worked out here, where the scancode and the pad button are
            // still in hand.
            bool boundControl = false;
            if (ev.type == SDL_KEYDOWN) {
                k = ev.key.keysym.sym;
                boundControl =
                    tubes::bindsKey(settings.bindings, ev.key.keysym.scancode);
            } else if (ev.type == SDL_CONTROLLERBUTTONDOWN && !livePlay) {
                k = menuKeyForPad(settings.bindings, ev.cbutton.button);
                boundControl =
                    tubes::bindsPad(settings.bindings, ev.cbutton.button);
            }
            if (k == SDLK_UNKNOWN) continue;

            // `1000:9744`'s typing loop. It owns the keyboard entirely while
            // it is up: printable characters append, backspace removes, and
            // Esc or Return finish - nothing else is looked at.
            if (hs.active) {
                if (hs.hold > 0.0f) continue;   // the applause owns the screen
                if (k == SDLK_RETURN || k == SDLK_ESCAPE) {
                    // `1000:96db`'s tail: the row is redrawn in the settled
                    // colour with no cursor, the applause plays, and the
                    // screen holds for `23e7:0024(0x78)` - 120 retraces,
                    // 1.71 s - before the record is committed and the file
                    // written. The port committed and left in the same frame.
                    if (soundOn && haveClapSound) music.playSound(&clapSound);
                    hs.hold = tubes::kHsCommitSeconds;
                } else if (k == SDLK_BACKSPACE) {
                    if (!hs.name.empty()) hs.name.pop_back();
                } else if (k >= 0x20 && k <= 0x7e &&
                           static_cast<int>(hs.name.size()) <
                               tubes::kHiScoreNameMax) {
                    // `1000:9718` gates on 0x20..0x7e and a length under 25.
                    const bool shift =
                        (SDL_GetModState() & KMOD_SHIFT) != 0;
                    char c = static_cast<char>(k);
                    if (shift && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
                    hs.name.push_back(c);
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

            // `1b2e:0c78` is the paging wait, and it maps a six-button input
            // byte - it does not react to "any key". Decompiled:
            //
            //     driver 0x01 Up   -> 5     scancode 0xc8/0xc9 Up/PgUp -> 5
            //     driver 0x02 Down -> 4     scancode 0xd0/0xd1 Dn/PgDn -> 4
            //     driver 0x10 A    -> 1     Enter or Space             -> 1
            //     driver 0x20 B    -> 2     Esc                        -> 2
            //                                 timeout                  -> 3
            //
            // and the deck then does: 2 leaves, 5 goes back, 1 and 4 advance.
            // Left and Right produce no code at all, so the wait simply
            // keeps waiting and the original ignores them.
            //
            // The port used to advance on anything that was not Esc or Up,
            // which is that six-button byte over-generalised to a whole
            // keyboard - so Left, reached for as "go back", turned the page
            // forward. Reported from play, and it was the port's bug rather
            // than the original's awkwardness.
            //
            // The pad still works: `menuKeyForPad` already translates its A to
            // Return, its B to Escape and its Up/Down to the arrows, all of
            // which are handled below - and its Left and Right now correctly
            // do nothing, which is what the original's driver byte does.
            //
            // The ordering deck's own last page says as much in the game's
            // words: "Press Button A, Button B, Enter, or Space to exit." On
            // the final page code 1 advances past the end and leaves, and code
            // 2 leaves outright, so all four do exit.
            if (instrOpen) {
                const bool leave = k == SDLK_ESCAPE;               // code 2
                const bool prev = k == SDLK_UP || k == SDLK_PAGEUP;   // code 5
                const bool next = k == SDLK_DOWN || k == SDLK_PAGEDOWN ||
                                  k == SDLK_RETURN || k == SDLK_KP_ENTER ||
                                  k == SDLK_SPACE;                 // codes 4, 1
                if (!leave && !prev && !next) continue;   // the wait ignores it
                if (leave) {
                    instrOpen = false;
                } else if (prev) {
                    if (instrSlide > 0) --instrSlide;
                } else if (++instrSlide >= instrPageCount) {
                    instrOpen = false;
                }
                // Each slide is its own `1b2e:0cd1(35)` / `1b2e:0e37(30)`
                // pair, so turning the page starts him talking again.
                if (instrOpen) {
                    profIdle.restart(tubes::kTalkBurstsSlide, sceneRng);
                    joke.maybeStart(sceneRng);
                }
                // Only leaving fades. Moving between slides does not - the
                // original changes the slide inside one screen function and
                // its fade-out is at the very end, on the way back.
                if (!instrOpen) {
                    if (quitAfterOrdering) {
                        // `1000:ac06` onward: the deck has returned, so the
                        // program ends. Music first - it is going down with
                        // everything else on the Halt - then the fade, then
                        // the banner the shell would have been left holding.
                        music.stop();
                        runFade(false);
                        if (exitBanner.rowsLoaded() > 0) {
                            tubes::runExitScreen(win, ren, exitBanner, fadeSteps,
                                          std::string());
                        }
                        running = false;
                        continue;
                    }
                    changeScreen();
                }
                continue;
            }

            // The rebinding screen owns the keyboard while it is up. When it
            // is armed the next press is the binding, Esc included - there is
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
            // applied and saved at once - the point of a display option is
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
                    tubes::applyDisplayOptions(win, ren, settings.graphics);
                    writeSettings();
                } else if (k == SDLK_ESCAPE) {
                    graphicsOpen = false;
                    changeScreen();
                }
                continue;
            }

            // `1b2e:6423`. The first page treats Esc specially - it leaves at
            // once - and any other key advances to the second. On the second
            // page every key leaves, Esc included.
            if (hsView.viewing) {
                if (hsView.page == 0 && k != SDLK_ESCAPE) {
                    hsView.page = 1;
                    hsView.timer = tubes::kHsViewSeconds;
                } else {
                    hsView.viewing = false;
                    playSong("TUBES.MUS");
                    changeScreen();
                }
                continue;
            }

            if (stage == Stage::kTitle) {
                // Every accepted press resets the attract countdown.
                attractTimer = tubes::kAttractTimeout;
                if (!menu.up()) {
                    // The two-key protocol, `DS:0x1d42`: Esc, Space or Return
                    // raises the menu and the press is swallowed, so it cannot
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
                // belongs to this screen. It plays on selecting an item, not
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
                        newSession(kDiff[c.difficulty], bootSeed ^ 0x5bf03635u,
                                   opt.edition);
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
                                   bootSeed ^ 0x5bf03635u, opt.edition);
                        gameMode = c.mode;
                        flags = tubes::SessionFlags{};
                        totals = tubes::SessionTotals{};
                        banner = tubes::Banner::kNone;
                        paused = false;
                        briefingUp = false;
                        game->loadFrom(rec, totals);
                        if (c.mode == 2) {
                            // A save resumes at the start of its wave, so the
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
                        openDeck(tubes::kInstructionSlides,
                                 tubes::kInstructionSlideCount);
                        instrSlide = 0;
                        profIdle.restart(tubes::kTalkBurstsSlide, sceneRng);
                        joke.maybeStart(sceneRng);
                        // `1b2e:2d63` builds the scene with `1b2e:0510`, so
                        // the projector screen comes down every time - no
                        // `DS:0x210e`-style gate on this one.
                        screenRoll.restart();
                        changeScreen();
                        break;
                    case tubes::MenuResult::kOrdering:
                        // `1000:abad`. The same deck the Exit path runs, but
                        // reached from the menu, so it comes back to the title
                        // instead of ending the program.
                        instrOpen = true;
                        openDeck(tubes::kOrderingPages,
                                 tubes::kOrderingPageCount);
                        profIdle.restart(tubes::kTalkBurstsSlide, sceneRng);
                        joke.maybeStart(sceneRng);
                        screenRoll.restart();
                        changeScreen();
                        break;
                    case tubes::MenuResult::kPreview: {
                        // `1000:ab52`: mode 2, difficulty 0, `DS:0x1d4c` = 1
                        // and the Preview flag on, then the ordinary session.
                        // It asks for neither mode nor difficulty, so this
                        // this does not route through the Game Mode page - the
                        // three stores are immediates in the menu arm.
                        static const tubes::Difficulty kDiff[3] = {
                            tubes::Difficulty::k101, tubes::Difficulty::k201,
                            tubes::Difficulty::k301};
                        const tubes::MenuChoice& c = menu.choice();
                        // `1000:ab52` sets mode 2 and difficulty 0 as
                        // immediates, which `Menu::select()` already stored in
                        // `c`, so the Preview runs those values just like a
                        // normal Start Game arm would.
                        newSession(kDiff[c.difficulty], bootSeed ^ 0x5bf03635u,
                                   {tubes::Edition::kShareware, true});
                        gameMode = c.mode;
                        flags = tubes::SessionFlags{};
                        totals = tubes::SessionTotals{};
                        banner = tubes::Banner::kNone;
                        paused = false;
                        briefingUp = false;
                        game->startWave();
                        raiseBriefing(false);
                        sstage = tubes::firstStage(gameMode);
                        playSong(tubes::kBriefingMusic);
                        stage = Stage::kPlay;
                        changeScreen();
                        break;
                    }
                    case tubes::MenuResult::kCredits:
                        // `1000:b280`. Same screen, same keys, four pages.
                        instrOpen = true;
                        openDeck(tubes::kCreditPages, tubes::kCreditPageCount);
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
                        // The port's own row and its own screen - input.h
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
                        // `1000:abf7`: the shareware's Exit is menu item 10,
                        // and it does not quit. It calls the Ordering Info
                        // deck and only then Halts, which is where the banner
                        // comes from. So the port opens the deck and defers
                        // the quit until it closes - see `quitAfterOrdering`.
                        //
                        // The registered build has no such path: its exit arm
                        // quits outright and its executable never names
                        // TUBESEND, so this is gated on the edition.
                        if (opt.edition.edition == tubes::Edition::kShareware) {
                            instrOpen = true;
                            openDeck(tubes::kOrderingPages,
                                     tubes::kOrderingPageCount);
                            quitAfterOrdering = true;
                            profIdle.restart(tubes::kTalkBurstsSlide, sceneRng);
                            joke.maybeStart(sceneRng);
                            screenRoll.restart();
                            changeScreen();
                            break;
                        }
                        running = false;
                        break;
                    case tubes::MenuResult::kHighScores:
                        // `1b2e:63c7`: the music becomes CLASS.MUS and the
                        // applause starts, on the Endurance page.
                        hsView.viewing = true;
                        hsView.page = 0;
                        hsView.timer = tubes::kHsViewSeconds;
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
            // `1000:2dd0` runs once a frame and only when `KeyPressed`, so it
            // belongs here rather than in the per-frame update. The keys it
            // recognises are ESC and F1..F5; everything else falls through to
            // the tube, which reads the keyboard separately.
            const uint8_t code = originalKeyCode(k);

            // The save screen owns the keyboard while it is up, just as the
            // typing loops do - `1000:2dd0` does not return until ESC or a
            // completed save. `1000:3382` is the navigation and `1000:3423`
            // the two keys that end it. `1000:301b` is a bare `ReadKey` with
            // nothing after it, so the help overlay leaves on any key that
            // reaches it and the key is discarded rather than re-dispatched.
            // F5 out of help does not pause and ESC out of help does not
            // abort, which is the whole difference between this wait and
            // Pause's `repeat until k = $bf`.
            //
            // "Any key" is not the whole rule, and the screen's own
            // `Press Any Key...` is what made that easy to get wrong: the
            // input driver has already eaten its six, so a control does
            // nothing here. Measured, not assumed - see `bindsKey`.
            if (helpScreen) {
                if (!boundControl) helpScreen = false;
                continue;
            }

            if (saveUi.open) {
                if (saveUi.written > 0.0f) continue;      // the written hold
                if (!saveUi.typing) {
                    if (k == SDLK_DOWN) {
                        // 1000:3393: five slots, and it wraps.
                        saveUi.slotSel = saveUi.slotSel == tubes::kSaveSlotsShown
                                          ? 1 : saveUi.slotSel + 1;
                    } else if (k == SDLK_UP) {
                        saveUi.slotSel = saveUi.slotSel == 1
                                          ? tubes::kSaveSlotsShown
                                          : saveUi.slotSel - 1;
                    } else if (k == SDLK_RETURN) {
                        // 1000:3448: the record is copied out and its
                        // description becomes the line being edited, so
                        // re-saving over a slot starts from what was there.
                        const tubes::SaveBank b =
                            gameMode == 1 ? tubes::SaveBank::kEndurance
                                          : tubes::SaveBank::kWave;
                        saveUi.desc = saves[b].slots[saveUi.slotSel - 1].description;
                        saveUi.typing = true;
                    } else if (k == SDLK_ESCAPE) {
                        saveUi.open = false;            // 1000:3435
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
                    saveUi.open = false;
                    saveUi.typing = false;
                } else if (k == SDLK_RETURN) {
                    const tubes::SaveBank b =
                        gameMode == 1 ? tubes::SaveBank::kEndurance
                                      : tubes::SaveBank::kWave;
                    tubes::SaveSlot& rec = saves[b].slots[saveUi.slotSel - 1];
                    // 1000:3654 onward: the description first, then the
                    // fourteen session fields, then the whole record into the
                    // bank and the file out.
                    rec.setDescription(saveUi.desc.empty()
                                           ? tubes::kSaveUndescribed
                                           : saveUi.desc);
                    game->saveInto(rec, totals);
                    // `1b2e:00ac` rolls two random numbers before writing, so
                    // saving perturbs the sequence - in the original too.
                    tubes::stampSaveNonces(
                        saves, game->rollForTest(tubes::kSaveNonceMax) + 1,
                        game->rollForTest(tubes::kSaveNonceMax) + 1);
                    writeSaves();
                    saveUi.typing = false;
                    saveUi.written = tubes::kSaveWrittenSeconds;
                } else if (k == SDLK_BACKSPACE) {
                    if (!saveUi.desc.empty()) saveUi.desc.pop_back();
                } else if (k >= 0x20 && k <= 0x7e &&
                           static_cast<int>(saveUi.desc.size()) <
                               tubes::kSaveDescTyped) {
                    const bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                    char c = static_cast<char>(k);
                    if (shift && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
                    saveUi.desc.push_back(c);
                }
                continue;
            }

            // F5 first: while paused the original is blocked inside
            // `repeat until ReadKey = $bf`, so nothing else is looked at and
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
                // `1000:5eec`: the abort arm draws its hint and then calls
                // `1000:2dd0` - the whole in-game key dispatch - a second
                // time, which is what makes the offer real. "F2 to Save Game,
                // ESC for Main Menu!" is not decoration: F2 there opens the
                // save screen, and it is the last chance to save a session
                // the player has just abandoned.
                //
                // The port dismissed the banner on any key at all, so the
                // hint pointed at nothing and the only way to save was to
                // remember F2 before aborting. Reported from play.
                if (banner == tubes::Banner::kAborted &&
                    bannerPhase == tubes::BannerPhase::kWait &&
                    code == tubes::gamekey::kF2 && gameMode != 0 &&
                    game->edition().canSave()) {
                    saveUi.open = true;
                    saveUi.typing = false;
                    saveUi.written = 0.0f;
                    if (saveUi.slotSel < 1 ||
                        saveUi.slotSel > tubes::kSaveSlotsShown) {
                        saveUi.slotSel = 1;
                    }
                    continue;
                }
                // `1000:5e0b` and `1000:5e78`: a key ends the wait, and only
                // the wait. The 40-retrace hold before it does not look at
                // input at all, and the outro after it is already committed -
                // which is what stops the tip keypress that ended the wave
                // from dismissing the banner it caused.
                if (bannerPhase == tubes::BannerPhase::kWait) leaveBannerWait();
                continue;

            case tubes::SessionStage::kStats: {
                const tubes::StageTransition t =
                    tubes::advanceStage(sstage, flags, gameMode);
            // `1000:a657` is two instructions sitting inside the progression
            // block, so it is reached only when the wave is being advanced -
            // which is what `t.advanceWave` means here:
                //
                //     if wave >= 75 then RegisteredEnding;
                //     wave := wave + 1
                //
                // The ending's own first act is to set the session's game-over
                // flag through the static link (`SS:[DI + 0xfe02] := 1`), so
                // the session is over the moment it is.
            // Edition-aware: 75 registered, 25 shareware, 5 in the Preview.
            // See `edition.h` - it is the same test at the same place in both
            // builds, with a different literal and a different destination.
                //
                // The destinations differ as well as the literals. Wave 25
                // in the shareware reaches `1000:8df8` - "You can't stop now!"
                // - where wave 75 in the registered build reaches the Nobel
                // ending at `1000:9499`. Neither image contains the other's
                // text, so the port shows each edition its own screen and
                // never substitutes one for the other.
                //
                // Read the edition from the session, not the CLI option:
                // Preview is set by the menu arm even when `--preview` was
                // not given on the command line.
                const tubes::EditionState& ed = game->edition();
                const bool sharewareEnd = ed.edition == tubes::Edition::kShareware;
                if (t.advanceWave &&
                    game->progress().wave >= ed.endingWave()) {
                    if (sharewareEnd) {
                        // One page, and `1000:8df8` waits with `1ac3:0b8f` -
                        // the terminal wait, not the paging one - so it is
                        // shown through the deck screen and any key leaves.
                        instrOpen = true;
                        openDeck(tubes::kRegistrationPages,
                                 tubes::kRegistrationPageCount, false);
                        profIdle.restart(tubes::kTalkBurstsSlide, sceneRng);
                        screenRoll.restart();
                        flags.gameOver = true;
                        stage = Stage::kTitle;
                        changeScreen();
                        continue;
                    }
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
            // exactly one place - `1000:b1e4`, which sets it to zero - and
            // read in three: this F2 gate, the abort banner's F2 hint, and the
            // high-score offer. So saving is always enabled in the shipped
            // build; the flag looks like a switch for an edition that never
            // came. The port hard-coded `true` here while the save screen did
            // not exist, which quietly made F2 dead once it did.
            switch (tubes::classifyGameKey(code, gameMode == 0,
                                           !game->edition().canSave())) {
            case tubes::GameAction::kAbort:
                flags.aborted = true;
                break;
            case tubes::GameAction::kPause:
                paused = true;
                music.setPaused(true);
                break;
            case tubes::GameAction::kMusicToggle:
                musicOn = !musicOn;
                // `1000:377a`: switching on restarts the current song, which
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
                saveUi.open = true;
                saveUi.typing = false;
                saveUi.written = 0.0f;
                if (saveUi.slotSel < 1 || saveUi.slotSel > tubes::kSaveSlotsShown) {
                    saveUi.slotSel = 1;
                }
                break;
            case tubes::GameAction::kHelp:
                // `1000:2e1c`. An overlay over the frame the loop left up,
                // dismissed by any key.
                helpScreen = true;
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
        // stage dispatch on purpose: the instructions and the credits are
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

        // `1b2e:0a11`'s slide drop: six frames, ten vertical retraces each,
        // and then done for the whole run. This sits outside the stage
        // dispatch for the same reason `DS:0x210e` is a program-lifetime flag
        // - the routine does not care which screen called it, and the briefing
        // is only one of five callers. Whichever classroom the player reaches
        // first is the one that drops the slide.
        //
        // It waits for the roll-down for the same reason the joke does:
        // `1b2e:0510` returns before its caller reaches `1b2e:0a11`, so there
        // is no slide to move while the screen is still coming down. The two
        // are consecutive, not concurrent.
        const bool classroomUp =
            briefingUp || instrOpen || endingPage > 0 ||
            sstage == tubes::SessionStage::kStats ||
            sstage == tubes::SessionStage::kContinue;
        if (classroomUp && !screenRoll.rolling() && !slideDropped) {
            slideAccum += dt * tubes::kRetraceHz;
            while (slideAccum >= tubes::kSlideDropRetraces) {
                slideAccum -= tubes::kSlideDropRetraces;
                if (++slideFrame >= tubes::kSlideDropFrames) {
                    slideDropped = true;
                    break;
                }
            }
        }

        // `--demo` drives the real loop with the scripted player, so the
        // render path gets exercised on every frame of a whole session rather
        // than only on the one frame `--auto` screenshots. That distinction
        // matters: a crash that needs both a full beaker and a live render is
        // invisible to `--auto`, which simulates first and draws once at the
        // end.
        if (stage == Stage::kTitle) {
            // The instructions, the credits and the rebinding screen all
            // borrow the classroom, so they borrow the professor's clock too -
            // `1b2e:0e37` steps him every ten retraces, and he waves his
            // pointer while any of the three waits for a key.
            //
            // This used to read `if (instrOpen)` around a copy of the render
            // section's slide draw, which ended the frame with its own
            // `continue` before the clock below could run - so the professor
            // stood still on the two screens the original animates him on, and
            // the render section's own block was unreachable. The draw is gone
            // from here; the clock is what belongs in the update.
            // `1b2e:52bf`'s loop body, at the title screen's own rate - see
            // kTitleHz. Everything in it is counted in frames: 4 px a frame
            // along a leg, a star frame every three, 720 frames to attract.
            titleAccum += dt * tubes::kTitleHz;
            int steps = static_cast<int>(titleAccum);
            titleAccum -= static_cast<float>(steps);
            if (steps > 8) steps = 8;      // a stall must not teleport the atom
            // The countdown belongs to the title screen and to nothing else.
            // The instructions, the credits, the high score viewer and the
            // rebinding screen are separate screens with waits of their own -
            // `1b2e:0e37(30)` for the slideshows, `kHsViewSeconds` for the
            // viewer - and the original's countdown is inside `1b2e:52bf`,
            // which is not running while any of them is up. The port ran it
            // regardless, so a player halfway through binding a key could be
            // dropped into the demo. Reported from play.
            const bool onTitleProper =
                !instrOpen && !rebindOpen && !graphicsOpen && !hsView.viewing;
            for (int k = 0; k < steps; ++k) {
                titleAtom.step();
                if (menu.up()) menu.tick();
                if (onTitleProper) --attractTimer;
            }
            if (onTitleProper && attractTimer <= 0) {
                attractTimer = tubes::kAttractTimeout;
                // `1000:b287`: the arm runs the blackboard cutscene first and
                // skips the demo entirely if it returns 2 - which is ESC.
                tubes::SkipWatch attractSkip;
                const int k = tubes::runCutscene(
                    res, ren, tex, screen, rgba, palRaw, pal, fadeSteps,
                    attractSkip, music, musicOn, soundOn,
                    haveBlackboard ? &blackboard : nullptr, haveBlackboard,
                    atoms, haveAtom, smallFont, haveSmall, -1, -1,
                    std::string());
                // The cutscene ends on black, so there is nothing to fade
                // out of - whatever comes next just fades in.
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
                // idle, not per frame: see Game::acceptsInput.
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
                       !saveUi.open && !helpScreen) {
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
            if (hs.active && hs.hold <= 0.0f) {
                hs.cursorAccum += dt * tubes::kRetraceHz;
                while (hs.cursorAccum >= 1.0f) {
                    hs.cursorAccum -= 1.0f;
                    hs.cursor += hs.cursorDir;
                    if (hs.cursor >= tubes::kHsCursorMax) hs.cursorDir = -1;
                    if (hs.cursor <= tubes::kHsCursorMin) hs.cursorDir = 1;
                }
            } else if (hs.active) {
                // The applause plays out, and only then is the record
                // committed and the file written.
                hs.hold -= dt;
                if (hs.hold <= 0.0f) {
                    hs.hold = 0.0f;
                    hiScores[hs.bank].rows[hs.row - 1].setName(hs.name);
                    saveHiScores();
                    hs.active = false;
                    stage = Stage::kTitle;
                    menu.raise();
                    playSong("TUBES.MUS");
                    changeScreen();
                }
            }

            // `1000:3722`: twenty retraces after the file is written, deaf,
            // and then the game resumes where it left off.
            if (saveUi.written > 0.0f) {
                saveUi.written -= dt;
                if (saveUi.written <= 0.0f) {
                    saveUi.written = 0.0f;
                    saveUi.open = false;
                    refreshSaveSlots();
                }
            }

            // `1b2e:0510`'s roll-down. It is armed by `raiseBriefing` only for
            // wave 1 of a session that is not a replay, which is the condition
            // `1000:86b8` itself tests before choosing between `1b2e:0510` and
            // `1b2e:0656`.
            if (briefingUp) screenRoll.tick(dt);

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
            // was taken and what it does not change.
            //
            // The sounds are taken either way, so F4 mutes without desyncing
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

            // The instructions, the credits, the high score viewer and the
            // rebinding screen each replace the title while they are up, so
            // each ends the frame with its own `continue` before the title is
            // drawn.
        if (instrOpen) {
            drawInstructionSlide(screen, instrPages, instrPageCount,
                                 instrSlide, instrNav, &blackboard, haveBlackboard,
                                 sceneArt, scenePose(),
                                 smallFont, haveSmall, headingFont,
                                 haveHeading, atoms, haveAtom, testTube,
                                 haveTube, furn, haveFurn);
            presentFrame();
            continue;
        }

        if (rebindOpen) {
            // `bindingLabel` is the one part of this screen that needs SDL, so
            // it stayed here and the finished strings go across instead.
            std::string bindLabels[tubes::kGameButtons];
            for (int i = 0; i < tubes::kGameButtons; ++i) {
                bindLabels[i] = bindingLabel(settings.bindings.b[i]);
            }
            drawRebindScreen(screen, bindLabels, rebindRow,
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
        if (hsView.viewing) {
            // `1b2e:63f2`: whenever the effects voice reports itself idle the
            // clap starts again, so the applause carries the whole screen.
            if (soundOn && haveClapSound && !music.soundBusy()) {
                music.playSound(&clapSound);
            }
            // The give-up does exactly what a key does.
            hsView.timer -= dt;
            if (hsView.timer <= 0.0f) {
                if (hsView.page == 0) {
                    hsView.page = 1;
                    hsView.timer = tubes::kHsViewSeconds;
                } else {
                    hsView.viewing = false;
                    playSong("TUBES.MUS");
                    changeScreen();
                }
            }
            const tubes::HiScoreBank viewBank =
                hsView.page == 0 ? tubes::HiScoreBank::kEndurance
                                : tubes::HiScoreBank::kWave;
            drawHiScoreViewer(screen, hiScores[viewBank],
                              tubes::kHsViewTitle[hsView.page], &blackboard,
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

        // Draw order transcribed from `1000:3a67`. The atoms go between the
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
        // snapshot needs no cases, because it is the artwork.
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
        // Without this a Flashium is invisible: `kAtomSprites[8]` is null, so
        // every draw site skipped it. It showed up as a phantom ball in the
        // test tube, because a Multiplier fills with `Random(8) + 1` and 8 is
        // in that range.
        auto ball = [&](int8_t cell) -> int8_t {
            return cell == tubes::kFlashium ? game->flashColour() : cell;
        };

        // `-0x189`, the hidden-atom modifier: "live through N atoms that are
        // hidden until they leave a tube". All six network draw sites carry
        //
        //     if hidden = 0 then Draw(ball[type], x, y)
        //                   else Draw(MYSTBALL,   x, y)
        //
        // and those six are the only places in `1000:3a67` that test it - not
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
        // one slot of the atom array - and the slot index is the column. So an
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
        // `GAMEFG.GFX` carries much of the network, but not all of it: the
        // vertical pieces through the lane rows are missing from it and come
        // from these passes. A previous version removed the passes on the
        // theory that `GAMEFG` was the complete network; the arcs came out
        // without their verticals. Both are needed.
        drawAtom(1); drawAtom(6);
        drawFurn(0, kFurnGroup0);
        drawAtom(2); drawAtom(5);
        drawFurn(kFurnGroup0, kFurnGroup1);
        drawAtom(3); drawAtom(4);
        drawFurn(kFurnGroup1, kFurnTotal);

        // The test tube is drawn twice per frame from two sprite pointers held
        // in its own record, at one position, with its contents in between:
        //
        //     Draw(tube.x, tube.y, tube.sprite[4])   if tube.phase = 1
        //     ... the atoms it is holding ...
        //     ... the HUD ...
        //     Draw(tube.x, tube.y, tube.sprite[phase])
        //
        // so the contents sit between the two layers of glass. The port had
        // this arrangement already; what is new is that it is now read from
        // the draw sequence rather than reasoned from "the beaker works this
        // way".
        //
        // `tube.x` comes straight from the six-stop table at `DGROUP:0x24` -
        // 104, 122, 140, 158, 176, 194, exactly the column x minus 3 - and
        // `tube.y` is the literal 0x44 the setup writes. The pixel diff's
        // apparent 6 px offset was the capture catching the tube mid-slide, at
        // a different stop from the one the state file named; there was never
        // an offset to sweep for.
        const int tubeX = game->tubeX();
        if (haveFurn[tubes::kTestTubeShadow]) {
            screen.draw(furn[tubes::kTestTubeShadow], tubeX, kTubeY);
        }

        // The tube's contents carry their own positions now. They used to be
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

        // Beaker shadow, then its contents, then the glass front last - the
        // original draws `BEAKER.CSP` after the settled atoms, so the glass
        // overlaps the balls. The port used to draw it first.
        if (haveFurn[tubes::kBeakerShadow]) screen.draw(furn[tubes::kBeakerShadow], 186, 135);

        // The beaker grid, then the marker overlay, in `1000:598b`'s order.
        // The cell is used as the sprite index directly - that is the whole
        // point of the `type + 19 * fadeFrame` encoding, and it is why a
        // clearing atom animates with no branch anywhere in the draw.
        const tubes::Board& b = game->board();
        for (int r = 0; r < b.rows(); ++r) {
            const int y = kGridY + r * kPitchY;
            for (int c = 0; c < b.cols(); ++c) {
                // Only the bare type 8 is substituted. A fading Flashium
                // holds `8 + 19 * frame`, whose table slot is `FFADE` and is
                // never rewritten.
                const tubes::Cell v = ball(b.at(c, r));
                if (v == 0 || !haveAtom[v]) continue;
                screen.draw(atoms[v], kGridX + c * kPitchX, y);
            }
        }
        // Plane C, drawn over a flagged cell at (x + 2, y + 1). Gated on the
        // wave mode in the original; the port has no waves yet, so the plane
        // is carried and drawn but nothing sets it.
        if (haveFurn[tubes::kMarker]) {
            for (int r = 0; r < b.rows(); ++r) {
                for (int c = 0; c < b.cols(); ++c) {
                    if (!b.isObjective(c, r)) continue;
                    screen.draw(furn[tubes::kMarker], kGridX + c * kPitchX + 2,
                                kGridY + r * kPitchY + 1);
                }
            }
        }

        // Records 7..12 are the atoms tipped out of the tube and falling into
        // the beaker. `1000:5c1f` runs them after the grid and the marker
        // overlay and before `BEAKER.CSP`, so a falling atom passes in front
        // of the settled ones and behind the glass.
        for (int n = tubes::kAtomSlots + 1; n <= tubes::kAtomRecords; ++n) {
            const tubes::Falling& f = game->atom(n);
            if (!f.drawn() || !haveAtom[ball(f.colour)]) continue;
            screen.draw(atoms[ball(f.colour)], f.x, f.y);
        }

        if (haveBeaker) screen.draw(beaker, kGridX - 4, kGridY);

        drawHud(screen, *game, bigFont, smallFont, haveBig, haveSmall);
        drawTaskDisplay(screen, *game, bigFont, haveBig, atoms, haveAtom, furn,
                        haveFurn, smallBall, haveSmallBall);

        // `1000:a5d2` shows the briefing instead of the play field, before
        // `1000:3a67` ever runs, so it simply replaces everything above.
        if (briefingUp) {
            drawBriefing(screen, *game, &blackboard, haveBlackboard, headingFont,
                         smallFont, haveBig, haveSmall, atoms, haveAtom, furn,
                         haveFurn, briefDecor, sceneArt, scenePose());
        }

        // `1000:9499` replaces the field the same way the stats screen does,
        // and comes first because the session it ends is still notionally on
        // the stats stage when it starts.
        if (endingPage > 0) {
            tubes::ScenePose p = scenePose();
            p.jumpFrame = jumpFrame;
            drawEnding(screen, endingPage, &blackboard, haveBlackboard,
                       sceneArt, p, headingFont, haveHeading, smallFont,
                       haveSmall, &prizeArt, havePrize);
        }

        // The stats screen replaces the field; the banner, the Continue prompt
        // and the pause overlay go over whatever is already drawn, because
        // that is what the original does - none of the three clears first.
        if (endingPage == 0 &&
            (sstage == tubes::SessionStage::kStats ||
             sstage == tubes::SessionStage::kContinue)) {
            drawStats(screen, statsRows, &blackboard, haveBlackboard, sceneArt,
                      headingFont, smallFont, bigFont, haveHeading, haveSmall,
                      haveBig, scenePose());
        }
        if (sstage == tubes::SessionStage::kBanner) {
            // `1000:5ec9`: the `F2` hint appears only on the abort arm, and
            // only when the mode is not attract and saving is enabled.
            drawBanner(screen, banner, headingFont, haveHeading, smallFont,
                       haveSmall,
                        banner == tubes::Banner::kAborted && gameMode != 0 &&
                            game->edition().canSave());
        }
        if (sstage == tubes::SessionStage::kContinue) {
            drawContinue(screen, continuePrompt.ticksLeft(), headingFont,
                         haveHeading, bigFont, haveBig);
        }
        if (hs.active) {
            // `1000:96db` does not call `1b2e:0656` or `1b2e:0a11`. It draws
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
            // `VIEWER` parks it.
            screen.clear(0);
            if (haveBlackboard) screen.blit(blackboard, 0, tubes::kBoardY);
            if (haveBar) {
                screen.blit(slideBar, tubes::kHsViewBarX, tubes::kHsViewBarY);
            }
            drawHiScores(screen, hiScores[hs.bank], headingFont, haveHeading,
                         scriptFont, haveScript, hs.row, hs.name,
                         hs.hold > 0.0f ? 0 : hs.cursor);
        }
        if (saveUi.open) {
            const tubes::SaveBank b = gameMode == 1
                                          ? tubes::SaveBank::kEndurance
                                          : tubes::SaveBank::kWave;
            drawSaveScreen(screen, saves[b], b, saveUi.slotSel, saveUi.typing,
                           saveUi.desc, headingFont, haveHeading, scriptFont,
                           haveScript, &smallBall[1], haveSmallBall[1]);
        }
        if (helpScreen) {
            drawHelpScreen(screen, gameMode, headingFont, haveHeading,
                           smallFont, haveSmall);
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
