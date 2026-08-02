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

// ---------------------------------------------------------------------------
// The first-run edition prompt - see the header for why it looks like this
// ---------------------------------------------------------------------------

const char* const kEditionAnswerText[kEditionAnswers] = {
    // The game's own words for the two releases. The shareware build calls the
    // other one `Preview Registered` on its own menu and its sign-off says
    // `Register`, so "Registered" is the program's term rather than a label
    // invented here; "Shareware" is what the release itself was called.
    "Registered Version",
    "Shareware Version",
};

namespace {

// A dialogue box roughly centred in the 80 x 25 field. The numbers are the
// port's - there is nothing to transliterate on a screen the original does not
// have - so they are named rather than scattered through the drawing.
constexpr int kBoxX = 14;
constexpr int kBoxY = 5;
constexpr int kBoxW = 52;
constexpr int kBoxH = 13;
constexpr int kTextX = kBoxX + 3;

// The text-mode attributes, in the DOS-dialogue convention: light grey for
// body, white for what matters, yellow for the row under the cursor.
constexpr uint8_t kBody = 0x07;
constexpr uint8_t kBright = 0x0F;
constexpr uint8_t kChosen = 0x0E;

// CP437's double-line box drawing, and the right-pointing triangle at 0x10.
constexpr uint8_t kHBar = 205, kVBar = 186, kTopL = 201, kTopR = 187;
constexpr uint8_t kBotL = 200, kBotR = 188, kPointer = 0x10;

void say(TextScreen& ts, int col, int row, const char* s, uint8_t attr) {
    for (int i = 0; s[i]; ++i) {
        ts.put(col + i, row, static_cast<uint8_t>(s[i]), attr);
    }
}

}  // namespace

int editionAnswerRow(int index) { return kBoxY + 8 + index; }

void buildEditionPrompt(TextScreen& ts, int selected) {
    for (int row = 0; row < kTextRows; ++row) {
        for (int col = 0; col < kTextCols; ++col) ts.put(col, row, ' ', kBody);
    }

    for (int i = 1; i < kBoxW - 1; ++i) {
        ts.put(kBoxX + i, kBoxY, kHBar, kBody);
        ts.put(kBoxX + i, kBoxY + kBoxH - 1, kHBar, kBody);
    }
    for (int j = 1; j < kBoxH - 1; ++j) {
        ts.put(kBoxX, kBoxY + j, kVBar, kBody);
        ts.put(kBoxX + kBoxW - 1, kBoxY + j, kVBar, kBody);
    }
    ts.put(kBoxX, kBoxY, kTopL, kBody);
    ts.put(kBoxX + kBoxW - 1, kBoxY, kTopR, kBody);
    ts.put(kBoxX, kBoxY + kBoxH - 1, kBotL, kBody);
    ts.put(kBoxX + kBoxW - 1, kBoxY + kBoxH - 1, kBotR, kBody);

    say(ts, kBoxX + 19, kBoxY, " Tubes Setup ", kBright);
    say(ts, kTextX, kBoxY + 2, "Tubes shipped in two editions, and this", kBody);
    say(ts, kTextX, kBoxY + 3, "one cannot tell which you have: the game", kBody);
    say(ts, kTextX, kBoxY + 4, "files are identical in both.", kBody);
    say(ts, kTextX, kBoxY + 6, "Which copy of Tubes do you have?", kBright);

    for (int i = 0; i < kEditionAnswers; ++i) {
        const bool on = i == selected;
        ts.put(kTextX + 3, editionAnswerRow(i), on ? kPointer : ' ',
               on ? kChosen : kBody);
        say(ts, kEditionAnswerCol, editionAnswerRow(i), kEditionAnswerText[i],
            on ? kChosen : kBody);
    }

    say(ts, kTextX, kBoxY + kBoxH - 2,
        "Up/Down to choose, Enter to accept.", kBody);
}

}  // namespace tubes
