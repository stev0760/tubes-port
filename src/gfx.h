// Decoding the original graphics formats into plain 8-bit indexed surfaces.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "res.h"

namespace tubes {

// A 256-entry RGB palette expanded from the 6-bit VGA values in a .PAL.
struct Palette {
    uint8_t rgb[256][3] = {};
};

// An indexed image. `transparent` marks the colour index treated as
// see-through when compositing; -1 means fully opaque.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;    // width * height, chunky
    int transparent = -1;

    bool valid() const {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<size_t>(width) * height;
    }
};

// A decoded compiled sprite. Origin is relative to the draw position, and
// may be negative, since a .CSP addresses pixels around a base pointer.
struct Sprite {
    int width = 0;
    int height = 0;
    // Where this sprite's pixels start, relative to the .CSP base pointer.
    // Compare against kSpriteBaseX/Y below to get the placement offset.
    int originX = 0;
    int originY = 0;
    std::vector<uint8_t> pixels;    // width * height
    std::vector<uint8_t> mask;      // 1 where a pixel was written
};

// The base every .CSP displacement is measured from. Not chosen: it is the
// minimum over all 108 sprites in TUBES.RES, and 84 of them sit exactly on
// it. See Screen::draw for why the excess is placement data rather than an
// artefact of the decoder's modulo arithmetic.
constexpr int kSpriteBaseX = 128;
constexpr int kSpriteBaseY = -2;

bool loadPalette(const Bytes& data, Palette& out, std::string& error);

// The screen fade, at last - `23e7:0097` fades IN and `23e7:00ce` fades OUT,
// and every screen in the game calls them. That is why three scans of the
// GAME segments for a fade found nothing: it lives in the graphics unit, which
// is exactly where the player guessed it would be.
//
// Both are one loop over a step counter `n`, up from 0 or down from
// `[DS:0x0ce6]` = 40, rewriting all 768 DAC components each time:
//
//     component := targetComponent * n div 40        { MUL BX / DIV [0ce6] }
//
// and handing the result to `23e7:003d`, which waits for ONE vertical retrace
// before uploading. So a fade is 41 uploads 1/70 s apart - 0.586 s, which is
// the "half a second or so" the player timed between screens.
//
// The target palette is the one `23e7:006c` last stored at `DS:0x2400`, and
// that routine blacks the DAC as it stores - so a screen is always drawn to a
// dark display and revealed by the fade, never flashed.
//
// The arithmetic is on the RAW 6-bit values, before the DAC expansion, which
// is why this takes the .PAL bytes rather than a Palette: truncating twice
// would not give the original's ramp.
constexpr int kFadeSteps = 40;              // [DS:0x0ce6]

Palette fadePalette(const Bytes& raw, int step, int steps = kFadeSteps);

// .GFX - u16 width, u16 height, then pixels. Data is planar (Mode X
// plane-major) unless the file carries a leading 0xE5, which marks chunky.
bool decodeGfx(const Bytes& data, Image& out, std::string& error);

// .CSP - generated 16-bit x86 that draws the sprite with unrolled stores.
// Interpreted rather than executed.
bool decodeCsp(const Bytes& data, Sprite& out, std::string& error);

}  // namespace tubes
