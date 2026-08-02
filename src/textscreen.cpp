#include "textscreen.h"

namespace tubes {

// The 16 text-mode colours in 6-bit DAC units, which is how the VGA actually
// holds them and what the game's `.PAL` resources are in. The pattern is the
// EGA one: bits are blue/green/red at 2/3 intensity for 0..7, full for 8..15,
// with index 6 pulled down to brown rather than being dark yellow - the single
// irregularity in the table, and it is in the hardware, not here.
const uint8_t kTextPalette[16][3] = {
    { 0,  0,  0},   //  0 black
    { 0,  0, 42},   //  1 blue
    { 0, 42,  0},   //  2 green
    { 0, 42, 42},   //  3 cyan
    {42,  0,  0},   //  4 red
    {42,  0, 42},   //  5 magenta
    {42, 21,  0},   //  6 brown
    {42, 42, 42},   //  7 light grey
    {21, 21, 21},   //  8 dark grey
    {21, 21, 63},   //  9 bright blue
    {21, 63, 21},   // 10 bright green
    {21, 63, 63},   // 11 bright cyan
    {63, 21, 21},   // 12 bright red
    {63, 21, 63},   // 13 bright magenta
    {63, 63, 21},   // 14 yellow
    {63, 63, 63},   // 15 white
};

Palette textPalette() {
    Palette p{};
    for (int i = 0; i < 16; ++i) {
        p.rgb[i][0] = kTextPalette[i][0];
        p.rgb[i][1] = kTextPalette[i][1];
        p.rgb[i][2] = kTextPalette[i][2];
    }
    return p;
}

bool TextScreen::loadBin(const std::vector<uint8_t>& blob) {
    const size_t rowBytes = static_cast<size_t>(kTextCols) * 2;
    if (blob.empty() || blob.size() % rowBytes != 0) return false;
    const int rows = static_cast<int>(blob.size() / rowBytes);
    if (rows > kTextRows) return false;

    cells_.assign(static_cast<size_t>(kTextCols) * kTextRows, TextCell{' ', 0x00});
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < kTextCols; ++c) {
            const size_t i = (static_cast<size_t>(r) * kTextCols + c) * 2;
            TextCell cell;
            cell.ch = blob[i];
            cell.attr = blob[i + 1];
            cells_[static_cast<size_t>(r) * kTextCols + c] = cell;
        }
    }
    rowsLoaded_ = rows;
    return true;
}

TextCell TextScreen::at(int col, int row) const {
    if (col < 0 || col >= kTextCols || row < 0 || row >= kTextRows) return TextCell{' ', 0};
    return cells_[static_cast<size_t>(row) * kTextCols + col];
}

void TextScreen::put(int col, int row, uint8_t ch, uint8_t attr) {
    if (col < 0 || col >= kTextCols || row < 0 || row >= kTextRows) return;
    cells_[static_cast<size_t>(row) * kTextCols + col] = TextCell{ch, attr};
}

void TextScreen::render(std::vector<uint8_t>& out) const {
    out.assign(static_cast<size_t>(kTextScreenW) * kTextScreenH, 0);
    for (int row = 0; row < kTextRows; ++row) {
        for (int col = 0; col < kTextCols; ++col) {
            const TextCell cell = cells_[static_cast<size_t>(row) * kTextCols + col];
            // Bit 7 is blink, not a fifth background bit - see the header for
            // why it is dropped rather than honoured.
            const uint8_t fg = cell.attr & 0x0F;
            const uint8_t bg = (cell.attr >> 4) & 0x07;
            const uint8_t* glyph = kCp437Font8x16[cell.ch];
            for (int gy = 0; gy < kGlyphH; ++gy) {
                const uint8_t bits = glyph[gy];
                const size_t base =
                    static_cast<size_t>(row * kGlyphH + gy) * kTextScreenW + col * kGlyphW;
                for (int gx = 0; gx < kGlyphW; ++gx) {
                    // MSB is the leftmost pixel, the way the character
                    // generator shifts it out.
                    out[base + gx] = (bits & (0x80 >> gx)) ? fg : bg;
                }
            }
        }
    }
}

}  // namespace tubes
