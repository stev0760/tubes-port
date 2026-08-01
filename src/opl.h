// OPL2 synthesis, and the audio device that drives the .MUS sequencer.
//
// The chip itself is Nuked-OPL3 (third_party/nuked-opl3), used unmodified.
// Emulating the hardware is a solved problem and not what this project is
// reverse engineering; the part that came off the original binary is the
// sequencer in mus.cpp, which produces the register writes fed in here.
//
// An OPL3 with its "new" bit clear behaves as an OPL2, and the driver never
// touches the OPL3 register set, so no compatibility shim is needed.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mus.h"
#include "sfx.h"

// opl3.h declares `typedef struct _opl3_chip opl3_chip;`, so the tag is what
// can be forward-declared here; including the C header would leak into every
// translation unit that only wants MusicPlayer.
struct _opl3_chip;

namespace tubes {

// Wraps the emulator and accepts the sequencer's register writes directly.
class OplChip : public RegisterSink {
public:
    explicit OplChip(int sampleRate);
    ~OplChip();

    void write(uint8_t reg, uint8_t value) override;

    // Fills `frames` stereo int16 sample pairs.
    void render(int16_t* out, int frames);

    int sampleRate() const { return sampleRate_; }

private:
    std::unique_ptr<_opl3_chip> chip_;
    int sampleRate_;
};

// Owns an SDL audio device, an OplChip and a sequencer, and steps the
// sequencer at 72.827 Hz from inside the audio callback so music timing is
// driven by the sample clock rather than the frame rate.
class MusicPlayer {
public:
    ~MusicPlayer();

    // Loads FMMUSIC.DRV from the already-open DRIVERS.RES and opens an audio
    // device. Returns false with `error` set; the caller may carry on
    // silently, since music is not required to play the game.
    bool open(const Archive& drivers, std::string& error, int sampleRate = 44100);

    // `data` is a decompressed .MUS resource. Replaces whatever is playing.
    bool play(const Bytes& data, std::string& error);

    void stop();
    void setPaused(bool paused);
    bool isOpen() const { return deviceId_ != 0; }

    // Opens the device without starting any music, so sound effects can play
    // on their own. Harmless if the device is already open.
    bool openSilent(std::string& error, int sampleRate = 44100);

    // Starts `s` on the single effects voice, cutting off whatever was
    // playing - which is what `SBSOUND.DRV` does. `s` must outlive the call;
    // the caller owns the decoded sounds.
    void playSound(const Sound* s);
    // `[DS:0x231a]`, StopSound - and the port needs it for a second reason
    // the original never had: a screen that owns its own `Sound` objects as
    // locals MUST silence the voice before they go out of scope, or the audio
    // thread reads freed memory. The cutscene crashed exactly that way.
    void stopSound();

    // `DS:0x230a`, the driver's "is a sound still playing" vector. The high
    // score viewer polls it to keep the applause looping.
    bool soundBusy() const { return sfxVoice_.busy(); }

    // `DS:0x22ce`, which returns 0xff once the song has been round once. The
    // banners at `1000:5e0b` and `1000:5e78` end on it as well as on a key,
    // which is what "the Wave Complete banner ends when its music does" is.
    // The sequencer already tracks it - `looped_` is the driver's `cs:0x32`.
    // Read from the game thread while the audio callback writes it; a plain
    // bool that only ever goes false-to-true needs nothing more.
    bool songLooped() const { return seq_ && seq_->looped(); }

    int deviceRate() const { return deviceRate_; }

    // Renders a song to a mono/stereo buffer without an audio device, for
    // offline verification. `seconds` bounds songs, which loop forever.
    static bool renderOffline(const Archive& drivers, const Bytes& song,
                              double seconds, int sampleRate,
                              std::vector<int16_t>& out, std::string& error);

    // Runs the sequencer with no chip attached and returns every register
    // write it makes. This is what `--dump-regs` prints so the stream can be
    // diffed against tools/mus_decode.py.
    static bool logRegisters(const Archive& drivers, const Bytes& song,
                             int ticks, RegisterLog& out, std::string& error);

private:
    static void audioCallback(void* userdata, uint8_t* stream, int len);
    void mix(int16_t* out, int frames);

    uint32_t deviceId_ = 0;
    int deviceRate_ = 44100;
    // The effects voice is mixed on top of the music in the same callback, so
    // there is one device and one clock for both.
    SfxPool sfxVoice_;
    FmTables tables_{};
    std::unique_ptr<OplChip> chip_;
    std::unique_ptr<MusSequencer> seq_;
    std::vector<MusEvent> events_;
    double tickAccumulator_ = 0.0;
    double samplesPerTick_ = 0.0;
};

}  // namespace tubes
