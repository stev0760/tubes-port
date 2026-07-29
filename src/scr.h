// `.SCR` - a recorded demo, replayed through the normal game loop.
//
// Not a cutscene script, despite the extension. `1000:5f4b` is attract mode: it
// picks a backdrop, loads `DEMO.SCR`, and hands the input stream to the same
// `1000:3a67` a human plays through.
//
//     u16   count      bytes following this field
//     u32   seed       written straight into RandSeed
//     u8[]  one input bitmask per frame, the same bits Game::update takes
//
// The seed was marked "[inferred]" for several sessions and is now read off the
// code. `1000:5fd9` reads four bytes and `1000:6008` stores them into `DS:0xd24`
// - which is Turbo Pascal's `RandSeed`, not the "demo pointer" the notes used
// to call it. That is what makes attract mode deterministic from a cold boot:
// the demo carries the exact generator state its recording was made against.
//
// It is also the only reason a recording can be this small. Nothing about the
// atoms is stored - which colour is dispensed, into which column, when - so the
// port reproduces a demo only if its `Random` and its call ORDER both match.
// That is precisely what makes this the project's regression oracle.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "res.h"

namespace tubes {

struct Demo {
    uint32_t seed = 0;
    std::vector<uint8_t> input;      // one bitmask per frame

    bool valid() const { return !input.empty(); }
};

bool decodeScr(const Bytes& raw, Demo& out, std::string& error);

}  // namespace tubes
