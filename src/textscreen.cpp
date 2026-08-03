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
// The version picker - see the header for what it is and why it looks like this
// ---------------------------------------------------------------------------

const char* const kEditionAnswerText[kEditionAnswers] = {
    // Named for what the player gets, because this is a CHOICE now rather than
    // a question about what they own - both editions are freely downloadable,
    // so the interesting difference is what is in them.
    "Registered   -   75 waves, both special atoms",
    "Shareware    -   25 waves, plus the Preview",
};

namespace {

// `TUBESEND.BIN`'S OWN PANEL GRAMMAR, read off the file rather than invented,
// which is what lets this screen sit beside the game's without looking bolted
// on. Dumping the banner's right-hand panel cell by cell gives:
//
//   * the field is BLUE, attribute 0x10 - black on blue - so a blank cell in it
//     is a space that reads as solid blue;
//   * there are TWO single-line boxes of the same size, the outer offset by
//     (+2, -1) from the inner, both in 0x10. That doubled outline is the whole
//     signature of the look;
//   * body text is 0x17, light grey on blue, and headings 0x1f, white on blue;
//   * the shadow is a bright-blue block: 0xdb at 0x19 down the right, 0xdf at
//     0x09 along the bottom, with 0xdc softening the corners.
//
// The colours it picks out with - 0x1b bright cyan, 0x1a bright green, 0x1e
// yellow - are the banner's own accents, used here for the same job.
constexpr uint8_t kField = 0x10;      // black on blue: the panel itself
constexpr uint8_t kBody = 0x17;       // light grey on blue
constexpr uint8_t kBright = 0x1F;     // white on blue
constexpr uint8_t kAccent = 0x1B;     // bright cyan on blue
constexpr uint8_t kChosen = 0x1E;     // yellow on blue
constexpr uint8_t kShadow = 0x09;     // bright blue on black
constexpr uint8_t kShadowIn = 0x19;   // bright blue on blue
constexpr uint8_t kEdge = 0x01;       // blue on black

// CP437: the single-line box, the blocks, and the pointer.
constexpr uint8_t kH = 0xC4, kV = 0xB3, kTL = 0xDA, kTR = 0xBF;
constexpr uint8_t kBL = 0xC0, kBR = 0xD9;
constexpr uint8_t kBlock = 0xDB, kUpper = 0xDF, kLower = 0xDC;
constexpr uint8_t kPointer = 0x10;

// The inner box. The outer is this offset by (+2, -1), as the banner has it.
constexpr int kInX = 10;
constexpr int kInY = 7;
constexpr int kInW = 58;
constexpr int kInH = 12;
constexpr int kOutX = kInX + 2;
constexpr int kOutY = kInY - 1;

// Text sits inside BOTH boxes, inset one cell - which is where the banner puts
// it: two columns right of the outer box's left edge.
constexpr int kTextX = kOutX + 2;

void say(TextScreen& ts, int col, int row, const char* s, uint8_t attr) {
    for (int i = 0; s[i]; ++i) {
        ts.put(col + i, row, static_cast<uint8_t>(s[i]), attr);
    }
}

void box(TextScreen& ts, int x, int y, int w, int h, uint8_t attr) {
    for (int i = 1; i < w - 1; ++i) {
        ts.put(x + i, y, kH, attr);
        ts.put(x + i, y + h - 1, kH, attr);
    }
    for (int j = 1; j < h - 1; ++j) {
        ts.put(x, y + j, kV, attr);
        ts.put(x + w - 1, y + j, kV, attr);
    }
    ts.put(x, y, kTL, attr);
    ts.put(x + w - 1, y, kTR, attr);
    ts.put(x, y + h - 1, kBL, attr);
    ts.put(x + w - 1, y + h - 1, kBR, attr);
}

}  // namespace

int editionAnswerRow(int index) { return kInY + 5 + index * 2; }

int editionAnswerIndex(Edition e) {
    return e == Edition::kShareware ? 1 : 0;
}

Edition editionForAnswer(int index) {
    return index == 1 ? Edition::kShareware : Edition::kRegistered;
}

void buildEditionPrompt(TextScreen& ts, int selected) {
    for (int row = 0; row < kTextRows; ++row) {
        for (int col = 0; col < kTextCols; ++col) ts.put(col, row, ' ', 0x0F);
    }

    // The blue field is the union of the two boxes, which is what makes the
    // outer stick out top-right and the inner bottom-left.
    for (int row = kOutY; row < kInY + kInH; ++row) {
        for (int col = kInX; col < kOutX + kInW; ++col) {
            ts.put(col, row, ' ', kField);
        }
    }
    // The banner caps the field's top corners with a half block rather than
    // ending it square - two cells, NOT a bar across the top, which is what an
    // earlier reading of this drew before the cells were dumped.
    ts.put(kInX - 1, kOutY, kLower, kEdge);
    ts.put(kOutX + kInW, kOutY, kLower, kEdge);

    box(ts, kOutX, kOutY, kInW, kInH, kField);
    box(ts, kInX, kInY, kInW, kInH, kField);

    // The shadow: a bright-blue block down the right and along the bottom.
    for (int row = kOutY + 1; row < kInY + kInH; ++row) {
        ts.put(kOutX + kInW, row, kBlock, kShadowIn);
    }
    ts.put(kOutX + kInW, kInY + kInH, kLower, kShadowIn);
    for (int col = kInX + 1; col <= kOutX + kInW; ++col) {
        ts.put(col, kInY + kInH, kUpper, kShadow);
    }

    say(ts, kOutX + (kInW - 13) / 2, kOutY, " Tubes Setup ", kBright);
    say(ts, kTextX, kInY + 1, "Tubes shipped in two editions.", kBody);
    say(ts, kTextX, kInY + 2, "Which one would you like to play?", kBright);

    for (int i = 0; i < kEditionAnswers; ++i) {
        const bool on = i == selected;
        ts.put(kTextX, editionAnswerRow(i), on ? kPointer : ' ',
               on ? kChosen : kField);
        say(ts, kEditionAnswerCol, editionAnswerRow(i), kEditionAnswerText[i],
            on ? kChosen : kBody);
    }

    say(ts, kTextX, kInY + kInH - 2,
        "Up/Down to choose, Enter to play.", kAccent);
}

}  // namespace tubes
