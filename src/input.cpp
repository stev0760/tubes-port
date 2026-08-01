#include "input.h"

#include <cstdlib>
#include <sstream>

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
