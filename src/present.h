#pragma once

// Putting a finished frame on the window, and the display options that decide
// how. SDL lives here.
//
// This is file-scope state rather than something threaded through the frame
// loop, and that is deliberate: the blocking screens - both splashes, the
// cutscene, the edition prompt, the exit banner - present without going round
// the loop at all, so a display option passed as a parameter would have to be
// threaded through ten call sites to say nothing this does not.
//
// `applyDisplayOptions` is the only writer.

#include <SDL2/SDL.h>

#include <string>
#include <vector>

#include "gfx.h"
#include "input.h"
#include "screen.h"

namespace tubes {

extern tubes::GraphicsOptions g_display;

void presentScreen(SDL_Renderer* ren, SDL_Texture* tex,
                   const tubes::Screen& screen, const tubes::Palette& pal,
                   std::vector<uint8_t>& rgba);

void applyDisplayOptions(SDL_Window* win, SDL_Renderer* ren,
                         const tubes::GraphicsOptions& g);

void saveBmp(std::vector<uint8_t>& rgba, const std::string& path);

}  // namespace tubes
