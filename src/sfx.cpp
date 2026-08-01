#include "sfx.h"

namespace tubes {
namespace {

constexpr size_t kMarker = 0x00;
constexpr size_t kName = 0x01;
constexpr size_t kRate = 0x20;
constexpr size_t kFlag = 0x22;
constexpr size_t kCount = 0x25;
constexpr size_t kPcm = 0x27;

uint16_t readWord(const Bytes& b, size_t at) {
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

}  // namespace

bool decodeSfx(const Bytes& raw, Sound& out, std::string& error) {
    if (raw.size() < kPcm) {
        error = "too short to hold a .SFX header";
        return false;
    }
    if (raw[kMarker] != 0xf1) {
        error = "missing the 0xf1 marker";     // SBSOUND.DRV bails here too
        return false;
    }

    const size_t nameLen = raw[kName];
    out.name.assign(reinterpret_cast<const char*>(&raw[kName + 1]),
                    nameLen > 30 ? 30 : nameLen);
    out.rate = readWord(raw, kRate);
    if (out.rate <= 0) {
        error = "sample rate is zero";
        return false;
    }
    // The driver takes a different path when this is set; nothing shipped does,
    // so what that path expects is unknown and cannot be inferred from the data.
    if (raw[kFlag] != 0) {
        error = "the flag at 0x22 is set, and its format is not known";
        return false;
    }

    size_t count = readWord(raw, kCount);
    // Every shipped file has `count == size - 0x27` exactly. Clamp rather than
    // reject, so a truncated resource is quiet instead of fatal.
    if (count > raw.size() - kPcm) count = raw.size() - kPcm;
    out.pcm.assign(raw.begin() + kPcm, raw.begin() + kPcm + count);
    return true;
}

void SfxVoice::play(const Sound* s, int deviceRate) {
    if (!s || !s->valid() || deviceRate <= 0) return;
    // Written last, so the callback can never see a new sound with a stale
    // position: it reads `sound_` first and the other two are already set.
    pos_ = 0;
    step_ = static_cast<uint32_t>((static_cast<uint64_t>(s->rate) << 16) /
                                 static_cast<uint32_t>(deviceRate));
    sound_ = s;
}

void SfxVoice::stop() { sound_ = nullptr; }

void SfxPool::play(const Sound* s, int deviceRate) {
    if (!s) { stop(); return; }
    for (SfxVoice& v : voices_) {
        if (!v.busy()) {
            v.play(s, deviceRate);
            return;
        }
    }
    // All four busy. The original would have cut the running one off, so
    // cutting ONE off is still the honest fallback - take the first.
    voices_[0].play(s, deviceRate);
}

void SfxPool::stop() {
    for (SfxVoice& v : voices_) v.stop();
}

void SfxPool::mix(int16_t* out, int frames) {
    for (SfxVoice& v : voices_) v.mix(out, frames);
}

bool SfxPool::busy() const {
    for (const SfxVoice& v : voices_) {
        if (v.busy()) return true;
    }
    return false;
}

void SfxVoice::mix(int16_t* out, int frames) {
    const Sound* s = sound_;
    if (!s) return;

    const size_t n = s->pcm.size();
    for (int i = 0; i < frames; ++i) {
        const size_t idx = pos_ >> 16;
        if (idx >= n) {
            sound_ = nullptr;
            return;
        }
        // Unsigned 8-bit centred on 0x80, scaled to leave headroom for the
        // music underneath rather than clipping against it.
        const int sample = (static_cast<int>(s->pcm[idx]) - 0x80) * 96;
        for (int ch = 0; ch < 2; ++ch) {
            int v = out[i * 2 + ch] + sample;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out[i * 2 + ch] = static_cast<int16_t>(v);
        }
        pos_ += step_;
    }
}

}  // namespace tubes
