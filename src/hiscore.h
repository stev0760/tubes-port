// `TUBES.HSC`, the high score table - `1000:96db`.
//
// The file is not shipped with the game; it is created the first time a score
// qualifies, which is why a fresh install has only `TUBES.SAV`. The format was
// recovered by warping a save to wave 75, clearing it, and entering a known
// string so the record was unmistakable in the bytes.
//
// Platform-agnostic: format, rules and layout. The caller draws and does I/O.
#ifndef TUBES_HISCORE_H
#define TUBES_HISCORE_H

#include <cstdint>
#include <string>
#include <vector>

namespace tubes {

// ---------------------------------------------------------------------------
// The format
// ---------------------------------------------------------------------------
//
//     record = 36 bytes
//       [0]      name length      Pascal `string[30]`
//       [1..30]  name             30 characters
//       [31]     padding          the record is word aligned
//       [32..35] score            u32, written as two words lo then hi
//
// `1b2e:0243` settles all of it independently of the file: it fills the banks
// longhand, and the addresses it writes give the stride (0x1634 - 0x1610 =
// 0x24), the score offset (0x1630 - 0x1610 = 0x20) and the 30-character string
// bound it passes to the copy.
//
// A bank is 0x18c = 396 bytes = 11 records, and the file is two banks back to
// back. `1000:96db`'s tail writes them in this order:
//
//     BlockWrite(f, DGROUP 0x1610, 0x18c)    { bank 0 }
//     BlockWrite(f, DGROUP 0x179c, 0x18c)    { bank 1 }
//
// and its head loads `0x1610` when `DS:0x1d4e = 1`, so **bank 0 is Endurance
// and bank 1 is Wave**. Those are the two the title screen draws under the
// headings ` Chains` and ` Wave`.
constexpr int kHiScoreRecord = 36;
constexpr int kHiScoreNameField = 31;
constexpr int kHiScoreBankBytes = 396;      // 0x18c
constexpr int kHiScoreFileBytes = kHiScoreBankBytes * 2;

// **Eleven slots, ten of them the table.** The display loop runs `1..10` and
// stops, so the eleventh is an overflow slot that an insert pushes an entry
// into rather than off the end. That is visible in a captured file: an
// untouched bank has ten names and an empty eleventh, and a bank that has just
// taken an entry has all eleven filled.
constexpr int kHiScoreSlots = 11;
constexpr int kHiScoreShown = 10;

// `1000:9718`: the typing loop rejects a character once the name reaches 25,
// even though the field holds 30.
constexpr int kHiScoreNameMax = 25;

enum class HiScoreBank { kEndurance = 0, kWave = 1 };

// The name is stored as a Pascal ShortString in a fixed 31-byte field, and
// **the original never clears the tail**. Assigning a shorter name writes the
// length byte and the characters and leaves the rest of the field alone, so
// residue from whatever was there before survives on disk.
//
// That is not a curiosity - it is what proves the sentinel. In a captured
// file the byte just past a 20-character player name is `)`, which is
// character 21 of `([C+C GAMES FACTORY])`: the record was seeded with the
// sentinel and typed over. Modelling the field faithfully takes the port from
// one differing byte to none.
struct HiScoreEntry {
    std::string name;
    uint32_t score = 0;
    uint8_t field[kHiScoreNameField] = {};   // the raw 31 bytes, tail included

    bool empty() const { return name.empty() && score == 0; }

    // Assign a name the way Pascal does: length and characters only.
    void setName(const std::string& s) {
        name = s.substr(0, kHiScoreNameMax);
        for (size_t i = 0; i < name.size(); ++i) {
            field[i] = static_cast<uint8_t>(name[i]);
        }
    }
    // Seed the whole field, as `1000:9705` does with the sentinel before the
    // player types over it.
    void seedField(const std::string& s) {
        for (int i = 0; i < kHiScoreNameField; ++i) {
            field[i] = i < static_cast<int>(s.size())
                           ? static_cast<uint8_t>(s[i]) : 0;
        }
    }
};

// One bank. `rows[0]` is the top of the table.
struct HiScoreBankData {
    HiScoreEntry rows[kHiScoreSlots];
};

struct HiScoreFile {
    HiScoreBankData bank[2];

