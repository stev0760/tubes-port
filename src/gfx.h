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
    int originX = 0;
    int originY = 0;
    std::vector<uint8_t> pixels;    // width * height
    std::vector<uint8_t> mask;      // 1 where a pixel was written
};

bool loadPalette(const Bytes& data, Palette& out, std::string& error);

// .GFX - u16 width, u16 height, then pixels. Data is planar (Mode X
// plane-major) unless the file carries a leading 0xE5, which marks chunky.
bool decodeGfx(const Bytes& data, Image& out, std::string& error);

// .CSP - generated 16-bit x86 that draws the sprite with unrolled stores.
// Interpreted rather than executed.
bool decodeCsp(const Bytes& data, Sprite& out, std::string& error);

}  // namespace tubes
