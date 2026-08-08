#include "textscreen.h"

namespace tubes {

// The 16 text-mode colours in 6-bit DAC units. That is how the VGA stores
// them and how the game's `.PAL` resources store them. The pattern is the EGA
// one: bits are blue/green/red at 2/3 intensity for 0..7, and full intensity
// for 8..15. Index 6 is brown instead of dark yellow. That is the only
// irregularity, and it is in the hardware, not in this table.
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
    // Expanded to 8 bits here, because that is what `Palette` is: a 256-entry
    // RGB palette expanded from the 6-bit VGA values. Returning the raw 6-bit
    // values looked harmless, and it was the cause of the exit screen's flicker
    // - the callers compensated with a `* 255 / 63` of their own, which then
    // also hit `fadePalette`'s output, which is already expanded. See gfx.h.
    Palette p{};
    for (int i = 0; i < 16; ++i) {
        for (int c = 0; c < 3; ++c) {
            p.rgb[i][c] = static_cast<uint8_t>(kTextPalette[i][c] * 255 / 63);
        }
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
// The version picker - see the header for what it is and why it looks like
// this
// ---------------------------------------------------------------------------

const char* const kEditionAnswerText[kEditionAnswers] = {
    "Registered",
    "Shareware",
};

namespace {

// `SETUP.EXE`'s own screen, dumped from text memory rather than copied from
// a screenshot. The game's installer was run under DOSBox-X; `0xB8000` read
// back, so every attribute below is the byte the original writes:
//
//   0x70  black on light grey    the field, and the body of everything
//   0x7f  white on light grey    the lit edges of a bevel, and value text
//   0x78  dark grey on grey      a button label that is not selected
//   0x7b  bright cyan on grey    a heading
//   0x7e  yellow on grey         the key hints along the bottom
//   0x1b  bright cyan on blue    text inside the blue title box
//   0x71  blue on light grey     the half-blocks that cap the blue box
//   0x01  blue on black          the same, where the box's shadow falls
//   0x00  black on black         the drop shadow itself
//
// The bevel is the signature: a box's top and left are white and its bottom
// and right are black, so it reads as raised. Shadows are black cells offset
// by (+1, +1). That is a completely different language from `TUBESEND.BIN`'s
// blue panels. The banner is the game signing off. `SETUP.EXE` is the game
// asking a question, and this screen asks a question.
constexpr uint8_t kField = 0x70;      // black on light grey
constexpr uint8_t kLit = 0x7F;        // white on grey: a bevel's lit edge
constexpr uint8_t kDim = 0x78;          // dark grey on grey: an idle label
constexpr uint8_t kHead = 0x7B;         // bright cyan on grey
constexpr uint8_t kHint = 0x7E;         // yellow on grey
constexpr uint8_t kOnBlue = 0x1B;       // bright cyan on blue
constexpr uint8_t kBlueTop = 0x71;      // blue on grey
constexpr uint8_t kBlueLow = 0x01;      // blue on black, where the shadow is
constexpr uint8_t kShadow = 0x00;     // black on black

constexpr uint8_t kH = 0xC4, kV = 0xB3, kTL = 0xDA, kTR = 0xBF;  // Box edges
constexpr uint8_t kBL = 0xC0, kBR = 0xD9;
constexpr uint8_t kUpper = 0xDF, kLower = 0xDC;                  // Half blocks
constexpr uint8_t kArrowUp = 0x18, kArrowDown = 0x19;            // Arrows

// The two panels, at `SETUP.EXE`'s own coordinates: a 26-column pane on the
// left, a 53-column one on the right, one blank column between them carrying
// the left pane's shadow.
constexpr int kLeftX = 0, kLeftW = 26;
constexpr int kRightX = 27, kRightW = 53;

// The buttons, 22 wide at column 2, three rows each and stacked touching. That
// matches the installer's `Select Graphics` / `Select Music` / ... column.
constexpr int kBtnX = 2, kBtnW = 22, kBtnH = 3;
constexpr int kBtnY = 8;                   // First button row

void say(TextScreen& ts, int col, int row, const char* s, uint8_t attr) {
    for (int i = 0; s[i]; ++i) {
        ts.put(col + i, row, static_cast<uint8_t>(s[i]), attr);
    }
}

int len(const char* s) {
    int n = 0;
    while (s[n]) ++n;
    return n;
}

void sayMid(TextScreen& ts, int x, int w, int row, const char* s, uint8_t attr) {
    say(ts, x + (w - len(s)) / 2, row, s, attr);
}

// A raised box: top and left lit, bottom and right in shadow.
void bevel(TextScreen& ts, int x, int y, int w, int h) {
    for (int i = 1; i < w - 1; ++i) {
        ts.put(x + i, y, kH, kLit);
        ts.put(x + i, y + h - 1, kH, kField);
    }
    for (int j = 1; j < h - 1; ++j) {
        ts.put(x, y + j, kV, kLit);
        ts.put(x + w - 1, y + j, kV, kField);
    }
    ts.put(x, y, kTL, kLit);
    ts.put(x + w - 1, y, kTR, kField);
    ts.put(x, y + h - 1, kBL, kLit);
    ts.put(x + w - 1, y + h - 1, kBR, kField);
}

// The blue title block: a row of lower half-blocks, `rows` rows of blue, then
// a row of upper half-blocks whose lower halves are the shadow. The right-hand
// shadow column starts one row down, which is what makes it read as lifted.
void blueBlock(TextScreen& ts, int x, int y, int w, int rows) {
    for (int i = 0; i < w; ++i) ts.put(x + i, y, kLower, kBlueTop);
    for (int j = 1; j <= rows; ++j) {
        for (int i = 0; i < w; ++i) ts.put(x + i, y + j, ' ', 0x10);
        ts.put(x + w, y + j, ' ', kShadow);
    }
    ts.put(x, y + rows + 1, kUpper, kBlueTop);
    for (int i = 1; i < w; ++i) ts.put(x + i, y + rows + 1, kUpper, kBlueLow);
    ts.put(x + w, y + rows + 1, ' ', kShadow);
}

}  // namespace

int editionAnswerRow(int index) { return kBtnY + index * kBtnH + 1; }

int editionAnswerIndex(Edition e) {
    return e == Edition::kShareware ? 1 : 0;
}

Edition editionForAnswer(int index) {
    return index == 1 ? Edition::kShareware : Edition::kRegistered;
}

void buildEditionPrompt(TextScreen& ts, int selected) {
    for (int row = 0; row < kTextRows; ++row) {
        for (int col = 0; col < kTextCols; ++col) ts.put(col, row, ' ', kField);
    }

    bevel(ts, kLeftX, 0, kLeftW, kTextRows);
    bevel(ts, kRightX, 0, kRightW, kTextRows);
    // The left pane's shadow falls in the column between the two panes.
    for (int row = 1; row < kTextRows; ++row) {
        ts.put(kLeftX + kLeftW, row, ' ', kShadow);
    }

    // The installer's own title block, in the port's name rather than the
    // publisher's. This is not Absolute Magic's SETUP and must not say it is.
    blueBlock(ts, 2, 1, 21, 3);
    sayMid(ts, 2, 21, 2, "SetUp!", kOnBlue);
    sayMid(ts, 2, 21, 3, "Tubes Port", kOnBlue);
    sayMid(ts, 2, 21, 4, "Choose Version", kOnBlue);

    for (int i = 0; i < kEditionAnswers; ++i) {
        const int y = kBtnY + i * kBtnH;
        bevel(ts, kBtnX, y, kBtnW, kBtnH);
        sayMid(ts, kBtnX, kBtnW, y + 1, kEditionAnswerText[i],
               i == selected ? kLit : kDim);
    }

    // The hints, in the installer's words and its yellow.
    ts.put(3, kTextRows - 3, kArrowUp, kHint);
    ts.put(4, kTextRows - 3, ',', kHint);
    ts.put(5, kTextRows - 3, kArrowDown, kHint);
    say(ts, 6, kTextRows - 3, "/Version Select", kHint);
    say(ts, 2, kTextRows - 2, "ESC/Exit   ENTER/Play", kHint);

    // The right pane mirrors `Current Set-Up`. It shows what the highlighted
    // button means, so the choice is legible before the player makes it.
    blueBlock(ts, kRightX + 2, 1, kRightW - 5, 1);
    sayMid(ts, kRightX + 2, kRightW - 5, 2, "Current Set-Up", kOnBlue);

    const bool sw = selected == 1;
    struct Line { int row; const char* text; uint8_t attr; };
    const Line kLines[] = {
        {5,  "Version", kHead},
        {7,  sw ? "Shareware Version" : "Registered Version", kLit},
        {9,  "Waves", kHead},
        {11, sw ? "25" : "75", kLit},
        {13, "Special Atoms", kHead},
        {15, sw ? "None in normal play" : "Bonus and AntiMatter", kLit},
        {17, "Extras", kHead},
        {19, sw ? "Preview Registered" : "None", kLit},
    };
    for (const Line& l : kLines) {
        sayMid(ts, kRightX + 1, kRightW - 2, l.row, l.text, l.attr);
    }
}

}  // namespace tubes
