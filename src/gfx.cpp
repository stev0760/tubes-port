#include "gfx.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace tubes {
namespace {

constexpr uint8_t kChunkyPrefix = 0xE5;

// The marker word both .SPR resources open with.
constexpr int kSprMarker = 0x00f5;

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

Palette fadePalette(const Bytes& raw, int step, int steps) {
    Palette out;
    if (raw.size() != 768 || steps <= 0) return out;
    if (step < 0) step = 0;
    if (step > steps) step = steps;
    for (int i = 0; i < 256; ++i) {
        for (int c = 0; c < 3; ++c) {
            const int v = (raw[i * 3 + c] & 0x3f) * step / steps;
            out.rgb[i][c] = static_cast<uint8_t>((v * 255) / 63);
        }
    }
    return out;
}

bool decodeGfx(const Bytes& data, Image& out, std::string& error,
               GfxLayout layout) {
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
        if (layout == GfxLayout::kChunky) chunky = true;
        if (layout == GfxLayout::kPlanar) chunky = false;
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

// ---- .SPR ------------------------------------------------------------------

bool decodeSpr(const Bytes& data, std::vector<Image>& out, std::string& error) {
    out.clear();
    if (data.size() < 4) {
        error = ".SPR is too short for a header";
        return false;
    }
    const int marker = rd16(data.data() + 0);
    const int count = rd16(data.data() + 2);
    if (marker != kSprMarker) {
        error = ".SPR marker is " + std::to_string(marker) + ", not 245";
        return false;
    }
    const size_t headerLen = 4 + static_cast<size_t>(count) * 2;
    if (count <= 0 || headerLen > data.size()) {
        error = ".SPR frame count " + std::to_string(count) + " does not fit";
        return false;
    }
    std::vector<size_t> offs(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) offs[i] = rd16(data.data() + 4 + i * 2);
    if (offs[0] != headerLen) {
        error = ".SPR first frame is at " + std::to_string(offs[0]) +
                ", not the end of the header";
        return false;
    }
    for (int i = 0; i < count; ++i) {
        const size_t end = (i + 1 < count) ? offs[i + 1] : data.size();
        if (offs[i] >= end || end > data.size()) {
            error = ".SPR frame " + std::to_string(i) + " has a bad extent";
            return false;
        }
        Image img;
        const Bytes slice(data.begin() + static_cast<long>(offs[i]),
                          data.begin() + static_cast<long>(end));
        if (!decodeGfx(slice, img, error)) {
            error = ".SPR frame " + std::to_string(i) + ": " + error;
            return false;
        }
        out.push_back(std::move(img));
    }
    return true;
}

// ---- .ANM ------------------------------------------------------------------
//
// The interpreter. Every opcode the one .ANM in the game uses is here, and an
// unknown one is an error rather than a skip - a compiled format that is
// silently tolerant decodes garbage into plausible-looking pixels.

bool decodeAnm(const Bytes& data, std::vector<AnimFrame>& out,
               std::string& error) {
    out.clear();
    if (data.size() < 2) {
        error = ".ANM is too short for a frame count";
        return false;
    }
    const int count = rd16(data.data() + 0);
    const size_t tableLen = 2 + static_cast<size_t>(count) * 4;
    if (count <= 0 || tableLen > data.size()) {
        error = ".ANM frame count " + std::to_string(count) + " does not fit";
        return false;
    }

    size_t at = tableLen;
    for (int f = 0; f < count; ++f) {
        const size_t size = rd16(data.data() + 2 + f * 4);
        if (rd16(data.data() + 4 + f * 4) != 0) {
            error = ".ANM frame " + std::to_string(f) + " has a high size word";
            return false;
        }
        if (at + size > data.size()) {
            error = ".ANM frame " + std::to_string(f) + " runs past the file";
            return false;
        }

        const uint8_t* code = data.data() + at;
        AnimFrame frame;
        // The registers the frame's caller sets up: DS:SI on the blob itself,
        // ES:DI at the top left of the screen.
        size_t ip = 0;
        int si = 0, di = 0, ax = 0, cx = 0;
        int runAt = -1;                         // where the open run started
        std::vector<uint8_t> run;

        auto put = [&](uint8_t v) {
            if (runAt < 0 || di != runAt + static_cast<int>(run.size())) {
                if (runAt >= 0 && !run.empty()) {
                    frame.runs.push_back({runAt, run});
                }
                run.clear();
                runAt = di;
            }
            run.push_back(v);
            di = (di + 1) & 0xffff;
        };
        auto get = [&]() -> uint8_t {
            const uint8_t v = (si >= 0 && static_cast<size_t>(si) < size)
                                  ? code[si] : 0;
            si = (si + 1) & 0xffff;
            return v;
        };
        auto imm16 = [&]() {
            const int v = code[ip] | (code[ip + 1] << 8);
            ip += 2;
            return v;
        };

        bool done = false;
        while (!done) {
            if (ip >= size) {
                error = ".ANM frame " + std::to_string(f) + " has no RETF";
                return false;
            }
            const uint8_t op = code[ip++];
            const uint8_t nxt = ip < size ? code[ip] : 0;
            switch (op) {
            case 0xcb:                                  // retf
                done = true;
                break;
            case 0x33:                                  // xor cx,cx
                if (nxt != 0xc9) { error = ".ANM: bad xor"; return false; }
                ++ip;
                cx = 0;
                break;
            case 0xb0:                                  // mov al,imm8
                ax = (ax & 0xff00) | code[ip++];
                break;
            case 0xb1:                                  // mov cl,imm8
                cx = (cx & 0xff00) | code[ip++];
                break;
            case 0xb9: cx = imm16(); break;             // mov cx,imm16
            case 0xb8: ax = imm16(); break;             // mov ax,imm16
            case 0x8b:                                  // mov bx,di / mov di,bx
                if (nxt != 0xdf && nxt != 0xfb) {
                    error = ".ANM: bad mov";
                    return false;
                }
                ++ip;
                break;                                  // bx is never read back
            case 0x81: {
                const uint8_t modrm = code[ip++];
                const int v = imm16();
                if (modrm == 0xc6) si += v;
                else if (modrm == 0xee) si -= v;
                else if (modrm == 0xc7) di = (di + v) & 0xffff;
                else if (modrm == 0xef) di = (di - v) & 0xffff;
                else {
                    error = ".ANM: unknown 81 /" + std::to_string(modrm);
                    return false;
                }
                break;
            }
            case 0xf3: {                                // rep <string op>
                const uint8_t sop = code[ip++];
                if (sop == 0xab) {
                    for (int i = 0; i < cx; ++i) {
                        put(static_cast<uint8_t>(ax));
                        put(static_cast<uint8_t>(ax >> 8));
                    }
                } else if (sop == 0xaa) {
                    for (int i = 0; i < cx; ++i) put(static_cast<uint8_t>(ax));
                } else if (sop == 0xa5) {
                    for (int i = 0; i < cx; ++i) { put(get()); put(get()); }
                } else if (sop == 0xa4) {
                    for (int i = 0; i < cx; ++i) put(get());
                } else {
                    error = ".ANM: unknown string op after REP";
                    return false;
                }
                cx = 0;
                break;
            }
            case 0xab:                                  // stosw
                put(static_cast<uint8_t>(ax));
                put(static_cast<uint8_t>(ax >> 8));
                break;
            case 0xaa: put(static_cast<uint8_t>(ax)); break;   // stosb
            case 0xa5: put(get()); put(get()); break;          // movsw
            case 0xa4: put(get()); break;                      // movsb
            default:
                error = ".ANM frame " + std::to_string(f) +
                        ": unknown opcode " + std::to_string(op) + " at " +
                        std::to_string(ip - 1);
                return false;
            }
        }
        if (runAt >= 0 && !run.empty()) frame.runs.push_back({runAt, run});
        out.push_back(std::move(frame));
        at += size;
    }
    if (at != data.size()) {
        error = ".ANM frames end at " + std::to_string(at) + ", file is " +
                std::to_string(data.size());
        return false;
    }
    return true;
}

}  // namespace tubes
