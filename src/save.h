// `TUBES.SAV`, the saved-game file - `1b2e:000a`, `1b2e:00ac`, `1000:2dd0`.
//
// Platform-agnostic: format and rules only. The caller draws and does I/O, the
// same split `hiscore.h` uses.
//
// Unlike `TUBES.HSC` the game DOES ship one of these, zero-filled - which is
// why an untouched install lists five `( Available )` slots rather than
// failing to find a file.
#ifndef TUBES_SAVE_H
#define TUBES_SAVE_H

#include <cstdint>
#include <string>
#include <vector>

namespace tubes {

// ---------------------------------------------------------------------------
// The file
// ---------------------------------------------------------------------------
//
// `1b2e:00ac`, the writer, is the whole of it:
//
//     DS:0x1ae3 := Random(254) + 1;  DS:0x1cc3 := Random(254) + 1
//     Assign(f, 'TUBES.SAV');  Rewrite(f, 1)
//     BlockWrite(f, DGROUP:0x1928, $1e0)      { bank 0 }
//     BlockWrite(f, DGROUP:0x1b08, $1e0)      { bank 1 }
//     Close(f)
//
// and `1b2e:000a`, the reader, `FillChar`s both banks with zero BEFORE reading,
// so a missing or short file leaves every record with a zero length byte. That
// is exactly the test the menu makes - `1b2e:5427` is `CMP byte ptr [0x1928],0`
// - so "empty" needs no separate flag.
//
// Bank 0 is Endurance and bank 1 is Wave, the same order `TUBES.HSC` uses and
// the same order the menu lists.
constexpr int kSaveSlotBytes = 0x50;       // 80
constexpr int kSaveBankBytes = 0x1e0;      // 480 = 6 * 80
constexpr int kSaveFileBytes = kSaveBankBytes * 2;

// **Six records, five of them slots.** `1b2e:00ac` writes the whole 0x1e0 and
// the menu lists five, so the sixth is spare - the same arrangement as
// `TUBES.HSC`'s eleventh entry. It is not dead space: see `kSaveNonceOffset`.
constexpr int kSaveSlots = 6;
constexpr int kSaveSlotsShown = 5;

enum class SaveBank { kEndurance = 0, kWave = 1 };

// ---------------------------------------------------------------------------
// The record, from `1000:2dd0`'s save arm at `1000:3660`
// ---------------------------------------------------------------------------
//
// The save screen fills a scratch record at `DGROUP:0x1ce8` one field at a
// time out of the session frame, and then `Move`s all 0x50 bytes of it into
// the chosen slot. So the record is not inferred from samples - it is a list
// of sixteen stores, and this is that list:
//
//     +0x00  string[30]  the description the player types    (31 bytes)
//     +0x1f  u32         score               -0x153 / -0x151, two stores
//     +0x23  byte        continues left      -0x14f
//     +0x24  u16         total chains        -0x14e
//     +0x26  byte        wave                -0x170
//     +0x27  byte        drops remaining     -0x17e
//     +0x28  byte        chains this wave    -0x17c
//     +0x29  u16         velocity            -0x180
//     +0x2b  byte        dispense interval   -0x181
//     +0x2c  byte        chain target        -0x183
//     +0x2d  byte        atom target         -0x182
//     +0x2e  byte        colour target       -0x184
//     +0x2f  byte        crystals            -0x185
//     +0x30  byte        marked              -0x186
//     +0x31  byte        pre-fill            -0x17b
//
// The last six are `WaveProgress`, the counters `1000:a4cd` seeds and
// `1000:a616` steps, so **a saved game restores the progression and not just
// the wave number.** That is what makes a loaded wave 40 harder than a warped
// one, and it is why the level-warp experiments saw wave-6 counters on a wave
// 75 save.
//
// Nothing is written past +0x31; the remaining 30 bytes are whatever the
// scratch record held. Both captured files have them zero, but the load arm
// copies a whole slot INTO the scratch, so a load followed by a save to a
// different slot would carry the first slot's tail across. Modelled rather
// than assumed away.
constexpr int kSaveDescField = 31;         // Pascal `string[30]`
constexpr int kSaveDescMax = 30;
constexpr int kSaveTailOffset = 0x32;
constexpr int kSaveTailBytes = kSaveSlotBytes - kSaveTailOffset;   // 30

// `1b2e:00ac` stores `Random(254) + 1` at bank + 0x1bb on every save, in both
// banks, whether or not either changed. That is record 5 (0-based) field
// +0x2b - **the sixth record's interval byte**. The reader loads it into a
// local and never reads it again.
//
// So it is a nonce with no consumer, not a checksum. The earlier note that it
// "differs between samples including in the bank that stayed empty, so it is
// not a checksum" was right, and this is why.
constexpr int kSaveNonceOffset = 0x1bb;
constexpr int kSaveNonceSlot = 5;
constexpr int kSaveNonceMax = 254;

struct SaveSlot {
    std::string description;
    uint32_t score = 0;
    int continuesLeft = 0;
    int totalChains = 0;
    int wave = 0;
    int drops = 0;
    int chainsThisWave = 0;
    int velocity = 0;
    int interval = 0;
    int chainTarget = 0;
    int atomTarget = 0;
    int colourTarget = 0;
    int crystals = 0;
    int marked = 0;
    int preFill = 0;

