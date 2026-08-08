#include "hiscore.h"

#include <algorithm>
#include <cstring>

namespace tubes {

// Read out of the binary at 0x00d621, packed with no padding, ten per bank.
// The block ends at 0x00d723 where code resumes with `ENTER 0x80,0`, which
// brackets it - the walk cannot have run past the end.
const char* const kHiScoreDefaults[2][kHiScoreShown] = {
    // bank 0 - Endurance, the ` Chains` table
    {"Ken Heckbert", "Kelly Rogers", "Glenda Moore", "Terry Herrin",
     "Rik Pierce", "Doug Howell", "Joe Siegler", "Bob Mandel",
     "Larry Nelson", "Adam Pedersen"},
    // bank 1 - Wave
    {"Ronald Davis", "Jason Blochowiak", "Matt Long", "Dan Linton",
     "Scott Miller", "Evan Heckbert", "Grant Heckbert", "Casey Rogers",
     "Micheal Moore", "Mike Bartelt"},
};

HiScoreFile defaultHiScores() {
    HiScoreFile f;
    for (int b = 0; b < 2; ++b) {
        for (int i = 0; i < kHiScoreShown; ++i) {
            f.bank[b].rows[i].seedField(kHiScoreDefaults[b][i]);
            f.bank[b].rows[i].name = kHiScoreDefaults[b][i];
            f.bank[b].rows[i].score =
                kHiScoreTopDefault - static_cast<uint32_t>(i) * kHiScoreStep;
        }
        // The eleventh is the overflow slot and ships empty.
        f.bank[b].rows[kHiScoreShown] = HiScoreEntry{};
    }
    return f;
}

bool decodeHiScores(const std::vector<uint8_t>& raw, HiScoreFile& out) {
    if (raw.size() != static_cast<size_t>(kHiScoreFileBytes)) return false;

    for (int b = 0; b < 2; ++b) {
        for (int i = 0; i < kHiScoreSlots; ++i) {
            const size_t off = static_cast<size_t>(b) * kHiScoreBankBytes +
                               static_cast<size_t>(i) * kHiScoreRecord;
            const uint8_t len = raw[off];
            // A length past the field would mean the record layout is wrong,
            // and a partly-filled table is worse than refusing the file.
            if (len > kHiScoreNameField) return false;
            std::memcpy(out.bank[b].rows[i].field, &raw[off + 1],
                        kHiScoreNameField);
            out.bank[b].rows[i].name.assign(
                reinterpret_cast<const char*>(&raw[off + 1]), len);
            out.bank[b].rows[i].score =
                static_cast<uint32_t>(raw[off + 32]) |
                (static_cast<uint32_t>(raw[off + 33]) << 8) |
                (static_cast<uint32_t>(raw[off + 34]) << 16) |
                (static_cast<uint32_t>(raw[off + 35]) << 24);
        }
    }
    return true;
}

std::vector<uint8_t> encodeHiScores(const HiScoreFile& in) {
    std::vector<uint8_t> raw(static_cast<size_t>(kHiScoreFileBytes), 0);
    for (int b = 0; b < 2; ++b) {
        for (int i = 0; i < kHiScoreSlots; ++i) {
            const size_t off = static_cast<size_t>(b) * kHiScoreBankBytes +
                               static_cast<size_t>(i) * kHiScoreRecord;
            const HiScoreEntry& e = in.bank[b].rows[i];
            const size_t len =
                std::min<size_t>(e.name.size(), kHiScoreNameField);
            raw[off] = static_cast<uint8_t>(len);
            // The whole field, tail residue and all - not just the name.
            std::memcpy(&raw[off + 1], e.field, kHiScoreNameField);
            raw[off + 32] = static_cast<uint8_t>(e.score & 0xff);
            raw[off + 33] = static_cast<uint8_t>((e.score >> 8) & 0xff);
            raw[off + 34] = static_cast<uint8_t>((e.score >> 16) & 0xff);
            raw[off + 35] = static_cast<uint8_t>((e.score >> 24) & 0xff);
        }
    }
    return raw;
}

bool qualifies(const HiScoreBankData& b, uint32_t score) {
    // Slot 10, the lowest displayed entry. `1000:9709` compares against it
    // rather than the eleventh, and `1000:8da5`'s banner tests the same slot.
    return score > b.rows[kHiScoreShown - 1].score;
}

int insertHiScore(HiScoreBankData& b, const std::string& name,
                  uint32_t score) {
    if (!qualifies(b, score)) return -1;

    int at = kHiScoreShown - 1;
    for (int i = 0; i < kHiScoreShown; ++i) {
        if (score > b.rows[i].score) { at = i; break; }
    }
    // Shift down into the eleventh slot rather than off the end. A bank that has
    // taken an entry holds eleven names on disk; an untouched one holds ten and
    // a blank.
    for (int i = kHiScoreSlots - 1; i > at; --i) {
        b.rows[i] = b.rows[i - 1];
    }
    // `1000:9705` seeds the new record with the sentinel and the player types
    // over it, so the sentinel's tail survives past the typed name.
    b.rows[at] = HiScoreEntry{};
    b.rows[at].seedField(kHsSentinel);
    b.rows[at].setName(name);
    b.rows[at].score = score;
    return at;
}

}  // namespace tubes
