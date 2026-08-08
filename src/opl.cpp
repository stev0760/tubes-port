#include "opl.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstring>

extern "C" {
#include "opl3.h"
}

namespace tubes {
namespace {

constexpr const char* kFmDriver = "FMMUSIC.DRV";

bool loadTables(const Archive& drivers, FmTables& tables, std::string& error) {
    Bytes drv;
    if (!drivers.read(kFmDriver, drv, error)) {
        error = std::string("cannot read ") + kFmDriver + ": " + error;
        return false;
    }
    return FmTables::load(drv, tables, error);
}

}  // namespace

OplChip::OplChip(int sampleRate)
    : chip_(new opl3_chip()), sampleRate_(sampleRate) {
    OPL3_Reset(chip_.get(), static_cast<uint32_t>(sampleRate));
}

OplChip::~OplChip() = default;

void OplChip::write(uint8_t reg, uint8_t value) {
    OPL3_WriteRegBuffered(chip_.get(), reg, value);
}

void OplChip::render(int16_t* out, int frames) {
    OPL3_GenerateStream(chip_.get(), out, static_cast<uint32_t>(frames));
}

MusicPlayer::~MusicPlayer() {
    if (deviceId_ != 0) {
        SDL_CloseAudioDevice(deviceId_);
        deviceId_ = 0;
    }
}

bool MusicPlayer::openSilent(std::string& error, int sampleRate) {
    if (deviceId_ != 0) return true;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        error = std::string("SDL_InitSubSystem(audio): ") + SDL_GetError();
        return false;
    }

    SDL_AudioSpec want{};
    want.freq = sampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &MusicPlayer::audioCallback;
    want.userdata = this;

    SDL_AudioSpec have{};
    deviceId_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (deviceId_ == 0) {
        error = std::string("SDL_OpenAudioDevice: ") + SDL_GetError();
        return false;
    }
    deviceRate_ = have.freq;
    SDL_PauseAudioDevice(deviceId_, 0);
    return true;
}

bool MusicPlayer::open(const Archive& drivers, std::string& error,
                       int sampleRate) {
    if (!loadTables(drivers, tables_, error)) return false;
    if (!openSilent(error, sampleRate)) return false;

    SDL_LockAudioDevice(deviceId_);
    chip_.reset(new OplChip(deviceRate_));
    seq_.reset(new MusSequencer(tables_, *chip_));
    samplesPerTick_ = deviceRate_ / kMusTickHz;
    tickAccumulator_ = 0.0;
    SDL_UnlockAudioDevice(deviceId_);
    return true;
}

bool MusicPlayer::play(const Bytes& data, std::string& error) {
    if (deviceId_ == 0) {
        error = "audio device is not open";
        return false;
    }
    std::vector<MusEvent> events;
    if (!parseMus(data, events, error)) return false;

    SDL_LockAudioDevice(deviceId_);
    events_ = std::move(events);
    tickAccumulator_ = 0.0;
    seq_->start(&events_);
    SDL_UnlockAudioDevice(deviceId_);
    return true;
}

void MusicPlayer::stop() {
    // The device can be open with no sequencer behind it: `openSilent` gives
    // sound effects their own device when music is off.
    if (deviceId_ == 0 || !seq_) return;
    SDL_LockAudioDevice(deviceId_);
    seq_->silence();
    events_.clear();
    SDL_UnlockAudioDevice(deviceId_);
}

void MusicPlayer::setPaused(bool paused) {
    if (deviceId_ != 0) SDL_PauseAudioDevice(deviceId_, paused ? 1 : 0);
}

void MusicPlayer::audioCallback(void* userdata, uint8_t* stream, int len) {
    auto* self = static_cast<MusicPlayer*>(userdata);
    self->mix(reinterpret_cast<int16_t*>(stream), len / (2 * sizeof(int16_t)));
}

// Steps the sequencer from the sample clock, so the tempo does not drift
// with the video frame rate the way it would if the game loop drove it.
void MusicPlayer::playSound(const Sound* s) {
    if (deviceId_ == 0) return;
    // The callback runs on the audio thread and reads the voice, so this
    // must be atomic with the callback.
    SDL_LockAudioDevice(deviceId_);
    sfxVoice_.play(s, deviceRate_);
    SDL_UnlockAudioDevice(deviceId_);
}

void MusicPlayer::stopSound() {
    if (deviceId_ == 0) return;
    SDL_LockAudioDevice(deviceId_);
    sfxVoice_.stop();
    SDL_UnlockAudioDevice(deviceId_);
}

void MusicPlayer::mix(int16_t* out, int frames) {
    // With no song loaded there is nothing to step, but the effects voice
    // still has to be serviced; `--no-music` should not mean no sound.
    if (!seq_ || !chip_) {
        for (int i = 0; i < frames * 2; ++i) out[i] = 0;
        sfxVoice_.mix(out, frames);
        return;
    }
    int done = 0;
    while (done < frames) {
        if (tickAccumulator_ <= 0.0) {
            seq_->tick();
            tickAccumulator_ += samplesPerTick_;
        }
        const int chunk =
            std::min(frames - done, static_cast<int>(tickAccumulator_) + 1);
        chip_->render(out + done * 2, chunk);
        tickAccumulator_ -= chunk;
        done += chunk;
    }
    sfxVoice_.mix(out, frames);
}

bool MusicPlayer::renderOffline(const Archive& drivers, const Bytes& song,
                                double seconds, int sampleRate,
                                std::vector<int16_t>& out, std::string& error) {
    FmTables tables{};
    if (!loadTables(drivers, tables, error)) return false;
    std::vector<MusEvent> events;
    if (!parseMus(song, events, error)) return false;

    OplChip chip(sampleRate);
    MusSequencer seq(tables, chip);
    seq.start(&events);

    const double samplesPerTick = sampleRate / kMusTickHz;
    const int total = static_cast<int>(seconds * sampleRate);
    out.assign(static_cast<size_t>(total) * 2, 0);

    double acc = 0.0;
    int done = 0;
    while (done < total) {
        if (acc <= 0.0) {
            seq.tick();
            acc += samplesPerTick;
        }
        const int chunk =
            std::min(total - done, static_cast<int>(acc) + 1);
        chip.render(out.data() + static_cast<size_t>(done) * 2, chunk);
        acc -= chunk;
        done += chunk;
    }
    return true;
}

bool MusicPlayer::logRegisters(const Archive& drivers, const Bytes& song,
                               int ticks, RegisterLog& out, std::string& error) {
    FmTables tables{};
    if (!loadTables(drivers, tables, error)) return false;
    std::vector<MusEvent> events;
    if (!parseMus(song, events, error)) return false;

    MusSequencer seq(tables, out);
    // The reset sweep happens before any tick, so it is logged at tick 0
    // the way `tools/mus_decode.py` does it.
    out.tick = 0;
    seq.start(&events);
    for (int i = 0; i < ticks; ++i) {
        out.tick = static_cast<uint32_t>(i);
        seq.tick();
        if (seq.looped()) break;
    }
    return true;
}

}  // namespace tubes
