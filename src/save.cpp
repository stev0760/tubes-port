#include "save.h"

#include <cstring>

namespace tubes {
namespace {

// The record is little-endian throughout, as everything Turbo Pascal writes
// with BlockWrite is.
uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
void wr16(uint8_t* p, int v) {
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
}
void wr32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xff);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xff);
}

void decodeSlot(const uint8_t* r, SaveSlot& s) {
    std::memcpy(s.descField, r, kSaveDescField);
    std::memcpy(s.tail, r + kSaveTailOffset, kSaveTailBytes);
    const int n = r[0] <= kSaveDescMax ? r[0] : kSaveDescMax;
    s.description.assign(reinterpret_cast<const char*>(r + 1),
                         static_cast<size_t>(n));
    s.score          = rd32(r + 0x1f);
    s.continuesLeft  = r[0x23];
    s.totalChains    = rd16(r + 0x24);
    s.wave           = r[0x26];
    s.drops          = r[0x27];
    s.chainsThisWave = r[0x28];
    s.velocity       = rd16(r + 0x29);
    s.interval       = r[0x2b];
    s.chainTarget    = r[0x2c];
    s.atomTarget     = r[0x2d];
    s.colourTarget   = r[0x2e];
    s.crystals       = r[0x2f];
    s.marked         = r[0x30];
    s.preFill        = r[0x31];
}

void encodeSlot(const SaveSlot& s, uint8_t* r) {
    std::memset(r, 0, kSaveSlotBytes);
    std::memcpy(r, s.descField, kSaveDescField);
    std::memcpy(r + kSaveTailOffset, s.tail, kSaveTailBytes);
    wr32(r + 0x1f, s.score);
    r[0x23] = static_cast<uint8_t>(s.continuesLeft);
    wr16(r + 0x24, s.totalChains);
    r[0x26] = static_cast<uint8_t>(s.wave);
    r[0x27] = static_cast<uint8_t>(s.drops);
    r[0x28] = static_cast<uint8_t>(s.chainsThisWave);
    wr16(r + 0x29, s.velocity);
    r[0x2b] = static_cast<uint8_t>(s.interval);
    r[0x2c] = static_cast<uint8_t>(s.chainTarget);
    r[0x2d] = static_cast<uint8_t>(s.atomTarget);
    r[0x2e] = static_cast<uint8_t>(s.colourTarget);
    r[0x2f] = static_cast<uint8_t>(s.crystals);
    r[0x30] = static_cast<uint8_t>(s.marked);
    r[0x31] = static_cast<uint8_t>(s.preFill);
}

}  // namespace

void SaveSlot::setDescription(const std::string& s) {
    description = s.substr(0, kSaveDescMax);
    descField[0] = static_cast<uint8_t>(description.size());
    for (size_t i = 0; i < description.size(); ++i) {
        descField[i + 1] = static_cast<uint8_t>(description[i]);
    }
}

bool decodeSaves(const std::vector<uint8_t>& raw, SaveFile& out) {
    if (raw.size() != static_cast<size_t>(kSaveFileBytes)) return false;
    for (int b = 0; b < 2; ++b) {
        for (int i = 0; i < kSaveSlots; ++i) {
            decodeSlot(raw.data() + b * kSaveBankBytes + i * kSaveSlotBytes,
                       out.bank[b].slots[i]);
        }
    }
    return true;
}

std::vector<uint8_t> encodeSaves(const SaveFile& in) {
    std::vector<uint8_t> raw(static_cast<size_t>(kSaveFileBytes), 0);
    for (int b = 0; b < 2; ++b) {
        for (int i = 0; i < kSaveSlots; ++i) {
            encodeSlot(in.bank[b].slots[i],
                       raw.data() + b * kSaveBankBytes + i * kSaveSlotBytes);
        }
    }
    return raw;
}

std::string saveSlotLabel(SaveBank bank, const SaveSlot& s) {
    if (!s.live()) return std::string();
    std::string label = s.description;
    if (static_cast<int>(label.size()) < kSaveLabelPad) {
        label.append(static_cast<size_t>(kSaveLabelPad) - label.size(), ' ');
    } else {
        label = label.substr(0, kSaveLabelPad);
    }
    if (bank == SaveBank::kWave) {
        return label + kSaveLabelWave + std::to_string(s.wave);
    }
    return label + kSaveLabelChainsGap + std::to_string(s.chainsThisWave) +
           kSaveLabelChains;
}

void stampSaveNonces(SaveFile& f, int a, int b) {
    // The nonce IS the sixth record's interval byte - it is stored through the
    // same field, at bank + 0x1bb.
    f.bank[0].slots[kSaveNonceSlot].interval = a;
    f.bank[1].slots[kSaveNonceSlot].interval = b;
}

}  // namespace tubes
