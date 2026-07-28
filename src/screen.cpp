#include "screen.h"

#include <algorithm>

namespace tubes {

void Screen::clear(uint8_t index) {
    std::fill(pixels_.begin(), pixels_.end(), index);
}

void Screen::blit(const Image& img, int x, int y) {
    if (!img.valid()) return;

    for (int row = 0; row < img.height; ++row) {
        int dy = y + row;
        if (dy < 0 || dy >= kScreenHeight) continue;

        const uint8_t* src = img.pixels.data() +
                             static_cast<size_t>(row) * img.width;
        uint8_t* dst = pixels_.data() + static_cast<size_t>(dy) * kScreenWidth;

        for (int col = 0; col < img.width; ++col) {
            int dx = x + col;
            if (dx < 0 || dx >= kScreenWidth) continue;
            uint8_t v = src[col];
            if (img.transparent >= 0 && v == img.transparent) continue;
            dst[dx] = v;
        }
    }
}

void Screen::draw(const Sprite& spr, int x, int y) {
    // The .CSP base: see the note in screen.h. Displacements are relative to
    // it, so a sprite whose pixels start further in is placed further in.
    x += spr.originX - kSpriteBaseX;
    y += spr.originY - kSpriteBaseY;

    for (int row = 0; row < spr.height; ++row) {
        int dy = y + row;
        if (dy < 0 || dy >= kScreenHeight) continue;

        size_t base = static_cast<size_t>(row) * spr.width;
        uint8_t* dst = pixels_.data() + static_cast<size_t>(dy) * kScreenWidth;

        for (int col = 0; col < spr.width; ++col) {
            if (!spr.mask[base + col]) continue;   // transparent
            int dx = x + col;
            if (dx < 0 || dx >= kScreenWidth) continue;
            dst[dx] = spr.pixels[base + col];
        }
    }
}

void Screen::stamp(const Screen& src, int x, int y, int w, int h) {
    for (int row = 0; row < h; ++row) {
        const int dy = y + row;
        if (dy < 0 || dy >= kScreenHeight) continue;

        const size_t line = static_cast<size_t>(dy) * kScreenWidth;
        const uint8_t* s = src.pixels_.data() + line;
        uint8_t* d = pixels_.data() + line;

        for (int col = 0; col < w; ++col) {
            const int dx = x + col;
            if (dx < 0 || dx >= kScreenWidth) continue;
            const uint8_t v = s[dx];
            if (v == 0) continue;      // OR AL,AL / JZ - index 0 is transparent
            d[dx] = v;
        }
    }
}

void Screen::toRgba(const Palette& pal, std::vector<uint8_t>& out) const {
    out.resize(pixels_.size() * 4);
    for (size_t i = 0; i < pixels_.size(); ++i) {
        const uint8_t* c = pal.rgb[pixels_[i]];
        out[i * 4 + 0] = c[0];
        out[i * 4 + 1] = c[1];
        out[i * 4 + 2] = c[2];
        out[i * 4 + 3] = 255;
    }
}

}  // namespace tubes
