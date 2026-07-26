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
