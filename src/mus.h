// .MUS playback: the FM sequencer from DRIVERS.RES:FMMUSIC.DRV.
//
// Format and driver offsets are documented in docs/reversing-notes.md.
// tools/mus_decode.py is the reference implementation; this is a port of it,
// and `tubes-port --dump-regs` exists so the two register streams can be
// diffed against each other.
//
// Nothing here embeds game data. The frequency and attenuation tables are
// read out of the user's own FMMUSIC.DRV at runtime.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "res.h"

namespace tubes {

// PIT divisor 0x4000, written by the driver's init at 0x92a.
constexpr double kMusTickHz = 1193182.0 / 16384.0;   // 72.8271 Hz

constexpr int kMusChannels = 11;        // 6 melodic + BD, SD, TT, CY, HH
constexpr int kMusPercussionBase = 6;
constexpr uint8_t kMusMarker = 0xf5;
constexpr int kMusHeaderLen = 32;

// Command classes, selected by the high nibble at driver offset 0x83d.
enum : uint8_t {
    kMusInstrument = 0x10,
    kMusVolume = 0x20,
    kMusNoteOn = 0x30,
    kMusNoteOff = 0x40,
    kMusPitchBend = 0x50,
    kMusEnd = 0xf0,
};

struct MusEvent {
    uint8_t delta = 0;      // ticks to wait before this event
    uint8_t cmd = 0;        // command class, low nibble masked off
    uint8_t channel = 0;
    uint8_t argc = 0;
    uint8_t args[12] = {};
};

// Splits a .MUS into events. Returns false with `error` set on anything
// unexpected; a well-formed resource consumes every byte to EOF.
bool parseMus(const Bytes& data, std::vector<MusEvent>& out, std::string& error);

// The lookup tables FMMUSIC.DRV keeps in its own code segment.
struct FmTables {
    uint8_t opOffset[kMusChannels];     // 0x3a: channel -> operator slot
    uint8_t rhythmMask[5];              // 0x45: channel-6 -> reg 0xBD bit
    uint8_t attenuation[128];           // 0x29a: volume -> TL attenuation
    uint16_t fnum[192];                 // 0x458: 12 pitch classes x 16 steps
    uint8_t block[96];                  // 0x5d8: note -> OPL block
    uint8_t pitchClass[96];             // 0x638: note -> fnum table row
    uint8_t defaultInstr[6][11];        // 0x85: six startup patches

    // `drv` is the decompressed FMMUSIC.DRV from DRIVERS.RES.
    static bool load(const Bytes& drv, FmTables& out, std::string& error);
};

// Where register writes go. Lets the sequencer drive either a real emulated
// chip or a log used for verification.
class RegisterSink {
public:
    virtual ~RegisterSink() = default;
    virtual void write(uint8_t reg, uint8_t value) = 0;
};

// Collects writes instead of sounding them.
class RegisterLog : public RegisterSink {
public:
    struct Write { uint32_t tick; uint8_t reg; uint8_t value; };

    void write(uint8_t reg, uint8_t value) override {
        writes.push_back({tick, reg, value});
    }
    uint32_t tick = 0;
    std::vector<Write> writes;
};

// A transcription of the driver's tick routine at 0x810 and the five command
// handlers it dispatches to. Deliberately keeps the original's asymmetries:
// note-on writes a frequency for channels 6 and 8 but not 7, 9 or 10, and
// note-off for channel 6 goes through the rhythm bit rather than reg 0xB0.
class MusSequencer {
public:
    MusSequencer(const FmTables& tables, RegisterSink& sink)
        : t_(tables), sink_(sink) {}

    // Resets the chip and loads the driver's default patches, then arms
    // `events` from the top. The caller owns `events`.
    void start(const std::vector<MusEvent>* events);

    // One 72.827 Hz tick. Safe to call with no song armed.
    void tick();

    // Set once the song has run past its 0xf0. Playback continues, because
    // 0xf0 rewinds rather than stopping.
    bool looped() const { return looped_; }

    void silence();

private:
    bool dispatch();            // false = stop processing for this tick
    void loadInstrument(int ch, const uint8_t* regs);
    void setVolume(int ch, uint8_t vol);
    void setFrequency(int ch, int note, uint8_t keyOn);
    void noteOn(int ch, uint8_t midiNote);
    void noteOff(int ch);
    void init();

    const FmTables& t_;
    RegisterSink& sink_;
    const std::vector<MusEvent>* events_ = nullptr;

    size_t index_ = 0;
    uint8_t delay_ = 0;         // cs:0x31
    bool looped_ = false;       // cs:0x32

    uint8_t savedTl_[0x16] = {};            // cs:0x4d, by operator slot
    uint8_t savedB0_[kMusChannels] = {};    // cs:0x64, reg 0xB0 sans key-on
    uint8_t volume_[kMusChannels] = {};     // cs:0x6f
    uint8_t rhythm_ = 0;                    // cs:0x4a, shadow of reg 0xBD
};

}  // namespace tubes