    HiScoreBankData& operator[](HiScoreBank b) {
        return bank[static_cast<int>(b)];
    }
    const HiScoreBankData& operator[](HiScoreBank b) const {
        return bank[static_cast<int>(b)];
    }
};

// The twenty names the binary ships, packed as Pascal ShortStrings at file
// offset 0x00d621 right after the `TUBES.HSC` literal - ten per bank, in the
// order the file writes them.
//
// `1b2e:0243` writes the 1000, 900 ... 100 ladder **longhand**, one literal
// store per record, rather than looping - so the ladder is a coincidence of
// twenty hand-written constants, not a generated series. Reproducing it with
// a loop gives the same bytes and is what this port does, but the note matters
// if a later edition ever ships a different ladder.
extern const char* const kHiScoreDefaults[2][kHiScoreShown];
constexpr uint32_t kHiScoreTopDefault = 1000;
constexpr uint32_t kHiScoreStep = 100;

// A table with the shipped defaults and an empty eleventh slot in each bank.
HiScoreFile defaultHiScores();

// Both return false on a malformed buffer rather than half-filling the table.
bool decodeHiScores(const std::vector<uint8_t>& raw, HiScoreFile& out);
std::vector<uint8_t> encodeHiScores(const HiScoreFile& in);

// `1000:9709`: the score has to beat the LOWEST DISPLAYED entry, slot 10 - not
// the eleventh. `1000:8da5`'s "High Score!" banner tests the same slot, which
// is why the two agree.
bool qualifies(const HiScoreBankData& b, uint32_t score);

// Insert-and-shift, returning the row the entry landed on, or -1 if it did not
// qualify. The original bubble-sorts descending and then finds its own entry
// by a sentinel name; the result is the same and this is the readable version.
int insertHiScore(HiScoreBankData& b, const std::string& name, uint32_t score);

// ---------------------------------------------------------------------------
// The screen, `1000:96db`
// ---------------------------------------------------------------------------
constexpr int kHsTitleY = 6;
constexpr int kHsRuleY = 9;
constexpr uint8_t kHsTitleColour = 47;      // 0x2f

// `2321:060b(10, 37, 299, 118, 111)` - the panel behind the list.
constexpr int kHsPanelX = 10;
constexpr int kHsPanelY = 37;
constexpr int kHsPanelW = 299;
constexpr int kHsPanelH = 118;
constexpr uint8_t kHsPanelColour = 111;     // 0x6f

// `2321:049b(16, i * 12 + 21, 30, 3, name)` and the score at x 220.
constexpr int kHsNameX = 16;                // 0x10
constexpr int kHsScoreX = 220;              // 0xdc
constexpr int kHsRowY0 = 21;                // 0x15
constexpr int kHsRowPitch = 12;             // 0xc
constexpr uint8_t kHsRowColour = 30;        // 0x1e

inline int hiScoreRowY(int row) {           // row is 1-based, as the loop is
    return row * kHsRowPitch + kHsRowY0;
}

// The score is `Str(score:10)` - `2591:005d` converts the longint and
// `2591:0180(10, ...)` formats it into a TEN-CHARACTER field, so it is
// RIGHT-JUSTIFIED with leading blanks and x 220 is where the field starts,
// not where the digits do. Both screens do this.
constexpr int kHsScoreWidth = 10;

// Row 1 lands at y 33, four pixels above the panel at y 37. That is not a
// misreading: `1b2e:61b6`, found later, draws its rows from the same two
// literals over the same panel, so the original genuinely puts the top row
// slightly proud of the panel. Do not nudge either constant.

// `1000:96db`'s tail. When the typing loop ends the row is redrawn in the
// settled colour with no cursor, `CLAP.SFX` plays, and only THEN - after
// `23e7:0024(0x78)`, 120 retraces - is the record copied into the bank and the
// file written. So the applause belongs to the entry screen as well as the
// viewer, and the screen holds while it plays.
constexpr int kHsCommitRetraces = 0x78;
constexpr float kHsCommitSeconds =
    static_cast<float>(kHsCommitRetraces) / 70.0f;

constexpr const char* kHsTitle = "Congratulations! High Score!";
constexpr const char* kHsRule = "_________________________";

// `1000:9684`. The new entry is seeded with this and the code then finds it
// again by name after sorting - a sentinel, not something a player sees.
constexpr const char* kHsSentinel = "([C+C GAMES FACTORY])";

// `CLAP.SFX` plays when the high score viewer opens - the player's account,
// and now `1b2e:61b6` below, which plays it and keeps replaying it.
//
// **It is a sound, not an animation.** `1b2e:0656`'s clap arm - the one gated
// on `DS:0x20c8`, which draws `CLAP1..3.GFX` at (267, 100) over `BOOKS.GFX` -
// is a DIFFERENT thing and remains unattributed. It was tempting to join the
// two because both say "clap", and that would have put an animated professor
// on a screen the player says does not have one; the viewer draws no professor.
constexpr int kClapY = 100;             // 0x64, vs 121 for the pointer pose
constexpr int kClapFrames = 3;
constexpr int kClapRetraces = 10;
constexpr const char* kClapSound = "CLAP.SFX";

// ---------------------------------------------------------------------------
// The high score VIEWER, `1b2e:61b6` - the menu item, not the entry screen
// ---------------------------------------------------------------------------
//
// Found by asking `MapProgram`'s dump which function references the strings
// "Endurance Mode High Scores" and "Wave Mode High Scores". The previous
// session looked for it with `FindScalarRefs` on the bank addresses and
// concluded it was "unfound, not absent" - correctly, and for the stated
// reason: the scan only sees code that names the banks, and this function does
// name them, but the search that would have found it was a STRING search. The
// lesson from CLAUDE.md applies exactly - when a search comes back empty,
// suspect the search.
//
// **It is ONE table at a time, and a key moves between them.** The original
// draws Endurance onto video page 0 and Wave onto page 1 up front, then flips
// pages with `2321:01b5`, so there is no redraw when the player presses a key.
// Redrawing gives the same picture.
//
//     SetVisualPage(0); SetActivePage(0)
//     { draw the Endurance table }
//     SetActivePage(1)
//     { draw the Wave table }
//     PlayMusic(CLASS.MUS); PlaySound(CLAP.SFX)
//     wait                                { page 0, Endurance }
//     if key <> ESC then begin
//       SetVisualPage(1); wait            { page 1, Wave }
//     end
//
// So ESC leaves at once from the first page, any other key advances to the
// second, and any key on the second leaves. The two names for that are "a key
// toggles the tables" and "a key pages through them"; the code is the second,
// and only the first page treats ESC specially.
//
// Each page draws, in this order:
//
//     Draw(0, 12, DS:0x2058)               { BLACKBRD.GFX, as the classroom }
//     FillRect(10, 37, 299, 118, 111)      { the panel - see below }
//     SetFont(SCRIPT.816, 8, 16, 8)
//     for i := 1 to 10 do begin
//       WriteAt(16, i * 12 + 21, 30, 3, name[i])
//       Str(score[i]:10, s); WriteAt(220, i * 12 + 21, 30, 3, s)
//     end
//     DrawMasked(57, 26, DS:0x2060)        { SLIDEBAR.GFX, the roller bar }
//     SetFont(STARTREK.816, 8, 16, 8)
//     WriteCentred(0, 319, 14, 159, 3, title)
//
// **THE CHALKBOARD CARRIES NO WRITING.** `BLACKBRD.GFX` has equations chalked
// on it, and `2321:060b(10, 37, 299, 118, 111)` - the same panel the entry
// screen puts up - covers all but its frame. The port drew the board bare for
// one revision and the player caught it. The panel is the entry screen's, so
// the two screens are the same picture with a different heading.
constexpr int kHsViewTitleY = 14;           // 0xe
constexpr uint8_t kHsViewTitleColour = 159; // 0x9f, pushed as -0x61
constexpr int kHsViewBarX = 57;             // 0x39
constexpr int kHsViewBarY = 26;             // 0x1a
constexpr const char* kHsViewMusic = "CLASS.MUS";   // `DS:0x212c`

// `DS:0x2120` is `CLAP.SFX`, and the wait loop restarts it whenever the effects
// voice reports itself idle - so the applause LOOPS for as long as the screen
// is up rather than playing once.
//
//     if not SoundBusy then PlaySound(CLAP.SFX)

// Titles, in page order: Endurance is page 0 and Wave is page 1, which agrees
// with bank 0 being Endurance.
constexpr const char* kHsViewTitle[2] = {"Endurance Mode High Scores",
                                         "Wave Mode High Scores"};

// The give-up: `0x1a4` iterations of `23e7:0024(5)` is 2100 retraces at Mode
// X's 70 Hz - **thirty seconds**, the same as the briefing's. A timeout does
// what a key does: page 0 advances to page 1, page 1 leaves.
constexpr int kHsViewIterations = 0x1a4;
constexpr int kHsViewDelay = 5;
constexpr float kHsViewSeconds =
    static_cast<float>(kHsViewIterations * kHsViewDelay) / 70.0f;

// The typing cursor, `1000:9757`: a 4 x 4 block at
// `(len * 8 + 18, row + 5)` whose colour ramps 0x91..0x9e and back, one step a
// frame - so it pulses rather than blinks.
constexpr int kHsCursorSize = 4;
constexpr int kHsCursorDX = 18;             // 0x12
constexpr int kHsCursorDY = 5;
constexpr uint8_t kHsCursorBase = 0x90;
constexpr int kHsCursorMin = 1;
constexpr int kHsCursorMax = 14;            // stops at 0xf, so 14 is the peak

}  // namespace tubes

#endif  // TUBES_HISCORE_H
