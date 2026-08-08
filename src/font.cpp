#include "font.h"

namespace tubes {
namespace {

// One glyph, `2000:35ec`. The original writes straight into Mode X with a
// plane mask, which is why its inner loop looks like four unrolled nibble
// tests. None of that survives translation to a linear framebuffer. What does
// survive is everything above it:
//
//     src := font + char * cellH;
//     for row := cellH downto 1 do begin
//         bits := src^;  Inc(src);
//         if bits <> 0 then <plot the set bits in colour BH>;
//         case mode of
//           1: Dec(BH);
//           2: Inc(BH);
//           3: if row > peakRow then Dec(BH, 2) else Inc(BH, 2);
//         end
//     end
//
// Note `if bits <> 0 then` and the per-pixel test inside: only set bits are
// written, so a glyph is transparent and never lays down a background.
void drawGlyph(Screen& scr, const Font& f, int x, int y, uint8_t colour,
               uint8_t mode, uint8_t ch) {
    const uint8_t* src = f.glyphs.data() + static_cast<size_t>(ch) * f.cellH;
    uint8_t c = colour;
    // The original's loop counter runs down from the cell height, and mode 3
    // compares that counter against the turning point. So the peak is measured
    // from the bottom of the cell, not the top.
    for (int row = f.cellH; row >= 1; --row) {
        const uint8_t bits = *src++;
        const int py = y + (f.cellH - row);
        if (bits && py >= 0 && py < kScreenHeight) {
            for (int b = 0; b < 8; ++b) {
                if (!(bits & (0x80 >> b))) continue;
                const int px = x + b;
                if (px < 0 || px >= kScreenWidth) continue;
                scr.pixelsMutable()[static_cast<size_t>(py) * kScreenWidth +
                                    static_cast<size_t>(px)] = c;
            }
        }
        switch (mode) {
            case textmode::kFadeDown: --c; break;
            case textmode::kFadeUp:   ++c; break;
            case textmode::kPeak:
                if (row > f.peakRow) c = static_cast<uint8_t>(c - 2);
                else                 c = static_cast<uint8_t>(c + 2);
                break;
            default: break;
        }
    }
}

}  // namespace

bool decodeFont(const Bytes& raw, int advance, int peak, Font& out,
                std::string& error) {
    if (raw.empty() || raw.size() % 256 != 0) {
        error = "font is not a whole number of 256 glyph cells";
        return false;
    }
    out.glyphs = raw;
    out.cellH = static_cast<int>(raw.size() / 256);
    out.advance = advance;
    // 1000:42ae passes 4 and the setup stores 5; 1000:42e8 passes 7 and it
    // stores 8. The increment is `2000:3fab`'s, not the caller's.
    out.peakRow = peak + 1;
    return true;
}

int textWidth(const Font& f, const std::string& s) {
    return static_cast<int>(s.size()) * f.advance;
}

void drawText(Screen& scr, const Font& f, int x, int y, uint8_t colour,
              uint8_t mode, const std::string& s) {
    if (!f.valid()) return;
    const bool shadow = (mode & textmode::kShadow) != 0;
    const uint8_t m = static_cast<uint8_t>(mode & 0x7f);

    for (char raw : s) {
        const uint8_t ch = static_cast<uint8_t>(raw);
        // 2000:36e8: a space advances and draws nothing at all - not even the
        // shadow, which is a visible difference on a padded number.
        if (ch != ' ') {
            if (shadow) {
                drawGlyph(scr, f, x + 1, y + 1, kShadowColour,
                          textmode::kFlat, ch);
            }
            drawGlyph(scr, f, x, y, colour, m, ch);
        }
        x += f.advance;
    }
}

void drawTextCentred(Screen& scr, const Font& f, int x0, int x1, int y,
                     uint8_t colour, uint8_t mode, const std::string& s) {
    if (!f.valid()) return;
    // 2321:05e8. The original adds the two bounds and halves, so passing
    // (0, 319) centres on 159 rather than 160 - a half-pixel the port keeps.
    const int x = (x0 + x1 - textWidth(f, s)) >> 1;
    drawText(scr, f, x, y, colour, mode, s);
}

}  // namespace tubes
