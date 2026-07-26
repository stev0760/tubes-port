#include "mus.h"

#include <algorithm>

namespace tubes {
namespace {

// Argument counts per command class, from the dispatcher at 0x83d.
int argCount(uint8_t cls) {
    switch (cls) {
        case kMusInstrument: return 12;
        case kMusVolume: return 1;
        case kMusNoteOn: return 1;
        case kMusNoteOff: return 0;
        case kMusPitchBend: return 2;
        default: return -1;
    }
}

uint16_t readU16(const Bytes& b, size_t off) {
    return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}

}  // namespace

bool parseMus(const Bytes& data, std::vector<MusEvent>& out, std::string& error) {
    out.clear();
    if (data.size() < kMusHeaderLen || data[0] != kMusMarker) {
        error = "not a .MUS: missing 0xf5 marker";
        return false;
    }

    size_t pos = kMusHeaderLen;
    for (;;) {
        if (pos + 2 > data.size()) {
            error = "event stream ran off the end";
            return false;
        }
        MusEvent ev;
        ev.delta = data[pos];
        const uint8_t cmd = data[pos + 1];
        pos += 2;

        if (cmd == kMusEnd) {
            ev.cmd = kMusEnd;
            out.push_back(ev);
            // A well-formed resource ends exactly here; this is the oracle
            // that proved the framing in the first place.
            if (pos != data.size()) {
                error = "trailing bytes after end-of-song";
                return false;
            }
            return true;
        }

        const uint8_t cls = cmd & 0xf0;
        const uint8_t chan = cmd & 0x0f;
        const int n = argCount(cls);
        if (n < 0) {
            error = "unknown command";
            return false;
        }
        if (chan >= kMusChannels) {
            error = "channel out of range";
            return false;
        }
        if (pos + static_cast<size_t>(n) > data.size()) {
            error = "arguments run past EOF";
            return false;
        }
        ev.cmd = cls;
        ev.channel = chan;
        ev.argc = static_cast<uint8_t>(n);
        std::copy(data.begin() + pos, data.begin() + pos + n, ev.args);
        out.push_back(ev);
        pos += n;
    }
}

bool FmTables::load(const Bytes& drv, FmTables& out, std::string& error) {
    // The tables end where the code at 0x698 begins, which is what pins
    // their sizes; see docs/reversing-notes.md.
    if (drv.size() < 0x698) {
        error = "FMMUSIC.DRV is too short to hold its tables";
        return false;
    }
    std::copy(drv.begin() + 0x3a, drv.begin() + 0x3a + kMusChannels, out.opOffset);
    std::copy(drv.begin() + 0x45, drv.begin() + 0x45 + 5, out.rhythmMask);
    std::copy(drv.begin() + 0x29a, drv.begin() + 0x29a + 128, out.attenuation);
    for (int i = 0; i < 192; ++i) out.fnum[i] = readU16(drv, 0x458 + i * 2);
    std::copy(drv.begin() + 0x5d8, drv.begin() + 0x5d8 + 96, out.block);
    std::copy(drv.begin() + 0x638, drv.begin() + 0x638 + 96, out.pitchClass);
    for (int i = 0; i < 6; ++i) {
        std::copy(drv.begin() + 0x85 + i * 11, drv.begin() + 0x85 + (i + 1) * 11,
                  out.defaultInstr[i]);
    }

    // Cheap checks against what the driver code requires of each table. A
    // wrong offset would otherwise produce plausible-sounding noise.
    static const uint8_t kSlots[6] = {0x00, 0x01, 0x02, 0x08, 0x09, 0x0a};
    if (!std::equal(kSlots, kSlots + 6, out.opOffset)) {
        error = "operator table is not the AdLib slot map";
        return false;
    }
    static const uint8_t kMasks[5] = {0x10, 0x08, 0x04, 0x02, 0x01};
    if (!std::equal(kMasks, kMasks + 5, out.rhythmMask)) {
        error = "rhythm mask table is not BD/SD/TT/CY/HH";
        return false;
    }
    if (out.attenuation[127] != 0) {
        error = "attenuation table does not bottom out at 0";
        return false;
    }
    return true;
}

// -- 0x142: reset the chip and install the default patches ------------------
void MusSequencer::init() {
    for (int reg = 0x01; reg <= 0xf5; ++reg) {
        sink_.write(static_cast<uint8_t>(reg), 0x00);
    }
    sink_.write(0x01, 0x20);            // unlock waveform select
    for (int ch = 0; ch < 6; ++ch) loadInstrument(ch, t_.defaultInstr[0]);
    for (int ch = 6; ch < kMusChannels; ++ch) {
        loadInstrument(ch, t_.defaultInstr[ch - 5]);
    }
    rhythm_ = 0xe0;                     // AM depth + vibrato depth + rhythm on
    sink_.write(0xbd, rhythm_);

    std::fill(std::begin(savedB0_), std::end(savedB0_), 0);
    std::fill(std::begin(volume_), std::end(volume_), 0);
}

// -- 0x1e8 ------------------------------------------------------------------
void MusSequencer::loadInstrument(int ch, const uint8_t* r) {
    const uint8_t off = t_.opOffset[ch];
    // The driver's two-operator test is `ch <= 6`, so the bass drum counts
    // as two-operator even though it is a percussion voice. That boundary
    // is one higher than the melodic/percussion split.
    const bool twoOp = ch <= 6;

    sink_.write(0x20 + off, r[0]);
    if (twoOp) sink_.write(0x23 + off, r[1]);

    sink_.write(0x40 + off, r[2]);
    savedTl_[off] = r[2];
    if (twoOp) {
        sink_.write(0x43 + off, r[3]);
        savedTl_[off + 3] = r[3];
    }

    sink_.write(0x60 + off, r[4]);
    if (twoOp) sink_.write(0x63 + off, r[5]);

    sink_.write(0x80 + off, r[6]);
    if (twoOp) sink_.write(0x83 + off, r[7]);

    sink_.write(0xe0 + off, r[8]);
    if (twoOp) {
        sink_.write(0xe3 + off, r[9]);
        sink_.write(0xb0 + ch, 0x00);
        sink_.write(0xc0 + ch, r[10]);
    }
}

// -- 0x378 and the recompute at 0x31a ---------------------------------------
void MusSequencer::setVolume(int ch, uint8_t vol) {
    volume_[ch] = vol;
    const uint8_t atten = t_.attenuation[vol & 0x7f];
    const uint8_t off = t_.opOffset[ch];

    auto scaled = [&](int slot) -> uint8_t {
        const uint8_t v = savedTl_[slot];
        const int level = (v & 0x3f) + atten;
        return static_cast<uint8_t>((v & 0xc0) | std::min(level, 0x3f));
    };

    sink_.write(0x40 + off, scaled(off));
    if (ch <= 6) sink_.write(0x43 + off, scaled(off + 3));
}

// -- 0x698 ------------------------------------------------------------------
void MusSequencer::setFrequency(int ch, int note, uint8_t keyOn) {
    // Pitch bend is dead code on AdLib: the note-on at 0x3a2 hardcodes the
    // centre value 0x2000, so the bend arithmetic collapses to note * 16.
    int ax = note * 16;
    ax = std::max(0, std::min(ax, 0x5ff));

    const int row = t_.pitchClass[ax >> 4];
    const int frac = ((ax << 1) & 0x1f) >> 1;
    const uint16_t raw = t_.fnum[row * 16 + frac];

    int block = t_.block[ax >> 4] - 1;
    // The sign bit of a table entry is a flag, not magnitude: negative means
    // "use the block as-is", positive means "halve me and drop a block".
    int32_t f = (raw & 0x8000) ? static_cast<int32_t>(raw) - 0x10000 : raw;
    if (f < 0) ++block;
    if (block < 0) {
        ++block;
        f >>= 1;
    }

    sink_.write(0xa0 + ch, static_cast<uint8_t>(f & 0xff));
    const uint8_t val =
        static_cast<uint8_t>(((f >> 8) & 0x03) + (block << 2) + keyOn);
    sink_.write(0xb0 + ch, val);
    savedB0_[ch] = static_cast<uint8_t>(val - keyOn);
}

// -- 0x388 ------------------------------------------------------------------
void MusSequencer::noteOn(int ch, uint8_t midiNote) {
    int note = static_cast<int>(midiNote) - 12;
    if (note < 0) note = 0;

    if (ch < kMusPercussionBase) {
        setFrequency(ch, note, 0x20);
    } else if (ch == 6 || ch == 8) {
        // Bass drum and tom are pitched, but key-on must stay clear in
        // rhythm mode - the 0xBD bit triggers them instead.
        setFrequency(ch, note, 0x00);
    }
    if (ch >= kMusPercussionBase) {
        rhythm_ |= t_.rhythmMask[ch - kMusPercussionBase];
        sink_.write(0xbd, rhythm_);
    }
}

// -- 0x420 ------------------------------------------------------------------
void MusSequencer::noteOff(int ch) {
    if (ch < kMusPercussionBase) {
        sink_.write(0xb0 + ch, savedB0_[ch]);
    } else {
        rhythm_ = static_cast<uint8_t>(
            rhythm_ & ~t_.rhythmMask[ch - kMusPercussionBase]);
        sink_.write(0xbd, rhythm_);
    }
}

void MusSequencer::start(const std::vector<MusEvent>* events) {
    events_ = events;
    index_ = 0;
    delay_ = 0;
    looped_ = false;
    init();
}

void MusSequencer::silence() {
    events_ = nullptr;
    init();
}

bool MusSequencer::dispatch() {
    const MusEvent& ev = (*events_)[index_];
    ++index_;
    switch (ev.cmd) {
        case kMusInstrument: loadInstrument(ev.channel, ev.args + 1); break;
        case kMusVolume: setVolume(ev.channel, ev.args[0]); break;
        case kMusNoteOn: noteOn(ev.channel, ev.args[0]); break;
        case kMusNoteOff: noteOff(ev.channel); break;
        case kMusPitchBend: break;      // 0x454 is `add si,2 / ret`
        case kMusEnd:
            // 0xf0 rewinds and keeps playing; it does not stop.
            index_ = 0;
            delay_ = 0;
            looped_ = true;
            return false;
        default: break;
    }
    return true;
}

// -- 0x810 ------------------------------------------------------------------
void MusSequencer::tick() {
    if (events_ == nullptr || events_->empty()) return;

    if (delay_ != 0) {
        if (--delay_ != 0) return;      // 0x82b/0x830: still counting down
        if (!dispatch()) return;        // 0x835: the pending event fires
    }
    // 0x8ad: read deltas, firing every event that carries a delta of 0 so
    // any number of them can share one tick.
    for (;;) {
        if (index_ >= events_->size()) { events_ = nullptr; return; }
        const uint8_t d = (*events_)[index_].delta;
        if (d != 0) { delay_ = d; return; }
        if (!dispatch()) return;
    }
}

}  // namespace tubes
