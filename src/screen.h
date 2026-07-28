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

    // Draws the sprite at (x, y), where (x, y) is the position the game passes
    // to `2321:0905` - NOT the top-left of the sprite's pixels.
    //
    // A .CSP is compiled code storing pixels at signed displacements from a
    // base pointer, so each one carries its own offset from that base. Every
    // one of the 108 sprites decodes with `originX >= 128` and
    // `originY >= -2`, and 84 sit at exactly (128, -2): that is the shared
    // base, and the excess is real placement data.
    //
    // The proof it is not an artefact is the fade families. GFADE1..6 walk
    // (+0,+3) (+0,+6) (+2,+6) (+4,+6) (+7,+5) (+7,+5) as the sprite shrinks -
    // a contracting animation has to move its origin inward to stay centred,
    // and no other reading produces that.
    //
    // This was previously documented as "meaningless as a placement offset"
    // and dropped. It is worth 6 to 11 pixels on TESTUBES, TUBEVS, TUBEVLS and
    // TUBEVRS, which is why the test tube had an inner wall in the wrong place
    // and the arcs were missing their verticals.
    void draw(const Sprite& spr, int x, int y);

    // Stamp a w x h box of `src` over this screen, skipping index 0.
    //
    // This is `2321:0874`, transliterated. The original keeps a full-screen
    // snapshot of the static scene - the tube network and the beaker on a
    // black field - captured once at session start, and re-stamps a box of it
    // wherever a moving sprite has been. Its inner loop is exactly:
    //
    //     LODSB            ; a byte of the snapshot
    //     OR AL,AL
    //     JZ  skip         ; index 0 is transparent
    //     MOV ES:[DI],AL   ; otherwise it wins
    //
    // Called on a sprite's OLD box it is the erase; called on its NEW box it
    // is what makes an atom look like it is inside a glass tube - the tube's
    // walls come back on top of the ball, and the ball shows through only
    // where the snapshot is transparent, which is the hollow of the pipe.
    // One routine, both jobs, which is why the original calls it twice per
    // moving object per frame.
    void stamp(const Screen& src, int x, int y, int w, int h);

    const uint8_t* pixels() const { return pixels_.data(); }
    // For the text renderer, which plots single pixels rather than blitting.
    uint8_t* pixelsMutable() { return pixels_.data(); }

    // Expands to RGBA8888 for upload as a texture.
    void toRgba(const Palette& pal, std::vector<uint8_t>& out) const;

private:
    std::vector<uint8_t> pixels_;
};

}  // namespace tubes
