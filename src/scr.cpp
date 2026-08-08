#include "scr.h"

namespace tubes {

bool decodeScr(const Bytes& raw, Demo& out, std::string& error) {
// The count covers everything after itself, seed included: the shipped
// `DEMO.SCR` is 11,976 bytes and its count reads 11,974.
    if (raw.size() < 6) {
        error = "too short to hold a .SCR header";
        return false;
    }
    const size_t count =
        static_cast<size_t>(raw[0]) | (static_cast<size_t>(raw[1]) << 8);
    size_t end = 2 + count;
    if (end > raw.size()) end = raw.size();
    if (end < 6) {
        error = "count leaves no room for the seed";
        return false;
    }

    out.seed = static_cast<uint32_t>(raw[2]) |
               (static_cast<uint32_t>(raw[3]) << 8) |
               (static_cast<uint32_t>(raw[4]) << 16) |
               (static_cast<uint32_t>(raw[5]) << 24);
    out.input.assign(raw.begin() + 6, raw.begin() + end);
    return true;
}

}  // namespace tubes