    // The raw 31-byte description field and the unwritten tail, so a file
    // round-trips byte for byte - the same reason `HiScoreEntry` carries its
    // field rather than just a string.
    uint8_t descField[kSaveDescField] = {};
    uint8_t tail[kSaveTailBytes] = {};

    // `1b2e:5427`: a record is a live save when its length byte is not zero.
    bool live() const { return descField[0] != 0; }

    // Pascal string assignment: the length and the characters, nothing else.
    void setDescription(const std::string& s);
};

struct SaveBankData {
    SaveSlot slots[kSaveSlots];
};

struct SaveFile {
    SaveBankData bank[2];

    SaveBankData& operator[](SaveBank b) { return bank[static_cast<int>(b)]; }
    const SaveBankData& operator[](SaveBank b) const {
        return bank[static_cast<int>(b)];
    }
};

// Both return false on a wrong-sized buffer rather than half-filling the
// table. A file that is the right size but all zeroes is VALID and decodes to
// five empty slots per bank, which is what a fresh install holds.
bool decodeSaves(const std::vector<uint8_t>& raw, SaveFile& out);
std::vector<uint8_t> encodeSaves(const SaveFile& in);

// `1b2e:00ac`'s two `Random(254) + 1` stores, kept together because they are
// one step of the save and because they consume two of the session's random
// numbers - saving perturbs the sequence, in the original too.
void stampSaveNonces(SaveFile& f, int a, int b);

// The description the save screen shows for a slot the player leaves blank -
// `1000:2dd0`'s own strings.
constexpr const char* kSaveAvailable = "( Available )";
constexpr const char* kSaveUndescribed = "Undescribed";

// ---------------------------------------------------------------------------
// The save screen, `1000:2dd0`'s F2 arm
// ---------------------------------------------------------------------------
//
// `2321:049b(30, i * 17 + 50, 30, 3, text)` per row, in SCRIPT.816 - the same
// cursive the high-score screens use.
constexpr int kSaveRowX = 30;              // 0x1e
constexpr int kSaveRowY0 = 50;             // 0x32
constexpr int kSaveRowPitch = 17;          // 0x11
constexpr uint8_t kSaveRowColour = 30;     // 0x1e
constexpr int kSaveDetailX = 242;          // 0xf2

inline int saveRowY(int slot) {            // slot is 1-based, as the loop is
    return slot * kSaveRowPitch + kSaveRowY0;
}

}  // namespace tubes

#endif  // TUBES_SAVE_H
