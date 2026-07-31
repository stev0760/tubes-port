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
//       [0]      name length      (Pascal ShortString)
//       [1..31]  name             31 bytes, NUL padded
//       [32..35] score            u32 little endian
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
// even though the field on disk is 31 bytes.
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
// order the file writes them. The scores are NOT stored: both banks default to
// the same 1000, 900 ... 100 ladder, so they are generated.
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

constexpr const char* kHsTitle = "Congratulations! High Score!";
constexpr const char* kHsRule = "_________________________";

// `1000:9684`. The new entry is seeded with this and the code then finds it
// again by name after sorting - a sentinel, not something a player sees.
constexpr const char* kHsSentinel = "([C+C GAMES FACTORY])";

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
