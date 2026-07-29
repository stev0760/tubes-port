// Digital sound effects: the `.SFX` resources and the one voice that plays them.
//
// The format was recovered from the file side (docs/reversing-notes.md) and is
// confirmed here from the OTHER side - `SBSOUND.DRV`'s play entry at offset
// 0x344 walks the same header:
//
//     if [SI] <> $F1 then exit;             { the marker }
//     SI := SI + $20;  CX := [SI];          { the sample rate, a WORD }
//     SI := SI + 2;
//     timeConstant := 256 - (1000000 div CX);
//     DSP($40, timeConstant);
//     if [SI] <> 0 then <the other path>    { the flag at $22 }
//     else begin SI := SI + 3;  BX := [SI];  SI := SI + 2 end;
//     <DMA BX bytes from SI>
//
// That is a second consumer of one byte stream, which is this project's most
// reliable technique, and it corrected two guesses. The rate is a **word** at
// 0x20, not a longword - the shipped files are all 8000 Hz so the high half is
// zero either way and the file side could not tell. And the "unknown byte" is
// at **0x22**, not 0x24: the driver reads it right after the rate and takes a
// different path when it is non-zero. Every shipped sound has it clear.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "res.h"

namespace tubes {

// One decoded sound. The samples stay in the original's unsigned 8-bit form -
// silence is 0x80 - because that is what the file holds and the conversion is
// the player's business.
struct Sound {
    std::string name;        // the Pascal ShortString at [1..0x1f]
    int rate = 8000;
    std::vector<uint8_t> pcm;

    bool valid() const { return !pcm.empty() && rate > 0; }
};

bool decodeSfx(const Bytes& raw, Sound& out, std::string& error);

// ONE voice, and a new sound cuts off whatever is playing.
//
// That is not a simplification: `SBSOUND.DRV`'s play entry calls its own stop
// routine at 0x422 before doing anything else, and the driver holds exactly one
// set of position and length variables in the sixteen bytes at `cs:0x20`. There
// is no mixing anywhere in its 1,158 bytes.
//
// The resampler is the port's own - the original just hands the card a rate and
// lets the hardware clock it. Sound is where SDL sits, so the arithmetic here
// is a platform detail rather than a transliteration.
class SfxVoice {
public:
    // Safe to call from the game thread; the audio callback picks it up.
    void play(const Sound* s, int deviceRate);
    void stop();

    // Mixes into `out`, which already holds the music. Called from the audio
    // callback, so it touches only its own fields.
    void mix(int16_t* out, int frames);

    bool busy() const { return sound_ != nullptr; }

private:
    const Sound* sound_ = nullptr;
    // Position in source samples, 16.16 fixed point, and the step that
    // advances it per output frame.
    uint32_t pos_ = 0;
    uint32_t step_ = 1 << 16;
};

}  // namespace tubes
