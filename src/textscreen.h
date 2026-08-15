#pragma once

// A VGA text-mode screen, rendered as pixels.
//
// Why this exists. `TUBESEND.BIN` is the shareware edition's sign-off, and it
// is not a bitmap: it is a raw dump of text-mode video memory, 80 x 23 cells of
// {character, attribute}, which the original `Move`s to segment 0xB800 and then
// quits. See `tools/bin_decode.py` and the notes.
//
// The presentation here is invented, and unavoidably so. The original does not
// display this screen; it leaves it behind. The program ends, the banner stays
// on the shell, and the DOS prompt lands in the two rows the dump deliberately
// does not cover - which is why 3,680 bytes is 23 rows, not 25. A windowed SDL
// port has no shell to leave anything on, so the port renders it, holds it
// until a key, and then exits. That hold is the invented part. Everything else
// - the cells, the attributes, the palette, the geometry - is the file's.
//
// Writing it to the real terminal was considered and rejected. That would
// reproduce the mechanism but lose the picture. The art is drawn in box and
// half-block glyphs with drop shadows, so it needs the 8x16 cell, the CP437
// shapes, and the 16-colour palette. Any terminal with its own font metrics or
// a themed palette breaks the tiling. Below 80 columns it wraps and is
// destroyed outright.
//
// Geometry. 80 x 25 cells of 8 x 16 is 640 x 400 - exactly twice the port's
// 320 x 200 in both axes, so the aspect is identical and `presentRect` returns
// the same destination rectangle. Window scaling, 4:3 correction, fullscreen,
// and scanlines therefore all apply unchanged, without the display code
// learning about a second resolution.

#include <cstdint>
#include <string>
#include <vector>

#include "edition.h"
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
// colours, the high eight their bright pairs, with the usual
// brown-not-dark-yellow at index 6.
extern const uint8_t kTextPalette[16][3];

// Builds a 256-entry palette whose first 16 entries are the text colours, so a
// `TextScreen` renders through the same `toRgba` path as everything else.
Palette textPalette();

// ---------------------------------------------------------------------------
// The fake DOS prompt
// ---------------------------------------------------------------------------
//
// The port's own invention, and the only content on this screen that is not in
// the file. It lives here rather than in the SDL layer so the invariant below
// can be tested.
//
// The file's shape justifies it. 3,680 bytes is 23 rows of a 25-row screen,
// and it stops short so the shell's next line lands under the art instead of
// scrolling it. The blank rows are a hole cut for the prompt. Drawing a prompt
// fills that gap, which is why the screen reads as finished, not cropped.
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
    // Returns false if the blob is not a whole number of 80-cell rows, instead
    // of rendering a torn screen.
    bool loadBin(const std::vector<uint8_t>& blob);

    int rowsLoaded() const { return rowsLoaded_; }

    TextCell at(int col, int row) const;
    void put(int col, int row, uint8_t ch, uint8_t attr);

    // Renders to an 8-bit indexed 640 x 400 buffer, palette indices 0..15.
    //
    // Blink (attribute bit 7) is not animated. On the real thing this screen is
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
// The version picker
// ---------------------------------------------------------------------------
//
// The port's own screen, top to bottom, and it does not pretend otherwise.
//
// The port cannot work out which edition to be. `TUBES.RES` is byte-identical
// between the shareware and registered releases, so an install carries no
// evidence of which one it came from - the edition lives only in the
// executable, and the port is the executable. So it asks.
//
// It asks every launch, and it asks "which would you like to play?" rather
// than "which copy do you have?". The second frames the edition as a fact
// about the player, and it is not one: both editions are preserved and
// downloadable, the registered one having been believed lost for years until a
// copy surfaced and reached archive.org. Nobody is stuck with one of them.
//
// Asking a preference is also what makes every launch reasonable rather than
// nagging. It is a launcher choice, like picking a difficulty, and it costs
// one keypress because the cursor starts on the remembered answer. It is
// skipped entirely for `--shareware` / `--registered`, which is the
// developer's and a capture script's way past it.
//
// Why a text screen, and not one inside the game. Putting the question on the
// title screen in the `Exit Tubes?` page's idiom, or on the projector slide in
// Lanny's mouth, looks right and is a small lie: the 1994 game never asked
// this and had no reason to, so a screen speaking as the game claims something
// about the original. A setup screen before the graphics come up sits outside
// the program's world, which is where a question the original never asked
// belongs - and it is still period-honest, being what `SETUP.EXE` would have
// looked like.
//
// The look to copy is `SETUP.EXE`, the game's installer, and not
// `TUBESEND.BIN`, its sign-off. The banner is the game saying goodbye,
// `SETUP.EXE` is the game asking the player a question, and this screen asks a
// question.
//
// The look was not taken from a screenshot either. `SETUP.EXE` was run under
// DOSBox-X and its text memory read back from 0xB8000, so every attribute is
// the byte the original writes: a light-grey field, raised bevels with white
// top-and-left edges and black bottom-and-right, black drop shadows offset by
// (+1, +1), a blue title block capped with half blocks, bright-cyan headings,
// white values, yellow key hints, and a button label that is white when chosen
// and dark grey when not. `textscreen.cpp` carries the table, and
// `testTheEditionPromptStaysInsideSetupsPalette` stops it drifting.
//
// The two-pane layout is the installer's as well: its buttons on the left, its
// `Current Set-Up` summary on the right, here saying what the highlighted
// version actually gives you.
//
// It shares the exit screen's renderer, so it needs no display code of its own
// and inherits window scaling, 4:3 correction, fullscreen, and the fade.
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

// The row an edition sits on, and back again. Two functions rather than a bare
// `== kShareware ? 1 : 0` at each site: the picker preselects the remembered
// answer, and a row index that disagreed with the edition it came from would
// start the cursor on the wrong line - quietly, and only for the edition that
// is not the default.
int editionAnswerIndex(Edition e);
Edition editionForAnswer(int index);

}  // namespace tubes
