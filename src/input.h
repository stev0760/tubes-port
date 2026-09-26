// Control bindings - the one part of this port that is deliberately not a
// transliteration.
//
// The original asks an input driver for one byte a frame: Up, Down, Left,
// Right, A, B. That byte is the boundary between the game and the machine. It
// is real game logic: `DEMO.SCR` stores exactly one of them per frame, so a
// recorded demo replays through the same code path as live play. Nothing here
// changes it, and `Game::update` still takes the same mask.
//
// What the port replaces is everything below that byte. `KEYBOARD.DRV`,
// `JOYSTK1/2.DRV` and `MOUSE.DRV` existed because 1994 had no common
// abstraction over an XT keyboard, a gameport and a serial mouse. SDL is that
// abstraction, so porting a driver chooser would mean transliterating the
// absence of SDL. The original could bind one device at a time; SDL can poll a
// keyboard and a gamepad together.
//
// The Game Options page keeps the two toggles, which are game state
// (`DS:0x215f` and `DS:0x215e`, the same flags F3 and F4 flip). Its third item
// remaps the six controls, just as the original's did, by a modern route.
// There is no device to choose.
//
// `SETUP.CFG` is not written. The original rewrites it on leaving the page,
// but that file is the DOS install's hardware configuration and belongs to
// `SETUP.EXE`. The port does not read a byte of it and must not damage it.
// These settings go in the port's own file, the same lesson as the game
// directory.
//
// The same reasoning extends to the graphics options screen below. Mode X was
// the only display the original had, so fullscreen, scale and aspect are
// choices SDL creates rather than choices the game made. They belong to the
// port, they persist in `Settings` beside these, and they stay render-side.
// Every speed in the game is a whole number of pixels per fixed 16.11 Hz
// step, so no display option may touch that step.
//
// A binding is two opaque integers. The meaning of a scancode or a controller
// button belongs to SDL, and SDL lives at the edge of the port.
#ifndef TUBES_INPUT_H
#define TUBES_INPUT_H

#include <string>
#include <vector>

#include "edition.h"

namespace tubes {

// The six inputs the driver reports, in the mask's own bit order. See
// `namespace button` in game.h; its values match `DEMO.SCR`'s bytes.
enum class GameButton { kUp, kDown, kLeft, kRight, kA, kB };
constexpr int kGameButtons = 6;

// What the redefine screen calls each one. "Button A" and "Button B" are the
// original's own words for them. The Instructions say "Button A tips the test
// tube" without naming a key, because the key depended on the driver.
extern const char* const kGameButtonNames[kGameButtons];

// One control, bound on both sources at once. A player with a gamepad plugged
// in should not have to visit a menu, and rebinding on the keyboard should not
// silently unbind the pad. Each holds its own binding, and the mask is the OR.
//
// `key` is an SDL scancode and `pad` is an SDL_GameControllerButton, both
// stored as plain ints so this header stays free of SDL. -1 is unbound.
struct Binding {
    int key = -1;
    int pad = -1;
};

constexpr int kUnbound = -1;

struct Bindings {
    Binding b[kGameButtons];

    Binding& operator[](GameButton g) { return b[static_cast<int>(g)]; }
    const Binding& operator[](GameButton g) const {
        return b[static_cast<int>(g)];
    }

    // Clears any other control holding the same key or pad button. A binding
    // cannot be shared: otherwise the game would read two directions at once
    // from one press, which no driver could ever have produced.
    void bindKey(GameButton g, int scancode);
    void bindPad(GameButton g, int button);
};

// The port's defaults, and also what `readKeyboard` hard-coded before this
// existed: the arrow keys, Ctrl or Space for A, Alt for B. The gamepad half
// is the obvious modern reading - the d-pad and the two face buttons a thumb
// sits on.
Bindings defaultBindings();

// ---------------------------------------------------------------------------
// Is this input one the driver would have claimed?
// ---------------------------------------------------------------------------
//
// `KEYBOARD.DRV` hooks the keyboard and takes the six keys it maps to Up,
// Down, Left, Right, A and B. They never reach the BIOS buffer that Turbo
// Pascal's `ReadKey` drains. That is invisible almost everywhere: the game
// reads the driver, not the keyboard. But it decides one thing outright. The
// F1 Help overlay waits in a bare `ReadKey` (`1000:301b`), so every key
// except the six the driver ate dismisses it.
//
// Measured on the original, sixteen probes: Up, Down, Left, Right, Ctrl and
// Space leave the overlay up; `a`, `z`, Tab, Enter, ESC, F3 and F5 all take
// it down. Six keys - exactly the driver's six inputs.
//
// This is where the port's one departure earns its keep. `src/input.h` does
// not transliterate the driver chooser because SDL is the abstraction the
// drivers existed to provide. The faithful transfer of "the driver ate it" is
// "the player has this bound to a control", and it keeps working after a
// rebind. A hard-coded key list would not. A joystick driver claims the stick
// instead and leaves the whole keyboard live. The pad half below applies the
// same rule for the same reason.
bool bindsKey(const Bindings& b, int scancode);
bool bindsPad(const Bindings& b, int padButton);

// ---------------------------------------------------------------------------
// Display options - the port's own, like the bindings above
// ---------------------------------------------------------------------------
//
// Nothing here is transliterated, and nothing here may be. The original had
// one display mode, unchained 320x200x256, and any choice about the hardware
// under it belonged to `SETUP.EXE`. These are the choices SDL created by
// existing, which is exactly the argument the bindings make.
//
// The rule that governs all of it: a display option changes how a frame is
// presented and never how one is computed. Every speed in the game is a whole
// number of pixels per 16.11 Hz frame, so all of this lives in the blit at
// the end of `presentScreen` and touches nothing that was reverse engineered.
// `screen.cpp` hands over the same 320x200 indexed framebuffer either way.
//
// Plain ints and bools, no SDL types, for the same reason `Binding` holds
// two opaque integers: `main.cpp` is the only file that knows what a display
// mode is.
struct GraphicsOptions {
    bool fullscreen = false;

