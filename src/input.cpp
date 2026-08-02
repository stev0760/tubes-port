#include "input.h"

#include <algorithm>
#include <sstream>

#include "screen.h"   // kScreenWidth / kScreenHeight, and no SDL in sight

namespace tubes {

const char* const kGameButtonNames[kGameButtons] = {
    "Up", "Down", "Left", "Right", "Button A", "Button B",
};

namespace {
// SDL scancodes and SDL_GameControllerButton values, spelled out rather than
// included, so this file has no SDL dependency. main.cpp is where they mean
// anything; a mismatch would show up the first time a default was pressed.
constexpr int kScUp = 82, kScDown = 81, kScLeft = 80, kScRight = 79;
constexpr int kScLCtrl = 224, kScLAlt = 226;
constexpr int kPadUp = 11, kPadDown = 12, kPadLeft = 13, kPadRight = 14;
constexpr int kPadA = 0, kPadB = 1;

const char* kSettingNames[kGameButtons] = {"up", "down", "left",
                                           "right", "a", "b"};
}  // namespace

const char* const kGraphicsRowNames[kGraphicsRows] = {
    "Display", "Window Size", "Pixels", "Vertical Sync", "Scanlines",
};

void Bindings::bindKey(GameButton g, int scancode) {
    if (scancode != kUnbound) {
        for (int i = 0; i < kGameButtons; ++i) {
            if (b[i].key == scancode) b[i].key = kUnbound;
        }
    }
    b[static_cast<int>(g)].key = scancode;
}

void Bindings::bindPad(GameButton g, int button) {
    if (button != kUnbound) {
        for (int i = 0; i < kGameButtons; ++i) {
            if (b[i].pad == button) b[i].pad = kUnbound;
        }
    }
    b[static_cast<int>(g)].pad = button;
}

Bindings defaultBindings() {
    Bindings x;
    x[GameButton::kUp] = {kScUp, kPadUp};
    x[GameButton::kDown] = {kScDown, kPadDown};
    x[GameButton::kLeft] = {kScLeft, kPadLeft};
    x[GameButton::kRight] = {kScRight, kPadRight};
    // A tips the tube and B speeds an atom, so A gets the button a thumb
    // rests on.
    x[GameButton::kA] = {kScLCtrl, kPadA};
    x[GameButton::kB] = {kScLAlt, kPadB};
    return x;
}

int displayUnitHeight(const GraphicsOptions& g) {
    // 240 is 200 x 1.2, which is 320x200 in a 4:3 frame - the shape a 1994
    // monitor showed and the shape the artists drew for.
    return g.aspect43 ? 240 : kScreenHeight;
}

DisplayRect presentRect(int winW, int winH, const GraphicsOptions& g) {
    const int unitH = displayUnitHeight(g);
    // The scale is a whole number of UNITS, which keeps columns exact: 320
    // source columns always land on a whole multiple of themselves, so there
    // is never a column two pixels wide beside one three pixels wide.
    //
    // Rows are exact only with square pixels. **4:3 puts 200 source rows into
    // 240 * s output rows, so a row is 1.2 * s tall and that is not a whole
    // number unless s is a multiple of five** - at 3x, source rows come out
    // three and four pixels tall in a repeating pattern. That is inherent to
    // showing a 320x200 image in a 4:3 frame by nearest neighbour, and it is
    // the reason square is the default: the stretch is what a CRT did, and it
    // is offered to players who want that, not imposed on everyone.
    int s = std::min(winW / kScreenWidth, winH / unitH);
    // A pinned scale is honoured only while it fits. Past that the window is
    // the constraint: a 6x window on a small display would put most of the
    // game off the edge, and Fit is at least a picture the player can see.
    if (g.scale > 0 && g.scale <= s) s = g.scale;
    s = std::max(1, s);
    const int w = kScreenWidth * s;
    const int h = unitH * s;
    // Centred, so what is left over is a black border rather than a bias.
    return DisplayRect{(winW - w) / 2, (winH - h) / 2, w, h};
}

std::string graphicsValueLabel(const GraphicsOptions& g, GraphicsRow r) {
    switch (r) {
    case GraphicsRow::kDisplay:
        return g.fullscreen ? "Fullscreen" : "Windowed";
    case GraphicsRow::kScale:
        // "Fit" is 0 and is the default, so the row reads as a word rather
        // than as a number that happens to mean "no number".
        if (g.scale <= 0) return "Fit";
        return std::to_string(g.scale) + "x";
    case GraphicsRow::kAspect:
        return g.aspect43 ? "4:3 Stretch" : "Square";
    case GraphicsRow::kVsync:
    case GraphicsRow::kScanlines: {
        const bool on = (r == GraphicsRow::kVsync) ? g.vsync : g.scanlines;
        // The menu's own words for a toggle - page 6 reads "Toggle Music yes".
        return on ? "yes" : "no";
    }
    }
    return "";
}

void cycleGraphics(GraphicsOptions& g, GraphicsRow r, int delta) {
    switch (r) {
    case GraphicsRow::kDisplay: g.fullscreen = !g.fullscreen; break;
    case GraphicsRow::kScale: {
        // 0..kMaxScale as one ring, so Fit sits next to 1x at one end and to
        // the largest scale at the other.
        const int n = GraphicsOptions::kMaxScale + 1;
        int v = (g.scale + delta) % n;
        if (v < 0) v += n;
        g.scale = v;
        break;
    }
    case GraphicsRow::kAspect: g.aspect43 = !g.aspect43; break;
    case GraphicsRow::kVsync: g.vsync = !g.vsync; break;
    case GraphicsRow::kScanlines: g.scanlines = !g.scanlines; break;
    }
}

std::string encodeSettings(const Settings& s) {
    std::ostringstream o;
    o << "# tubes-port settings. Not TUBES.SAV and not SETUP.CFG - this file\n"
         "# is the port's own and the game never reads it.\n";
    o << "music " << (s.music ? 1 : 0) << "\n";
    o << "sound " << (s.sound ? 1 : 0) << "\n";
    for (int i = 0; i < kGameButtons; ++i) {
        o << "bind " << kSettingNames[i] << " " << s.bindings.b[i].key << " "
          << s.bindings.b[i].pad << "\n";
    }
    o << "fullscreen " << (s.graphics.fullscreen ? 1 : 0) << "\n";
    o << "scale " << s.graphics.scale << "\n";
    o << "aspect43 " << (s.graphics.aspect43 ? 1 : 0) << "\n";
    o << "vsync " << (s.graphics.vsync ? 1 : 0) << "\n";
    o << "scanlines " << (s.graphics.scanlines ? 1 : 0) << "\n";
    return o.str();
}

void decodeSettings(const std::string& text, Settings& out) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string word;
        ls >> word;
        if (word == "music" || word == "sound") {
            int v = 1;
            if (!(ls >> v)) continue;
            (word == "music" ? out.music : out.sound) = v != 0;
        } else if (word == "fullscreen" || word == "aspect43" ||
                   word == "vsync" || word == "scanlines") {
            int v = 0;
            if (!(ls >> v)) continue;
            GraphicsOptions& g = out.graphics;
            if (word == "fullscreen") g.fullscreen = v != 0;
            else if (word == "aspect43") g.aspect43 = v != 0;
            else if (word == "vsync") g.vsync = v != 0;
            else g.scanlines = v != 0;
        } else if (word == "scale") {
            int v = 0;
            if (!(ls >> v)) continue;
            // Clamped rather than rejected: a hand-edited 12 becomes Fit,
            // which is a window the player can still see, where honouring it
            // would open one larger than the display.
            out.graphics.scale =
                (v < 0 || v > GraphicsOptions::kMaxScale) ? 0 : v;
        } else if (word == "bind") {
            std::string which;
            int key = kUnbound, pad = kUnbound;
            if (!(ls >> which >> key >> pad)) continue;
            for (int i = 0; i < kGameButtons; ++i) {
                if (which != kSettingNames[i]) continue;
                // Assigned directly rather than through `bindKey`, which would
                // unbind whatever was read a line earlier.
                out.bindings.b[i].key = key;
                out.bindings.b[i].pad = pad;
            }
        }
        // Anything else is from a version that knew more than this one.
    }
}

}  // namespace tubes
