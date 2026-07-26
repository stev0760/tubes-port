#include "gfx.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace tubes {
namespace {

constexpr uint8_t kChunkyPrefix = 0xE5;

// Mode X: four planes, so a plane row is a quarter of the image width.
constexpr int kPlanes = 4;

// Compiled-sprite opcodes.
constexpr uint8_t kMovByte = 0xC6;
constexpr uint8_t kMovWord = 0xC7;
constexpr uint8_t kModrmDisp8 = 0x44;
constexpr uint8_t kModrmDisp16 = 0x84;
constexpr uint8_t kOutDxAl = 0xEE;
constexpr uint8_t kRetf = 0xCB;

// al holds the VGA map mask and starts at 0x11 so that rotating left cycles
// 0x11 -> 0x22 -> 0x44 -> 0x88 -> 0x11, setting carry once per four planes.
constexpr uint8_t kInitialMask = 0x11;

// Mode X plane stride for the 320-wide screen the sprites are authored against.
constexpr int kSpriteStride = 80;

uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

// Sprite offsets go negative, because a .CSP addresses pixels either side of
// its base pointer. C++ division truncates toward zero, which would map
// off = -128 to (row -1, column -48) instead of (row -2, column 32) and
// wreck the bounding box. These floor toward negative infinity instead.
int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

int floorMod(int a, int b) {
    int r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

int planeOf(uint8_t mask) {
    switch (mask & 0x0f) {
        case 0x01: return 0;
        case 0x02: return 1;
        case 0x04: return 2;
        case 0x08: return 3;
        default:   return 0;
    }
}

}  // namespace

bool loadPalette(const Bytes& data, Palette& out, std::string& error) {
    if (data.size() != 768) {
        error = "palette must be 768 bytes, got " + std::to_string(data.size());
        return false;
    }
    for (int i = 0; i < 256; ++i) {
        for (int c = 0; c < 3; ++c) {
            // VGA DAC values are 6-bit.
            int v = data[i * 3 + c] & 0x3f;
            out.rgb[i][c] = static_cast<uint8_t>((v * 255) / 63);
        }
    }
    return true;
}

bool decodeGfx(const Bytes& data, Image& out, std::string& error) {
    // Header sits at offset 0, or at 1 when preceded by the chunky marker.
    // Pick whichever makes width * height exactly fill the file.
    size_t headerAt = 0;
    bool chunky = false;
    bool found = false;

    for (size_t off : {size_t{0}, size_t{1}}) {
        if (data.size() < off + 4) continue;
        int w = rd16(data.data() + off);
        int h = rd16(data.data() + off + 2);
        if (w <= 0 || h <= 0) continue;
        if (off + 4 + static_cast<size_t>(w) * h != data.size()) continue;
        headerAt = off;
        chunky = (off == 1 && data[0] == kChunkyPrefix);
        found = true;
        break;
    }
    if (!found) {
        error = "no .GFX header fits a " + std::to_string(data.size()) +
                " byte resource";
        return false;
    }

    out.width = rd16(data.data() + headerAt);
    out.height = rd16(data.data() + headerAt + 2);
    const uint8_t* src = data.data() + headerAt + 4;
    size_t count = static_cast<size_t>(out.width) * out.height;

    if (chunky) {
        out.pixels.assign(src, src + count);
        return true;
    }

    if (out.width % kPlanes != 0) {
        error = "planar .GFX width " + std::to_string(out.width) +
                " is not a multiple of 4";
        return false;
    }

    // Plane-major: all of plane 0, then 1, 2, 3.
    out.pixels.assign(count, 0);
    int planeWidth = out.width / kPlanes;
    size_t planeSize = static_cast<size_t>(planeWidth) * out.height;
    for (int plane = 0; plane < kPlanes; ++plane) {
        const uint8_t* p = src + plane * planeSize;
        for (int y = 0; y < out.height; ++y) {
            uint8_t* dst = out.pixels.data() + static_cast<size_t>(y) * out.width;
            const uint8_t* row = p + static_cast<size_t>(y) * planeWidth;
            for (int x = 0; x < planeWidth; ++x) {
                dst[x * kPlanes + plane] = row[x];
            }
        }
    }
    return true;
}

bool decodeCsp(const Bytes& data, Sprite& out, std::string& error) {
    struct Write {
        int x, y;
        uint8_t value;
    };
    std::vector<Write> writes;

    size_t i = 0;
    int si = 0;
    uint8_t mask = kInitialMask;
    int carry = 0;

    while (i < data.size()) {
        uint8_t op = data[i];

        if (op == kRetf) {
            if (i + 1 != data.size()) {
                error = "retf at " + std::to_string(i) + " but resource is " +
                        std::to_string(data.size()) + " bytes";
                return false;
            }
            break;
        }

        // rol al,1 - rotate the plane mask, remembering the carry out.
        if (op == 0xD0 && i + 1 < data.size() && data[i + 1] == 0xC0) {
            carry = (mask >> 7) & 1;
            mask = static_cast<uint8_t>((mask << 1) | carry);
            i += 2;
            continue;
        }

        // adc si,0 - advance one byte per four planes. Note rol/adc are
        // independent instructions and may repeat without an intervening
        // out dx,al, to skip planes holding no pixels.
        if (op == 0x83 && i + 2 < data.size() && data[i + 1] == 0xD6 &&
            data[i + 2] == 0x00) {
            si += carry;
            carry = 0;
            i += 3;
            continue;
        }

        if (op == kOutDxAl) {   // no effect when interpreting
            i += 1;
            continue;
        }

        if ((op == kMovByte || op == kMovWord) && i + 1 < data.size()) {
            uint8_t modrm = data[i + 1];
            int disp = 0;
            size_t dispLen = 0;
            if (modrm == kModrmDisp8) {
                disp = static_cast<int8_t>(data[i + 2]);
                dispLen = 1;
            } else if (modrm == kModrmDisp16) {
                disp = static_cast<int16_t>(rd16(data.data() + i + 2));
                dispLen = 2;
            } else {
                error = "unexpected modrm at " + std::to_string(i);
                return false;
            }

            size_t immAt = i + 2 + dispLen;
            size_t immLen = (op == kMovByte) ? 1 : 2;
            if (immAt + immLen > data.size()) {
                error = "truncated store at " + std::to_string(i);
                return false;
            }

            int plane = planeOf(mask);
            for (size_t k = 0; k < immLen; ++k) {
                int off = si + disp + static_cast<int>(k);
                int x = floorMod(off, kSpriteStride) * kPlanes + plane;
                int y = floorDiv(off, kSpriteStride);
                writes.push_back({x, y, data[immAt + k]});
            }
            i = immAt + immLen;
            continue;
        }

        error = "unexpected opcode " + std::to_string(op) + " at " +
                std::to_string(i);
        return false;
    }

    if (writes.empty()) {
        error = "sprite contains no pixels";
        return false;
    }

    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();
    for (const Write& w : writes) {
        minX = std::min(minX, w.x);
        minY = std::min(minY, w.y);
        maxX = std::max(maxX, w.x);
        maxY = std::max(maxY, w.y);
    }

    out.width = maxX - minX + 1;
    out.height = maxY - minY + 1;
    out.originX = minX;
    out.originY = minY;
    size_t n = static_cast<size_t>(out.width) * out.height;
    out.pixels.assign(n, 0);
    out.mask.assign(n, 0);

    for (const Write& w : writes) {
        size_t idx = static_cast<size_t>(w.y - minY) * out.width + (w.x - minX);
        out.pixels[idx] = w.value;
        out.mask[idx] = 1;
    }
    return true;
}

}  // namespace tubes
