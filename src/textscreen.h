#pragma once

// A VGA text-mode screen, rendered as pixels.
//
// WHY THIS EXISTS. `TUBESEND.BIN` is the shareware edition's sign-off, and it
// is not a bitmap: it is a raw dump of text-mode video memory, 80 x 23 cells of
// {character, attribute}, which the original `Move`s to segment 0xB800 and then
// quits. See `tools/bin_decode.py` and the notes.
//
// THE ONE PLACE THIS PORT INVENTS A PRESENTATION, and it is unavoidable. The
// original does not DISPLAY this screen, it LEAVES it behind: the program ends,
// the banner stays on the shell, and the DOS prompt lands in the two rows the
// dump deliberately does not cover - which is why 3,680 bytes is 23 rows and
// not 25. A windowed SDL port has no shell to leave anything on. So the port
// renders it, holds it until a key, and then exits, and that hold is the
// invented part. Everything else - the cells, the attributes, the palette, the
// geometry - is the file's.
//
// Writing it to the real terminal instead was considered and rejected. It
// reproduces the mechanism and loses the picture: the art is drawn in box and
// half-block glyphs with drop shadows, so it needs the 8x16 cell, the CP437
// shapes and the 16-colour palette, and any terminal with its own font metrics
// or a themed palette breaks the tiling. Below 80 columns it wraps and is
// destroyed outright.
//
// GEOMETRY. 80 x 25 cells of 8 x 16 is 640 x 400 - exactly twice the port's
// 320 x 200 in both axes, so the aspect is identical and `presentRect` returns
// the SAME destination rectangle. Window scaling, 4:3 correction, fullscreen
// and scanlines therefore all apply unchanged, without the display code
// learning about a second resolution.

#include <cstdint>
#include <string>
#include <vector>

#include "gfx.h"

namespace tubes {

constexpr int kTextCols = 80;
constexpr int kTextRows = 25;
constexpr int kGlyphW = 8;
constexpr int kGlyphH = 16;
constexpr int kTextScreenW = kTextCols * kGlyphW;   // 640
constexpr int kTextScreenH = kTextRows * kGlyphH;   // 400

// Modern DOS 8x16, generated into `cp437_font.cpp`. One byte per scanline,
// MSB leftmost.
extern const uint8_t kCp437Font8x16[256][16];

// One cell of text-mode memory, in the order the hardware stores it.
struct TextCell {
    uint8_t ch = ' ';
    uint8_t attr = 0x07;   // bit 7 blink, 6-4 background, 3-0 foreground
};

// The 16 text-mode colours, as 6-bit VGA DAC values - the same 0..63 range the
// game's own `.PAL` resources use, so one palette type serves both and the
// existing fade works on this screen without knowing what it is.
//
// These are the standard EGA/VGA text palette: the low eight are the dim
// colours, the high eight their bright pairs, with the usual brown-not-dark-
// yellow at index 6.
extern const uint8_t kTextPalette[16][3];

// Builds a 256-entry palette whose first 16 entries are the text colours, so a
// `TextScreen` renders through the same `toRgba` path as everything else.
Palette textPalette();

// ---------------------------------------------------------------------------
// The fake DOS prompt
// ---------------------------------------------------------------------------
//
// THE PORT'S OWN INVENTION, and the only content on this screen that is not in
// the file. It is here rather than buried in the SDL layer so the invariant
// below can be tested.
//
// The justification is the file's own shape: 3,680 bytes is 23 rows of a
// 25-row screen, and it stops short precisely so the shell's next line lands
// under the art instead of scrolling it. The blank rows are a hole cut for the
// prompt. Drawing one fills the gap the dump was designed around, which is why
// the screen reads as finished rather than as cropped.
constexpr int kPromptRow = 23;
constexpr uint8_t kPromptAttr = 0x07;       // light grey on black, the DOS default

// The VGA text cursor is an underline on the cell's last two scanlines,
// toggling every 16 vertical retraces. Written as a retrace count because that
// is the unit this project measures every hold in - `23e7:0024` is `Delay(n)`
// at n/70 s - rather than as milliseconds.
constexpr int kCursorBlinkRetraces = 16;
constexpr int kCursorTopRow = 14;

class TextScreen {
public:
    TextScreen() : cells_(kTextCols * kTextRows) {}

    // Loads a `.BIN` dump: `rows` rows of 80 cells, character then attribute.
    // Rows beyond it are left blank, which is what the original leaves for the
    // shell prompt - the file really is 23 rows of a 25-row screen.
    //
    // Returns false if the blob is not a whole number of 80-cell rows, rather
    // than rendering a torn screen.
    bool loadBin(const std::vector<uint8_t>& blob);

    int rowsLoaded() const { return rowsLoaded_; }

    TextCell at(int col, int row) const;
    void put(int col, int row, uint8_t ch, uint8_t attr);

    // Renders to an 8-bit indexed 640 x 400 buffer, palette indices 0..15.
    //
    // Blink (attribute bit 7) is NOT animated. On the real thing this screen is
    // static the instant the program exits, so nothing on it ever blinks - and
    // `TUBESEND.BIN` sets the bit nowhere, which `bin_decode.py INFO` confirms:
    // all twelve of its attributes are below 0x80. The bit is masked off rather
    // than being allowed to pick a background from the high eight colours.
    void render(std::vector<uint8_t>& out) const;

private:
    std::vector<TextCell> cells_;
    int rowsLoaded_ = 0;
};

// ---------------------------------------------------------------------------
// The first-run edition prompt
// ---------------------------------------------------------------------------
//
// THE PORT'S OWN SCREEN, top to bottom, and it is deliberately the only one
// that does not pretend otherwise.
//
// The port has to know which edition the player owns, and it cannot find out:
// `TUBES.RES` is byte-identical between the shareware and registered releases,
// so an install carries no evidence of which one it came from. The edition
// lives only in the executable, and the port IS the executable. So it asks.
//
// WHY THIS IS A TEXT SCREEN, and why that was the right of four candidates.
// The other three put the question inside the game - on the title screen in
// the `Exit Tubes?` page's idiom, or on the projector slide in Lanny's mouth.
// Both look authentic and both are a small lie: the 1994 game never asked this
// and had no reason to, so a screen that speaks AS the game is claiming
// something about the original. A setup screen before the graphics come up is
// outside the program's world, which is exactly where a question the original
// never asked belongs - and it is still period-honest, because it is what
// `SETUP.EXE` would have looked like.
//
// It borrows `TUBESEND.BIN`'s presentation - CP437 in 80 x 25 at 8 x 16, the
// sixteen text-mode attributes - so it needs no renderer of its own and
// inherits the window scaling, the 4:3 correction and the fade with it.
//
// The layout lives here rather than in `main.cpp` so it can be tested without
// SDL, the same reason `kPromptRow` does.

constexpr int kEditionAnswers = 2;
// Index 0 is registered and 1 shareware, matching `Edition`'s own order.
extern const char* const kEditionAnswerText[kEditionAnswers];

// Draws the whole prompt into `ts`, with answer `selected` marked. Everything
// on the screen is redrawn, so moving the selection is one more call.
void buildEditionPrompt(TextScreen& ts, int selected);

// Where the answers land, so a test can read them back off the screen without
// knowing the box's geometry.
int editionAnswerRow(int index);
constexpr int kEditionAnswerCol = 22;

}  // namespace tubes