    // 0 is "fit", which the port has always done and which `--scale 0` means:
    // pick the largest whole multiple the display can take. 1..6 pin it. Whole
    // multiples only - a 320x200 framebuffer at 2.5x is a grid of uneven
    // pixels, which an indexed retro renderer must not do by accident.
    int scale = 0;
    static constexpr int kMaxScale = 6;

    // Square pixels, or the 4:3 a 1994 monitor actually showed. 320x200 in a
    // 4:3 frame is a 1.2x vertical stretch, which is why the game's sprites
    // are drawn slightly squat: the artists were drawing for the stretch.
    //
    // It is offered rather than imposed, and it defaults off, because the
    // stretch costs something real - 200 rows over 240 * scale means source
    // rows three and four pixels tall alternating at 3x, where square pixels
    // are exact. Square pixels are also what every pixel-diff capture in
    // `docs/debug-rig.md` was taken against.
    bool aspect43 = false;

    // On by default. The frame loop paces itself on its own clock, so vsync
    // costs nothing and stops the tearing a 16.11 Hz update makes obvious.
    bool vsync = true;

    // A dark line over every other output row. Cosmetic and off by default.
    // It is a CRT impression, not a simulation - no phosphor bloom, no shadow
    // mask, no attempt at either.
    bool scanlines = false;
};

// Where the 320x200 framebuffer lands inside an output of `winW` x `winH`.
// This is the whole scale-and-letterbox rule. It is here rather than beside
// SDL so tests can check it: `tubes-tests` links no SDL at all.
struct DisplayRect { int x, y, w, h; };
DisplayRect presentRect(int winW, int winH, const GraphicsOptions& g);

// The height one unit of the framebuffer occupies before scaling. 200 with
// square pixels, 240 stretched - 320x200 shown in a 4:3 frame.
int displayUnitHeight(const GraphicsOptions& g);

// What the graphics screen puts on each row, and the order.
enum class GraphicsRow { kDisplay, kScale, kAspect, kVsync, kScanlines };
constexpr int kGraphicsRows = 5;
extern const char* const kGraphicsRowNames[kGraphicsRows];

// The value column for one row - "Fullscreen", "3x", "4:3" and so on. Here
// rather than in `main.cpp` so the tests can read it without SDL.
std::string graphicsValueLabel(const GraphicsOptions& g, GraphicsRow r);

// Advance one row's value by `delta` (+1 or -1). Every row wraps, so a
// player who can only find one direction still reaches every value.
void cycleGraphics(GraphicsOptions& g, GraphicsRow r, int delta);

// ---------------------------------------------------------------------------
// The port's settings file
// ---------------------------------------------------------------------------
//
// Text, one setting per line. It is written by hand as often as by the game,
// and a corrupt binary would be a silent lockout. Unknown lines are ignored
// rather than rejected, so a file from a later version still loads.
//
//     music 1
//     sound 1
//     bind up 82 11
//     fullscreen 0
//
struct Settings {
    bool music = true;      // `DS:0x215f`
    bool sound = true;      // `DS:0x215e`
    Bindings bindings = defaultBindings();
    GraphicsOptions graphics;

    // Which edition the player owns - the port's own setting, and the only one
    // here that the original has no counterpart for at all.
    //
    // It has to be asked rather than detected. `TUBES.RES` is byte-identical
    // between the editions, so an install directory carries no evidence of
    // which one it came from. The edition lives only in the executable, and
    // the port is the executable. Anything calling itself detection would be
    // inventing evidence. See PLAN.md, "Where the edition switch should live".
    //
    // `editionChosen` makes the question first-run: it is false until the
    // player answers or a `--shareware` / `--preview` flag is made sticky, and
    // the key is simply absent from the file until then. A fresh install asks
    // once and never again; deleting the line asks again. That is the whole
    // recovery path for answering it wrongly.
    Edition edition = Edition::kRegistered;
    bool editionChosen = false;

    // The folder the game was last found in, UTF-8 and absolute. Written
    // whenever an interactive run finds the game somewhere else, so a player
    // who dropped the .exe into their Tubes folder once can start it from a
    // shortcut afterwards. Only a fallback: see `gamedir.h` for the order.
    std::string gameDir;
};

std::string encodeSettings(const Settings& s);
// Never fails: anything unparseable keeps the default already in `out`.
void decodeSettings(const std::string& text, Settings& out);

}  // namespace tubes

#endif  // TUBES_INPUT_H
