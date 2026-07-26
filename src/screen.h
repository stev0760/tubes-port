// A 320x200 indexed framebuffer, presented with integer scaling.

#pragma once

#include <cstdint>
#include <vector>

#include "gfx.h"

namespace tubes {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 200;

class Screen {
public:
    Screen() : pixels_(kScreenWidth * kScreenHeight, 0) {}

    void clear(uint8_t index = 0);

    // Draws at (0,0); images smaller than the screen are clipped, not scaled.
    void blit(const Image& img, int x = 0, int y = 0);

    // Places the sprite's top-left at (x, y).
    //
    // A .CSP encodes offsets from whatever base pointer the original game
    // passed in, which for these resources puts originX around 128. That is
    // useful provenance but meaningless as a placement offset here, so it is
    // kept on the Sprite and deliberately not applied.
    void draw(const Sprite& spr, int x, int y);

    const uint8_t* pixels() const { return pixels_.data(); }

    // Expands to RGBA8888 for upload as a texture.
    void toRgba(const Palette& pal, std::vector<uint8_t>& out) const;

private:
    std::vector<uint8_t> pixels_;
};

}  // namespace tubes
