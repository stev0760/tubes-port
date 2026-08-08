// Bitmap text, transliterated from the original's own text unit.
//
// The fonts are standard VGA glyph tables with no header at all: 256
// characters, one byte per scanline, most significant bit leftmost. The cell
// height is the file size divided by 256, and the cell width is always 8
// because a scanline is exactly one byte. See docs/reversing-notes.md.
//
// What is not obvious from the files is the renderer, and that is where the
// game's look comes from. `2000:35ec` draws a glyph one scanline at a time and
// walks the palette index as it goes, so a character is a vertical gradient
// rather than a flat colour. The HUD's cyan digits are a single colour index
// of 127 plus that walk; the palette holds a cyan ramp at 112..127 and the
// glyph simply reads down it.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "res.h"
#include "screen.h"

namespace tubes {

// `2000:35ec`'s `BL`, applied after every scanline. Modes 1 and 2 are a linear
// ramp; mode 3 is a peak, brightening to the middle of the cell and dimming
// again, and is what the score pop-ups use.
namespace textmode {
constexpr uint8_t kFlat = 0;
constexpr uint8_t kFadeDown = 1;   // colour - 1 per scanline
constexpr uint8_t kFadeUp = 2;     // colour + 1 per scanline
constexpr uint8_t kPeak = 3;       // -2 above the midpoint, +2 below it
// `2000:36ab` takes the mode with this bit set to mean "draw the glyph a
// second time first, at (x + 1, y + 1), flat, in `kShadowColour`". Every call
// in the game session sets it.
constexpr uint8_t kShadow = 0x80;
}  // namespace textmode

// The shadow's index, `DS:0x239c`. Measured off a captured frame rather than
// found in the setup code: the pixel one down and one right of every glyph
// stem in the HUD is index 0.
constexpr uint8_t kShadowColour = 0;

// A loaded glyph table plus the four numbers `2000:3fab` stores alongside it.
// The game only ever selects two of these, and it swaps between them mid-frame
// - the labels and the pop-ups are the small one, the numbers the large one.
struct Font {
    std::vector<uint8_t> glyphs;   // 256 * cellH bytes
    int cellH = 16;                // DS:0x2397, also the per-character stride
    int advance = 8;               // DS:0x2398
    int peakRow = 8;               // DS:0x2399, the mode-3 turning point

    bool valid() const {
        return cellH > 0 && glyphs.size() == 256u * static_cast<size_t>(cellH);
    }
};

// `advance` and `peak` are the renderer's, not the file's: a glyph cell is
// always 8 wide and fonts that are narrower just leave the right columns
// clear, so how far to step is the caller's decision. The game passes 8 for
// FUTURE.816 and 6 for TINY6X8.88.
//
// `peak` is `2000:3fab`'s last argument PLUS ONE, which is how the original
// stores it; pass the raw argument and this adds the one.
bool decodeFont(const Bytes& raw, int advance, int peak, Font& out,
                std::string& error);

// Total advance of `s` in pixels. Spaces cost the same as anything else here:
// the proportional path in `2000:36ab` is gated on `DS:0x239d`, which the
// game session never sets.
int textWidth(const Font& f, const std::string& s);

// `2000:36ab`. A space is skipped entirely rather than drawn, so it can never
// lay down a shadow.
void drawText(Screen& scr, const Font& f, int x, int y, uint8_t colour,
              uint8_t mode, const std::string& s);

// `2000:37ea`, the centred variant: it measures the string and starts it at
// `(x0 + x1 - width) / 2`. The game passes 0 and 319 for the score, so the
// score is centred on the screen rather than on the beaker.
void drawTextCentred(Screen& scr, const Font& f, int x0, int x1, int y,
                     uint8_t colour, uint8_t mode, const std::string& s);

}  // namespace tubes
