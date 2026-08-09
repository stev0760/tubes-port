#pragma once

// The screens that run their own loop, outside the frame loop entirely.
//
// Both boot splashes, the opening cutscene, the first-run edition prompt and
// the shareware sign-off all block: each pumps SDL events, presents, and holds
// on its own clock until it is skipped or finishes. None of them is a stage the
// frame loop can be in, which is why they were never part of its dispatch and
// why they group together here.
//
// They keep SDL, so boot.cpp is built into tubes-port only. See CLAUDE.md's
// Layout table for the files permitted to include it.

#include <SDL2/SDL.h>

#include <string>
#include <vector>

#include "edition.h"
#include "font.h"
#include "gfx.h"
#include "mus.h"
#include "opl.h"
#include "res.h"
#include "screen.h"
#include "textscreen.h"

namespace tubes {

struct SkipWatch {
    int key = 0;                        // 1 Enter/Space, 2 ESC, 0 nothing yet

    void pump() {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { key = 2; continue; }
            // A pad works here too, and it has to: these screens run before
            // the frame loop exists, so nothing else would see the button.
            // B and Back are the ESC of a controller; anything else is Enter.
            if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
                const int b = ev.cbutton.button;
                key = (b == SDL_CONTROLLER_BUTTON_B ||
                       b == SDL_CONTROLLER_BUTTON_BACK) ? 2 : 1;
                continue;
            }
            if (ev.type != SDL_KEYDOWN) continue;
            const SDL_Keycode k = ev.key.keysym.sym;
            if (k == SDLK_ESCAPE) key = 2;
            else if (k == SDLK_RETURN || k == SDLK_SPACE) key = 1;
        }
    }
    int take() {
        const int k = key;
        key = 0;
        return k;
    }
    // Pump and report. Anything that waits calls this rather than sleeping.
    bool pumped() {
        pump();
        return key != 0;
    }
};

tubes::Edition runEditionPrompt(SDL_Renderer* ren, int fadeSteps,
                                bool& cancelled,
                                const std::string& screenshot = std::string(),
                                int startOn = 0);

void runExitScreen(SDL_Window* win, SDL_Renderer* ren, const tubes::TextScreen& banner,
                   int fadeSteps, const std::string& screenshot);

int runSoftwareCreationsSplash(const tubes::Archive& res, SDL_Renderer* ren,
                               SDL_Texture* tex, tubes::Screen& screen,
                               std::vector<uint8_t>& rgba, int fadeSteps,
                               SkipWatch& skip, int shotFrame,
                               const std::string& shotPath);

int runAbsoluteMagicSplash(const tubes::Archive& res, SDL_Renderer* ren,
                           SDL_Texture* tex, tubes::Screen& screen,
                           std::vector<uint8_t>& rgba, int fadeSteps,
                           SkipWatch& skip, tubes::MusicPlayer& music,
                           bool musicOn, bool soundOn, int shotStep,
                           const std::string& shotPath);

int runCutscene(const tubes::Archive& res, SDL_Renderer* ren, SDL_Texture* tex,
                tubes::Screen& screen, std::vector<uint8_t>& rgba,
                const tubes::Bytes& palRaw, const tubes::Palette& pal,
                int fadeSteps, SkipWatch& skip, tubes::MusicPlayer& music,
                bool musicOn, bool soundOn, const tubes::Image* board,
                bool haveBoard, const tubes::Sprite* atoms,
                const bool* haveAtom, const tubes::Font& small, bool haveSmall,
                int shotPage, int shotTick, const std::string& shotPath);

}  // namespace tubes
