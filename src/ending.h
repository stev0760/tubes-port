// The wave-75 ending, `1000:9499` - the one screen in the program nobody had
// seen, and the reason `PRIZE.GFX` sits in the archive unused.
//
// `1000:a657` is the trigger, and it is two instructions:
//
//     if wave >= 75 then RegisteredEnding;      { CMP $4b / JC }
//     wave := wave + 1
//
// so it runs on clearing wave 75, before the counter moves, and the routine's
// first act is to set the session's `gameOver` flag - `SS:[DI + 0xfe02] := 1`
// through the static link, which is `BP-0x1fe` in `1000:9e53`'s frame. The
// session therefore ends the moment the ending is over, and the high score
// screen follows as it would from any other game over.
//
// The screen is the classroom, not a new one: `1b2e:0656` and `1b2e:0a11`
// again, with `DS:0x20e3` set - which is the flag that selects the professor's
// jumping arm, the only place in the program that reaches it. `1000:9676`
// clears it again on the way out.
//
// Two pages, each held by `1b2e:0b8f(0x1e)` - the jump wait, thirty seconds or
// a key:
//
//     page 1   'Congratulations' over the story text
//     page 2   `1b2e:0a11` again (which wipes the text), then PRIZE.GFX
//              centred: ((320 - w) div 2, (200 - h) div 2)
//
// Platform-agnostic: text and layout, no SDL. See `docs/reversing-notes.md`.
#ifndef TUBES_ENDING_H
#define TUBES_ENDING_H

#include <cstdint>

namespace tubes {

// `2000:3fab` is handed a resource slot, and the slot is what names the font:
// `DS:0x2110` is STARTREK.816 and `DS:0x2118` is TINY6X8.88.
enum class EndingFont { kHeading, kSmall };

struct EndingLine {
    EndingFont font;
    int left;          // x, or the left bound when centred
    int right;         // the right bound; equals `left` when not centred
    int y;
    uint8_t colour;
    uint8_t mode;
    bool centred;      // `2000:37ea` rather than `2000:36ab`
    const char* text;
};

constexpr int kEndingLineCount = 12;
extern const EndingLine kEndingLines[kEndingLineCount];

// `1000:9508` sets it, `1000:9676` clears it. The port has no DGROUP, so the
// jumping arm is selected by asking for it.
constexpr int kEndingWave = 75;              // `1000:a657`, `CMP ..., 0x4b`

// `1b2e:0b8f`, the jump wait. Same shape as `1b2e:0e37`: `param * 7`
// iterations of `Delay(10)`, so `param` seconds, and it returns the same key
// codes. Both of the ending's pages pass 0x1e.
constexpr int kEndingHoldSeconds = 30;       // 0x1e

// `1b2e:0b8f`'s own animation. The frame counter `DS:0x20fc` runs 1..3 and the
// professor's y is `0x61 - 3 * frame`, so he rises 94, 91, 88 and drops back -
// a hop rather than a ping-pong, and the x never moves.
constexpr const char* kJumpNames[3] = {"JUMP1.GFX", "JUMP2.GFX", "JUMP3.GFX"};
constexpr int kJumpFrames = 3;
constexpr int kJumpRetraces = 10;            // `Delay(10)`, as the wave is
constexpr int kJumpX = 267;                  // 0x10b
constexpr int kJumpBaseY = 97;               // 0x61, less 3 per frame
constexpr int kJumpStepY = 3;
inline int jumpY(int frame) { return kJumpBaseY - kJumpStepY * frame; }

// `1b2e:0656`'s jump arm stands his books somewhere else than the idle arm
// does - `Draw(276, 165, BOOKS)` rather than nothing at all.
constexpr int kJumpBooksX = 276;             // 0x114
constexpr int kJumpBooksY = 165;             // 0xa5

constexpr const char* kPrizeArt = "PRIZE.GFX";

}  // namespace tubes

#endif
