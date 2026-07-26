// Reading the original Tubes resource containers.
//
// Format documented in docs/reversing-notes.md. Nothing here ships game data:
// the container is opened at runtime from the user's own copy.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tubes {

using Bytes = std::vector<uint8_t>;

struct Entry {
    std::string name;
    uint8_t flags = 0;      // 1 = LZSS compressed, 0 = stored
    uint32_t rawSize = 0;   // size once decompressed
    uint32_t storedSize = 0;
    uint32_t offset = 0;
};

// An "Absolute Magic Resource File!" archive, kept open and read on demand.
class Archive {
public:
    // Returns false and fills `error` rather than throwing, so callers can
    // report a missing or wrong-version game directory cleanly.
    bool open(const std::string& path, std::string& error);

    bool has(const std::string& name) const;

    // Decompresses on each call; callers that need a resource repeatedly
    // should hold on to the result.
    bool read(const std::string& name, Bytes& out, std::string& error) const;

    const std::map<std::string, Entry>& entries() const { return entries_; }

private:
    std::string path_;
    std::map<std::string, Entry> entries_;
};

// Okumura LZSS as used by the container: 4096-byte ring buffer pre-filled
// with spaces, 12-bit offset, length (b & 0xf) + 3.
Bytes lzssDecompress(const uint8_t* src, size_t srcLen, size_t expected);

}  // namespace tubes
