// Control bindings - the one part of this port that is deliberately NOT a
// transliteration.
//
// The original asks an input DRIVER for one byte a frame: Up, Down, Left,
// Right, A, B. That byte is the boundary between the game and the machine, and
// it is real game logic - `DEMO.SCR` stores exactly one of them per frame,
// which is why a recorded demo replays through the same code path as live
// play. Nothing here changes it; `Game::update` still takes the same mask.
//
// What IS replaced is everything below that byte. `KEYBOARD.DRV`,
// `JOYSTK1/2.DRV` and `MOUSE.DRV` exist because 1994 had no common abstraction
// over an XT keyboard, a gameport and a serial mouse. SDL is that abstraction,
// so porting a driver chooser would be transliterating the ABSENCE of SDL -
// and it would be worse than the original, which could bind one device at a
// time where SDL can poll a keyboard and a gamepad at once.
//
// So the Game Options page keeps the two toggles, which are game state
// (`DS:0x215f` and `DS:0x215e`, the same flags F3 and F4 flip), and its third
// item does what the original's did - remap the six controls - by a modern
// route. There is no device to choose.
//
// **`SETUP.CFG` is not written.** The original rewrites it on leaving the
// page, but that file is the DOS install's hardware configuration and belongs
// to `SETUP.EXE`. The port does not read a byte of it and must not damage it;
// these settings go in the port's own file. Same lesson as the game directory.
//
// The same reasoning extends to the GRAPHICS options screen below, and it is
// the same argument one layer over: Mode X was the only display the original
// had, so fullscreen, scale and aspect are choices SDL creates rather than
// choices the game made. They belong to the port, they persist in `Settings`
// beside these, and they stay render-side - the fixed 16.11 Hz step is load
// bearing and no display option may touch it.
//
// Platform-agnostic: a binding is two opaque integers, because the meaning of
// a scancode or a controller button belongs to SDL and SDL lives at the edge.
#ifndef TUBES_INPUT_H
#define TUBES_INPUT_H

#include <string>
#include <vector>

namespace tubes {

// The six the driver reports, in the mask's own bit order - see
// `namespace button` in game.h, whose values match `DEMO.SCR`'s bytes.
enum class GameButton { kUp, kDown, kLeft, kRight, kA, kB };
constexpr int kGameButtons = 6;

// What the redefine screen calls each one. "Button A" and "Button B" are the
// original's own words for them: the Instructions say "Button A tips the test
// tube" without ever naming a key, because the key depended on the driver.
extern const char* const kGameButtonNames[kGameButtons];

// One control, bound on both sources at once. A player with a gamepad plugged
// in should not have to visit a menu, and rebinding on the keyboard should not
// silently unbind the pad - so each holds its own and the mask is the OR.
//
// `key` is an SDL scancode and `pad` an SDL_GameControllerButton, both stored
// as plain ints so this header stays free of SDL. -1 is unbound.
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

    // Clears any OTHER control holding the same key or pad button, so a
    // binding cannot be shared - the game would otherwise read two directions
    // at once from one press, which no driver could ever have produced.
    void bindKey(GameButton g, int scancode);
    void bindPad(GameButton g, int button);
};

// The port's defaults, which are also what `readKeyboard` hard-coded before
// this existed: the arrow keys, Ctrl or Space for A, Alt for B. The gamepad
// half is the obvious modern reading - the d-pad, and the two face buttons a
// thumb sits on.
Bindings defaultBindings();

// ---------------------------------------------------------------------------
// Display options - THE PORT'S OWN, like the bindings above
// ---------------------------------------------------------------------------
//
// Nothing here is transliterated and nothing here may be: the original had one
// display mode, unchained 320x200x256, and whatever choice existed about the
// hardware under it belonged to `SETUP.EXE`. These are the choices SDL created
// by existing, which is exactly the argument the bindings make.
//
// **The rule that governs all of it: a display option changes how a frame is
// PRESENTED and never how one is computed.** The 16.11 Hz step is load bearing
// - every speed in the game is a whole number of pixels per frame - so all of
// this lives in the blit at the end of `presentScreen` and touches nothing
// that was reverse engineered. `screen.cpp` hands over the same 320x200
// indexed framebuffer either way.
//
// Plain ints and bools, no SDL types, for the same reason `Binding` holds two
// opaque integers: `main.cpp` is the only file that knows what a display mode
// is.
struct GraphicsOptions {
    bool fullscreen = false;

    // 0 is "fit", which is what the port has always done and what `--scale 0`
    // means: pick the largest whole multiple the display can take. 1..6 pins
    // it. Whole multiples only - a 320x200 framebuffer at 2.5x is a grid of
    // uneven pixels, which is the one thing an indexed retro renderer must not
    // do by accident.
    int scale = 0;
    static constexpr int kMaxScale = 6;

    // Square pixels, or the 4:3 a 1994 monitor actually showed. 320x200 in a
    // 4:3 frame is a 1.2x vertical stretch, which is why the game's sprites
    // are drawn slightly squat: the artists were drawing for the stretch.
    //
    // It is offered rather than imposed, and it defaults OFF, because the
    // stretch costs something real - 200 rows over 240 * scale means source
    // rows three and four pixels tall alternating at 3x, where square pixels
    // are exact. It is also what every pixel-diff capture in
    // `docs/debug-rig.md` was taken against.
    bool aspect43 = false;

    // On by default: the frame loop paces itself on its own clock, so vsync
    // costs nothing and stops the tearing a 16.11 Hz update makes obvious.
    bool vsync = true;

    // A dark line over every other output row. Cosmetic, off by default, and
    // it is a CRT impression rather than a simulation of one - no phosphor
    // bloom, no shadow mask, no attempt at either.
    bool scanlines = false;
};

// Where the 320x200 framebuffer lands inside an output of `winW` x `winH`.
// The whole of the scale-and-letterbox rule, and it is here rather than beside
// SDL so the tests can check it: `tubes-tests` links no SDL at all.
struct DisplayRect { int x, y, w, h; };
DisplayRect presentRect(int winW, int winH, const GraphicsOptions& g);

// The height one unit of the framebuffer occupies before scaling: 200 with
// square pixels, 240 stretched, which is 320x200 shown in a 4:3 frame.
int displayUnitHeight(const GraphicsOptions& g);

// What the graphics screen puts on each row, and the order it puts them in.
enum class GraphicsRow { kDisplay, kScale, kAspect, kVsync, kScanlines };
constexpr int kGraphicsRows = 5;
extern const char* const kGraphicsRowNames[kGraphicsRows];

// The value column for one row - "Fullscreen", "3x", "4:3" and so on. Here
// rather than in `main.cpp` so the tests can read it without SDL.
std::string graphicsValueLabel(const GraphicsOptions& g, GraphicsRow r);

// Advance one row's value by `delta` (+1 or -1). Every row wraps, so a player
// who can only find one direction still reaches every value.
void cycleGraphics(GraphicsOptions& g, GraphicsRow r, int delta);

// ---------------------------------------------------------------------------
// The port's settings file
// ---------------------------------------------------------------------------
//
// Text, one setting per line, because it is written by hand as often as by the
// game and a corrupt binary would be a silent lockout. Unknown lines are
// ignored rather than rejected, so a file from a later version still loads.
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
};

std::string encodeSettings(const Settings& s);
// Never fails: anything unparseable keeps the default that was already there.
void decodeSettings(const std::string& text, Settings& out);

}  // namespace tubes

#endif  // TUBES_INPUT_H
