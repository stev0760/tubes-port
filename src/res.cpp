#include "res.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace tubes {
namespace {

constexpr char kSignature[] = "Absolute Magic Resource File!\r\n\x1a";
constexpr size_t kSignatureLen = 32;
constexpr size_t kHeaderLen = 39;
constexpr size_t kEntryLen = 26;

// LZSS parameters, read out of the decompressor at `2475:115c` / `2475:10dc`.
constexpr int kRingSize = 4096;
constexpr int kMaxMatch = 18;
constexpr int kThreshold = 2;
constexpr uint8_t kRingFill = 0x20;

uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

Bytes lzssDecompress(const uint8_t* src, size_t srcLen, size_t expected) {
    Bytes out;
    out.reserve(expected);

    uint8_t ring[kRingSize];
    std::memset(ring, kRingFill, sizeof(ring));

    int r = kRingSize - kMaxMatch;   // 0xfee
    size_t pos = 0;
    unsigned flags = 0;

    while (out.size() < expected) {
        flags >>= 1;
        if ((flags & 0x100) == 0) {
            if (pos >= srcLen) break;
            flags = src[pos++] | 0xff00u;
        }

        if (flags & 1) {
            if (pos >= srcLen) break;
            uint8_t c = src[pos++];
            out.push_back(c);
            ring[r] = c;
            r = (r + 1) & (kRingSize - 1);
        } else {
            if (pos + 1 >= srcLen) break;
            uint8_t lo = src[pos++];
            uint8_t hi = src[pos++];
            int offset = lo | ((hi & 0xf0) << 4);
            int length = (hi & 0x0f) + kThreshold + 1;
            for (int k = 0; k < length && out.size() < expected; ++k) {
                uint8_t c = ring[(offset + k) & (kRingSize - 1)];
                out.push_back(c);
                ring[r] = c;
                r = (r + 1) & (kRingSize - 1);
            }
        }
    }
    return out;
}

bool Archive::open(const std::string& path, std::string& error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "cannot open " + path;
        return false;
    }

    uint8_t header[kHeaderLen];
    if (std::fread(header, 1, kHeaderLen, f) != kHeaderLen) {
        error = path + ": too short to be a resource file";
        std::fclose(f);
        return false;
    }
    if (std::memcmp(header, kSignature, kSignatureLen) != 0) {
        error = path + ": not an Absolute Magic resource file";
        std::fclose(f);
        return false;
    }
    if (header[32] != 1) {
        error = path + ": unsupported container version " +
                std::to_string(header[32]);
        std::fclose(f);
        return false;
    }

    uint16_t count = rd16(header + 33);
    uint32_t dirOffset = rd32(header + 35);

    if (std::fseek(f, static_cast<long>(dirOffset), SEEK_SET) != 0) {
        error = path + ": cannot seek to directory";
        std::fclose(f);
        return false;
    }

    Bytes dir(static_cast<size_t>(count) * kEntryLen);
    if (std::fread(dir.data(), 1, dir.size(), f) != dir.size()) {
        error = path + ": directory truncated";
        std::fclose(f);
        return false;
    }
    std::fclose(f);

    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* e = dir.data() + static_cast<size_t>(i) * kEntryLen;
        Entry ent;
        // Name is a Pascal ShortString padded to 12 characters.
        size_t n = std::min<size_t>(e[0], 12);
        ent.name.assign(reinterpret_cast<const char*>(e + 1), n);
        ent.flags = e[13];
        ent.rawSize = rd32(e + 14);
        ent.storedSize = rd32(e + 18);
        ent.offset = rd32(e + 22);
        entries_[ent.name] = ent;
    }

    path_ = path;
    return true;
}

bool Archive::has(const std::string& name) const {
    return entries_.find(name) != entries_.end();
}

bool Archive::read(const std::string& name, Bytes& out,
                   std::string& error) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) {
        error = "resource not found: " + name;
        return false;
    }
    const Entry& e = it->second;

    FILE* f = std::fopen(path_.c_str(), "rb");
    if (!f) {
        error = "cannot reopen " + path_;
        return false;
    }
    if (std::fseek(f, static_cast<long>(e.offset), SEEK_SET) != 0) {
        error = name + ": cannot seek to payload";
        std::fclose(f);
        return false;
    }

    Bytes stored(e.storedSize);
    size_t got = std::fread(stored.data(), 1, stored.size(), f);
    std::fclose(f);
    if (got != stored.size()) {
        error = name + ": payload truncated";
        return false;
    }

    if (e.flags == 1) {
        out = lzssDecompress(stored.data(), stored.size(), e.rawSize);
    } else {
        out = std::move(stored);
    }

    if (out.size() != e.rawSize) {
        error = name + ": expected " + std::to_string(e.rawSize) +
                " bytes, decoded " + std::to_string(out.size());
        return false;
    }
    return true;
}

}  // namespace tubes
