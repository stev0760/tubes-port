// The present path. See present.h.

#include "present.h"

#include <cstdio>

namespace tubes {

// The pause overlay, `1000:3916`. Same two rows as a banner, and the loop is
// blocked entirely while it is up.
// How the framebuffer gets put on the window. Entirely the port's - see the
// display options in `input.h`. It is file-scope rather than a parameter
// because the blocking screens (both splashes, every fade) present without
// going round the frame loop, and threading one struct through all ten call
// sites would add nothing this comment does not already say.
//
// It is written once, from the settings, and then only by the graphics screen.
tubes::GraphicsOptions g_display;

void presentScreen(SDL_Renderer* ren, SDL_Texture* tex,
                   const tubes::Screen& screen, const tubes::Palette& pal,
                   std::vector<uint8_t>& rgba) {
    screen.toRgba(pal, rgba);
    SDL_UpdateTexture(tex, nullptr, rgba.data(), tubes::kScreenWidth * 4);

    int winW = 0, winH = 0;
    SDL_GetRendererOutputSize(ren, &winW, &winH);
    // The arithmetic is in `input.cpp`, with no SDL in it, so the tests get at
    // the scaling and letterbox rules directly.
    const tubes::DisplayRect r = tubes::presentRect(winW, winH, g_display);
    const SDL_Rect dst{r.x, r.y, r.w, r.h};

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, &dst);

// Scanlines: one dark line per output row pair, drawn over the image
// rather than baked into it, so the framebuffer a screenshot captures is
// untouched. Skipped below 2x, where every second row would be half the
// picture.
    if (g_display.scanlines && dst.h >= tubes::kScreenHeight * 2) {
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 64);
        for (int y = dst.y + 1; y < dst.y + dst.h; y += 2) {
            SDL_Rect line{dst.x, y, dst.w, 1};
            SDL_RenderFillRect(ren, &line);
        }
    }
    SDL_RenderPresent(ren);
}

// Push the display options at the window and the renderer. Everything else in
// `GraphicsOptions` is read at present time; these two are state SDL holds.
void applyDisplayOptions(SDL_Window* win, SDL_Renderer* ren,
                         const tubes::GraphicsOptions& g) {
    g_display = g;
    if (!win || !ren) return;
    // Desktop fullscreen, not a mode set: the port has no business changing
    // the display's resolution for a 320x200 image it is going to letterbox
    // anyway, and a borderless desktop window alt-tabs cleanly.
    SDL_SetWindowFullscreen(win, g.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP
                                              : 0);
    if (!g.fullscreen && g.scale > 0) {
        SDL_SetWindowSize(win, tubes::kScreenWidth * g.scale,
                          tubes::displayUnitHeight(g) * g.scale);
    }
    SDL_RenderSetVSync(ren, g.vsync ? 1 : 0);
}

void saveBmp(std::vector<uint8_t>& rgba, const std::string& path) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        rgba.data(), tubes::kScreenWidth, tubes::kScreenHeight, 32,
        tubes::kScreenWidth * 4, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return;
    SDL_SaveBMP(surf, path.c_str());
    SDL_FreeSurface(surf);
    std::printf("wrote %s\n", path.c_str());
}

}  // namespace tubes
