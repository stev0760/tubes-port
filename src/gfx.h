// Decode the original graphics formats into plain 8-bit indexed surfaces.

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

// A decoded compiled sprite. The origin is relative to the draw position and
// may be negative, because a .CSP addresses pixels around a base pointer.
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

// The screen fade, at last. `23e7:0097` fades in and `23e7:00ce` fades out,
// and every screen in the game calls them. That is why three scans of the
// GAME segments for a fade found nothing: it lives in the graphics unit,
// which is exactly where the player guessed it would be.
//
// Both are one loop over a step counter `n`, up from 0 or down from
// `[DS:0x0ce6]` = 40, rewriting all 768 DAC components each time:
//
//     component := targetComponent * n div 40        { MUL BX / DIV [0ce6] }
//
// and handing the result to `23e7:003d`, which waits for one vertical retrace
// before uploading. So a fade is 41 uploads 1/70 s apart - 0.586 s, which is
// the "half a second or so" the player timed between screens.
//
// The target palette is the one `23e7:006c` last stored at `DS:0x2400`, and
// that routine blacks the DAC as it stores, so a screen is always drawn to a
// dark display and revealed by the fade, never flashed.
//
// The arithmetic is on the raw 6-bit values, before the DAC expansion, which
// is why this takes the .PAL bytes rather than a Palette: truncating twice
// would not give the original's ramp.
constexpr int kFadeSteps = 40;              // [DS:0x0ce6]

Palette fadePalette(const Bytes& raw, int step, int steps = kFadeSteps);

// How a .GFX's pixels are stored. This is not a property of the file: three
// resources carry a leading 0xE5 and it is a header byte, not a layout
// marker. Both of the original's blitters skip exactly one byte before
// reading the width - `23df:0022` then copies rows chunky, `2321:0948` walks
// four planes - so the caller decides, by choosing a routine.
//
// `kAuto` is the rule the port has used from the start and it is right for 72
// of the 73 resources: prefixed means chunky. The exception is `AMWRITE.GFX`,
// which the Absolute Magic splash draws with the Mode X routine, and reading
// it chunky produced a smear where "Absolute Magic" should be. A player
// spotted that; no size check could have.
enum class GfxLayout { kAuto, kChunky, kPlanar };

// .GFX - u16 width, u16 height, then pixels, optionally behind a 0xE5 byte.
bool decodeGfx(const Bytes& data, Image& out, std::string& error,
               GfxLayout layout = GfxLayout::kAuto);

// .CSP - generated 16-bit x86 that draws the sprite with unrolled stores.
// Interpreted rather than executed.
bool decodeCsp(const Bytes& data, Sprite& out, std::string& error);

// .SPR - a numbered strip of .GFX images in one resource. There are two,
// `AMLOGO.SPR` and `LIGHTN.SPR`, both owned by the Absolute Magic splash.
//
//     u16   0x00f5              a marker; both files carry it
//     u16   count
//     u16   offset[count]       from the start of the file
//     ...   count .GFX images, each u16 width, u16 height, then pixels
//
// The offsets are an oracle rather than a reading: every one of the eleven
// sub-images satisfies `next - offset = width * height + 4` exactly, and the
// first offset is exactly the header length. The pixels are planar, like an
// ordinary .GFX with no 0xE5 - which is what rendering them settles, since a
// wrong choice there passes every size check and only shows up on screen.
bool decodeSpr(const Bytes& data, std::vector<Image>& out, std::string& error);

// .ANM - a delta animation, and like a .CSP it is executable rather than
// data: `21d5:0000` FAR CALLS each frame with ES:DI on the mode 13h
// framebuffer and DS:SI on the frame itself.
//
//     u16   count
//     u32   size[count]         bytes of the frame, only the low word read
//     ...   count code blobs, each ending in RETF with its literal pixels
//           stored after the RETF - which is what its opening
//           `add si,<code length>` skips SI over
//
// A frame paints only what changed, so the frames must be replayed in order
// over the still image beneath (`SOFT.GFX`). Here that comes out as runs of
// bytes at a linear `y * 320 + x` offset; the interpreter is in gfx.cpp and
// `tools/anm_decode.py` is the cross-check, exactly as `csp_decode.py` is for
// the compiled sprites.
struct AnimFrame {
    struct Run {
        int offset = 0;                 // linear, into a 320x200 screen
        std::vector<uint8_t> pixels;
    };
    std::vector<Run> runs;
};

bool decodeAnm(const Bytes& data, std::vector<AnimFrame>& out,
               std::string& error);

}  // namespace tubes
