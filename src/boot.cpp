// The blocking screens: splashes, cutscene, edition prompt, sign-off.
// See boot.h.

#include "boot.h"

#include "present.h"
#include "screens.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace tubes {

// The first-run edition prompt, on the same text screen `TUBESEND.BIN` uses.
//
// See `textscreen.h` for why the question exists at all and why it is asked
// here, before the graphics come up, rather than inside the game: the original
// never asked it, so a screen that speaks as the game would be claiming
// something about the original that is not true. This one is plainly the
// port's setup, in the idiom `SETUP.EXE` would have used.
//
// Returns the answer. `cancelled` comes back true if the player closed the
// window, in which case nothing is saved and the program should stop - quietly
// defaulting to registered and carrying on would file their saves under a
// choice they never made.
tubes::Edition runEditionPrompt(SDL_Renderer* ren, int fadeSteps,
                                bool& cancelled,
                                const std::string& screenshot,
                                int startOn) {
    cancelled = false;
    int selected = startOn;

    tubes::TextScreen ts;
    std::vector<uint8_t> indexed;

    tubes::Bytes raw(768, 0);
    for (int i = 0; i < 16; ++i) {
        raw[i * 3 + 0] = tubes::kTextPalette[i][0];
        raw[i * 3 + 1] = tubes::kTextPalette[i][1];
        raw[i * 3 + 2] = tubes::kTextPalette[i][2];
    }

    // Static, for the reason the exit screen documents at length: this is a
    // 640x400 RGBA picture that changes only when the selection moves, and
    // streaming plus `SDL_UpdateTexture` is the pairing that produced the
    // partial uploads reported as a flicker.
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC,
                                         tubes::kTextScreenW,
                                         tubes::kTextScreenH);
    if (!tex) return tubes::Edition::kRegistered;
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);

    std::vector<uint8_t> rgba(static_cast<size_t>(tubes::kTextScreenW) *
                              tubes::kTextScreenH * 4);
    auto upload = [&](const tubes::Palette& pal) {
        for (size_t i = 0; i < indexed.size(); ++i) {
            // `Palette` is already 8-bit - both `fadePalette` and
            // `textPalette` expand - so this copies rather than converting.
            // Expanding here as well is what made the fade wrap; see the
            // flicker note in PLAN.md.
            const uint8_t* c = pal.rgb[indexed[i]];
            rgba[i * 4 + 0] = c[0];
            rgba[i * 4 + 1] = c[1];
            rgba[i * 4 + 2] = c[2];
            rgba[i * 4 + 3] = 255;
        }
        SDL_UpdateTexture(tex, nullptr, rgba.data(), tubes::kTextScreenW * 4);
    };
    auto draw = [&]() {
        int winW = 0, winH = 0;
        SDL_GetRendererOutputSize(ren, &winW, &winH);
        const tubes::DisplayRect r = tubes::presentRect(winW, winH, tubes::g_display);
        const SDL_Rect dst{r.x, r.y, r.w, r.h};
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        if (tubes::g_display.scanlines && dst.h >= tubes::kScreenHeight * 2) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 64);
            for (int y = dst.y + 1; y < dst.y + dst.h; y += 2) {
                SDL_Rect line{dst.x, y, dst.w, 1};
                SDL_RenderFillRect(ren, &line);
            }
        }
        SDL_RenderPresent(ren);
    };

    auto rebuild = [&]() {
        tubes::buildEditionPrompt(ts, selected);
        ts.render(indexed);
    };

    // Fade in, the same way every screen in this port arrives.
    rebuild();
    for (int i = 0; i <= fadeSteps; ++i) {
        upload(tubes::fadePalette(raw, i, fadeSteps ? fadeSteps : 1));
        draw();
        if (fadeSteps > 0) SDL_Delay(1000 / 70);
    }
    const tubes::Palette lit = tubes::textPalette();
    upload(lit);
    draw();

    // `--edition-prompt` captures the screen and leaves, the same way every
    // other screen in this port can be opened for a capture without walking to
    // it. It answers nothing: the caller treats a capture as a scripted run.
    if (!screenshot.empty()) {
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
            rgba.data(), tubes::kTextScreenW, tubes::kTextScreenH, 32,
            tubes::kTextScreenW * 4, SDL_PIXELFORMAT_RGBA32);
        if (surf) {
            SDL_SaveBMP(surf, screenshot.c_str());
            SDL_FreeSurface(surf);
            std::printf("wrote %s\n", screenshot.c_str());
        }
        SDL_DestroyTexture(tex);
        cancelled = true;
        return tubes::Edition::kRegistered;
    }

    // Throttled unconditionally - a vsync request is advisory on this port's
    // targets and paces nothing. Same measurement as the exit screen's hold.
    bool answering = true;
    while (answering) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                cancelled = true;
                answering = false;
                break;
            }
            int move = 0;
            bool accept = false;
            if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                case SDLK_UP: case SDLK_LEFT: move = -1; break;
                case SDLK_DOWN: case SDLK_RIGHT: move = 1; break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
                    accept = true; break;
                case SDLK_ESCAPE: cancelled = true; answering = false; break;
                default: break;
                }
            } else if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (ev.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: move = -1; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: move = 1; break;
                case SDL_CONTROLLER_BUTTON_A: accept = true; break;
                default: break;
                }
            }
            if (move != 0) {
                // Two answers, so a wrap and a clamp are the same thing - but
                // it wraps, because every list in this game does.
                selected = (selected + move + tubes::kEditionAnswers) %
                           tubes::kEditionAnswers;
                rebuild();
                upload(lit);
            }
            if (accept) answering = false;
        }
        draw();
        SDL_Delay(1000 / 70);
    }
    SDL_DestroyTexture(tex);
    return tubes::editionForAnswer(selected);
}

void runExitScreen(SDL_Window* win, SDL_Renderer* ren, const tubes::TextScreen& banner,
                   int fadeSteps, const std::string& screenshot) {
    tubes::TextScreen ts = banner;
    std::vector<uint8_t> indexed;
    ts.render(indexed);

    // The text palette in the raw 6-bit form `fadePalette` expects, so the
    // ramp arithmetic is the game's and not a second implementation of it.
    tubes::Bytes raw(768, 0);
    for (int i = 0; i < 16; ++i) {
        raw[i * 3 + 0] = tubes::kTextPalette[i][0];
        raw[i * 3 + 1] = tubes::kTextPalette[i][1];
        raw[i * 3 + 2] = tubes::kTextPalette[i][2];
    }

    // Static, not streaming - and this is what the flicker was.
    //
    // The reported symptom was precise and is what identified it: about twice,
    // the first quarter of the picture inverted for a split second. That is
    // not a swap or a present-rate artifact, which is what three earlier
    // attempts assumed; it is a partial texture upload. Part of the image is
    // drawn from the new upload and part from what was there before, so a
    // region of it shows the previous frame's colours.
    //
    // The cause is a mismatched pairing. SDL's two texture access modes each
    // have their own update path:
    //
    //     static    + SDL_UpdateTexture              - changes rarely
    //     streaming + SDL_LockTexture / Unlock       - changes every frame
    //
    // This texture was streaming and updated with `SDL_UpdateTexture`, which
    // is the combination neither is for. The main loop does the same thing and
    // gets away with it at 320 x 200; this one is 640 x 400 RGBA - a megabyte,
    // four times the data - which is four times the window for an upload to
    // race the draw still sampling from it.
    //
    // Static is the right mode on the merits anyway: this screen is a fixed
    // picture with an 8 x 2 cursor on it. It is uploaded when it changes, not
    // once a frame.
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC,
                                         tubes::kTextScreenW, tubes::kTextScreenH);
    if (!tex) return;
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);

    std::vector<uint8_t> rgba(static_cast<size_t>(tubes::kTextScreenW) *
                              tubes::kTextScreenH * 4);
    // The cursor is stamped over the finished pixels rather than being a
    // glyph, because that is what the hardware does - it is generated by the
    // CRTC from the cursor scanline registers, not fetched from the character
    // ROM.
    int cursorCol = 0;
    bool cursorOn = false;

    // Build the pixels and hand them to the texture. Called only when
    // something has actually changed: once per fade step, once when the prompt
    // appears, and once per cursor blink.
    auto upload = [&](const tubes::Palette& pal) {
        for (size_t i = 0; i < indexed.size(); ++i) {
            // Already 8-bit: `Palette` holds expanded values, and both the
            // fade and the lit palette produce them. This used to expand a
            // second time, which overflowed and wrapped - the flicker.
            const uint8_t* c = pal.rgb[indexed[i]];
            rgba[i * 4 + 0] = c[0];
            rgba[i * 4 + 1] = c[1];
            rgba[i * 4 + 2] = c[2];
            rgba[i * 4 + 3] = 255;
        }
        if (cursorOn) {
            const uint8_t* c = pal.rgb[kPromptAttr & 0x0F];
            for (int y = kCursorTopRow; y < tubes::kGlyphH; ++y) {
                const size_t base =
                    (static_cast<size_t>(kPromptRow * tubes::kGlyphH + y) * tubes::kTextScreenW +
                     static_cast<size_t>(cursorCol) * tubes::kGlyphW) * 4;
                for (int x = 0; x < tubes::kGlyphW; ++x) {
                    rgba[base + x * 4 + 0] = c[0];
                    rgba[base + x * 4 + 1] = c[1];
                    rgba[base + x * 4 + 2] = c[2];
                    rgba[base + x * 4 + 3] = 255;
                }
            }
        }
        SDL_UpdateTexture(tex, nullptr, rgba.data(), tubes::kTextScreenW * 4);
    };

    // Put the texture on the window. No upload, so this is safe to call as
    // often as the loop likes.
    auto draw = [&]() {
        int winW = 0, winH = 0;
        SDL_GetRendererOutputSize(ren, &winW, &winH);
        const tubes::DisplayRect r = tubes::presentRect(winW, winH, tubes::g_display);
        const SDL_Rect dst{r.x, r.y, r.w, r.h};
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        if (tubes::g_display.scanlines && dst.h >= tubes::kScreenHeight * 2) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 64);
            for (int y = dst.y + 1; y < dst.y + dst.h; y += 2) {
                SDL_Rect line{dst.x, y, dst.w, 1};
                SDL_RenderFillRect(ren, &line);
            }
        }
        SDL_RenderPresent(ren);
    };

    // `23e7:0097`. One upload and one draw per step, throttled to the retrace
    // - never faster, because a vsync request is advisory on this port's
    // targets and cannot be relied on to pace anything. See the note in the
    // hold below.
    for (int i = 0; i <= fadeSteps; ++i) {
        upload(tubes::fadePalette(raw, i, fadeSteps ? fadeSteps : 1));
        draw();
        if (fadeSteps > 0) SDL_Delay(1000 / 70);
    }
    const tubes::Palette lit = tubes::textPalette();

// The prompt appears after the fade, not during it: on the real thing the
// banner is written, the program exits, and only then does the shell get a
// turn. Keeping that order costs nothing and is the whole beat of the
// screen.
    const char* kPrompt = "C:\\TUBES>";
    drawPrompt(ts, kPrompt);
    cursorCol = static_cast<int>(std::strlen(kPrompt));
    ts.render(indexed);
    cursorOn = true;
    upload(lit);
    draw();

    if (!screenshot.empty()) {
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
            rgba.data(), tubes::kTextScreenW, tubes::kTextScreenH, 32,
            tubes::kTextScreenW * 4, SDL_PIXELFORMAT_RGBA32);
        if (surf) {
            SDL_SaveBMP(surf, screenshot.c_str());
            SDL_FreeSurface(surf);
            std::printf("wrote %s\n", screenshot.c_str());
        }
        SDL_DestroyTexture(tex);
        return;
    }

    // Hold until a key. The original does not wait - it has already exited and
    // the banner simply persists - so this is the port's substitute for a
    // screen that outlives the program.
    //
    // Throttled unconditionally. Measured on a Wayland/OpenGL target:
// `SDL_RenderSetVSync(1)` returns 0, sets the present-vsync flag, and does
// not sync - 60 presents in 459 ms. So a vsync request is advisory here and
// anything presenting in a loop paces itself.
    bool waiting = true;
    const uint32_t start = SDL_GetTicks();
    const uint32_t blinkMs = kCursorBlinkRetraces * 1000 / 70;
    bool shown = cursorOn;
    while (waiting) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT || ev.type == SDL_KEYDOWN ||
                ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONDOWN) {
                waiting = false;
            }
        }
        const bool want = ((SDL_GetTicks() - start) / blinkMs) % 2 == 0;
        if (want != shown) {
            shown = want;
            cursorOn = want;
            upload(lit);           // the only thing on this screen that changes
        }
        draw();
        SDL_Delay(1000 / 70);
    }
    SDL_DestroyTexture(tex);
    (void)win;
}




// ---- The boot splashes -----------------------------------------------------
//
// `1b2e:11b0` is the whole sequence and it is five lines:
//
//     SetMode13h;                                { 23df:0000 }
//     k := SoftwareCreations;                    { 21d5:007b }
//     if (k <> 1) and (k <> 2) then AbsoluteMagic;   { 2178:00eb }
//
// so a skip during the first splash takes the second one with it. The codes
// are the input driver's: 1 is Enter or Space, 2 is ESC.
//
// `23e7:0024` is `Delay(n)` and its unit is the vertical retrace - the body is
// `23e7:0016` (wait for the end of one vblank, then for the start of the next)
// with `LOOP` around it. So every hold in these screens is n/70 s.

// One retrace, near enough. The blocking screens are the only place the port
// paces on the 70 Hz retrace rather than the 16.11 Hz game frame; everything
// else runs off the simulation clock.
void waitRetraces(int n) {
    if (n > 0) SDL_Delay(static_cast<Uint32>(n * 1000 / 70));
}

// The player's skip.
//
// The original reads its input driver only in the tail loop of each splash,
// but through a buffered read - `[DS:0x234e]` (ClearKeyBuffer) immediately
// after the wait is the tell - so a press during the animation still counts,
// just not until the next poll. That made ESC feel dead for up to a second,
// and the player asked for it to cut in at once.
//
// So `pumped()` is checked everywhere the screen would otherwise block: the
// fade steps, the animation frames and the holds. This is a deliberate
// departure from the original, agreed with the player, and the only one in
// these two screens - the pacing, the order and every literal are untouched.

// A hold that a keypress can cut short. `n` retraces, polled once each.
bool holdRetraces(int n, SkipWatch& skip) {
    for (int i = 0; i < n; ++i) {
        if (skip.pumped()) return true;
        SDL_Delay(1000 / 70);
    }
    return skip.pumped();
}

// `21ea:0690` is `SetFrameRate(fps)`: it computes `145 div fps` and programs
// that as the timer period, so what the game actually runs at is
// `145 / (145 div fps)`. That is where 16.11 Hz comes from - the session asks
// for 16 (`1000:44d8`), 145 div 16 is 9, and 145/9 is 16.11. Both of the
// Absolute Magic splash's phases set it, to 9 and to 4.
//
// The same hold as above, but for a whole game frame.
bool holdGameFrame(int fps, SkipWatch& skip) {
    return holdRetraces(70 * (145 / (fps > 0 ? fps : 1)) / 145, skip);
}

// The palette ramp both splashes fade with. They have their own .PAL, so this
// takes the raw bytes rather than reaching for the game's.
bool runSplashFade(SDL_Renderer* ren, SDL_Texture* tex,
                   const tubes::Screen& screen, const tubes::Bytes& palRaw,
                   std::vector<uint8_t>& rgba, int steps, bool in,
                   SkipWatch& skip) {
    if (steps <= 0) {
        // The fade is off. Cut to the screen, or to black on the way out.
        tubes::Palette pal;
        std::string err;
        if (in) tubes::loadPalette(palRaw, pal, err);
        tubes::presentScreen(ren, tex, screen, pal, rgba);
        return skip.pumped();
    }
    for (int i = 0; i <= steps; ++i) {
        const int n = in ? i : steps - i;
        tubes::presentScreen(ren, tex, screen, tubes::fadePalette(palRaw, n, steps),
                      rgba);
        // A skip cuts a fade-in short - there is no sense revealing a screen
        // the player has already dismissed - but never a fade-out, which has
        // to reach black or the next screen starts up lit.
        if (skip.pumped() && in) return true;
        SDL_Delay(1000 / 70);
    }
    return skip.key != 0;
}

// `21d5:007b`, the Software Creations splash. Returns the key that ended it.
//
//     Clear;                                     { 23df:0012 }
//     SetPalette(SOFT.PAL);                      { blanks the DAC as it stores }
//     Draw(0, 0, SOFT.GFX);                      { 23df:0022 }
//     FadeIn;                                    { 23e7:0097 }
//     Delay(10);
//     PlayAnm(SOFT.ANM);                         { 21d5:0000, nested }
//     n := 7;
//     repeat Delay(10); Dec(n); k := ReadInput until (k in [1,2]) or (n = 0);
//     FadeOut;  ClearKeyBuffer;
//
// The whole body sits under `if [DS:0x2a04] = 0`, a flag every screen function
// in the game tests and none of them writes - so it is set elsewhere, most
// likely when a resource fails to load. The port's equivalent is simply that
// the resources are there: if any of the three is missing the splash is
// skipped rather than half drawn.
int runSoftwareCreationsSplash(const tubes::Archive& res, SDL_Renderer* ren,
                               SDL_Texture* tex, tubes::Screen& screen,
                               std::vector<uint8_t>& rgba, int fadeSteps,
                               SkipWatch& skip, int shotFrame,
                               const std::string& shotPath) {
    tubes::Bytes palRaw, gfxRaw, anmRaw;
    tubes::Image still;
    std::vector<tubes::AnimFrame> anim;
    std::string err;
    if (!res.read("SOFT.PAL", palRaw, err) || palRaw.size() != 768 ||
        !res.read("SOFT.GFX", gfxRaw, err) ||
        !tubes::decodeGfx(gfxRaw, still, err) ||
        !res.read("SOFT.ANM", anmRaw, err) ||
        !tubes::decodeAnm(anmRaw, anim, err)) {
        return 0;
    }

    screen.clear(0);
    screen.blit(still, 0, 0);

    tubes::Palette pal;
    tubes::loadPalette(palRaw, pal, err);
    // Every stage below ends the screen the moment a key arrives, and the
    // fade-out always runs - the DAC has to reach black either way.
    auto leave = [&]() {
        const int k = skip.take();
        runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
        return k == 0 ? 2 : k;
    };
    if (runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, true, skip)) {
        return leave();
    }
    if (holdRetraces(10, skip)) return leave();

    // `21d5:0000`: three retraces a frame, and each frame paints only what it
    // changes over the still image already on screen.
    for (const tubes::AnimFrame& frame : anim) {
        if (holdRetraces(3, skip)) return leave();
        uint8_t* px = screen.pixelsMutable();
        for (const tubes::AnimFrame::Run& r : frame.runs) {
            for (size_t i = 0; i < r.pixels.size(); ++i) {
                const int d = r.offset + static_cast<int>(i);
                if (d >= 0 && d < tubes::kScreenWidth * tubes::kScreenHeight) {
                    px[d] = r.pixels[i];
                }
            }
        }
        tubes::presentScreen(ren, tex, screen, pal, rgba);
        // `--splash N --screenshot FILE`: capture the animation's Nth frame,
        // which is how this screen gets diffed against the original.
        if (shotFrame >= 0 &&
            shotFrame == static_cast<int>(&frame - anim.data())) {
            tubes::saveBmp(rgba, shotPath);
            return 2;
        }
    }

    // Seven holds of ten retraces - one second - and the only place the
    // original looks at the keyboard.
    for (int n = 7; n > 0; --n) {
        if (holdRetraces(10, skip)) return leave();
    }

    runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
    return 0;
}

// `2178:00eb`, the Absolute Magic splash - the second and much the larger of
// the two. Nine resources: `INTRO.PAL`, `CLOUD.GFX`, `AMWRITE.GFX`,
// `AMLOGO.SPR`, `LIGHTN.SPR`, `AMTHEME.MUS` and the three sounds.
//
// The backdrop is built rather than loaded. `2178:0000` clears a 64,000 byte
// buffer, copies `CLOUD.GFX` (320x83) in at offset 0, and then writes the same
// bytes again descending from offset 63,999 - so the bottom of the screen is
// the cloud band rotated 180 degrees, and rows 83..116 stay black. `21d0:0000`
// then de-chunks it into Mode X planes and `2321:0792` blits it. The port
// composes the same picture straight into its chunky framebuffer.
//
// Three phases, and each one sets the frame rate first:
//
//   * the logo arrives - `WOOSH.SFX`, then `AMLOGO.SPR`'s six frames centred,
//     20x20 growing to 172x127, at 9 fps;
//   * five lightning strikes - `LIGHTN.SFX` and one `LIGHTN.SPR` frame each,
//     at five hardcoded positions, at 4 fps, each with a white flash: the
//     splash allocates its own 768-byte palette, fills it with 63s, and
//     uploads it around the page flip before restoring `INTRO.PAL`;
//   * the writing - `ABSMAGIC.SFX`, the page cleared to black, the logo and
//     `AMWRITE.GFX` centred at (72, 88), then six holds of ten retraces.
//
// Index 0 is transparent, read out of `2321:09d0`'s inner loop (`OR AL,AL;
// JZ`) rather than assumed from the way the sprites look.
int runAbsoluteMagicSplash(const tubes::Archive& res, SDL_Renderer* ren,
                           SDL_Texture* tex, tubes::Screen& screen,
                           std::vector<uint8_t>& rgba, int fadeSteps,
                           SkipWatch& skip, tubes::MusicPlayer& music,
                           bool musicOn, bool soundOn, int shotStep,
                           const std::string& shotPath) {
    tubes::Bytes palRaw, cloudRaw, writeRaw, logoRaw, boltRaw, song;
    tubes::Image cloud, writing;
    std::vector<tubes::Image> logo, bolts;
    std::string err;
    if (!res.read("INTRO.PAL", palRaw, err) || palRaw.size() != 768 ||
        !res.read("CLOUD.GFX", cloudRaw, err) ||
        !tubes::decodeGfx(cloudRaw, cloud, err) ||
        !res.read("AMWRITE.GFX", writeRaw, err) ||
        // Planar, though it carries the 0xE5 prefix - `2321:0948` is what
        // draws it, and that routine walks four planes. See GfxLayout.
        !tubes::decodeGfx(writeRaw, writing, err,
                          tubes::GfxLayout::kPlanar) ||
        !res.read("AMLOGO.SPR", logoRaw, err) ||
        !tubes::decodeSpr(logoRaw, logo, err) ||
        !res.read("LIGHTN.SPR", boltRaw, err) ||
        !tubes::decodeSpr(boltRaw, bolts, err) ||
        logo.empty() || bolts.empty()) {
        return 0;
    }
    for (tubes::Image& im : logo) im.transparent = 0;
    for (tubes::Image& im : bolts) im.transparent = 0;
    writing.transparent = 0;

    // The three sounds are loaded here rather than from the atom-indexed
    // table.
    // The voice MUST be silenced before the sounds below go out of scope:
    // the audio callback holds a bare pointer at whichever one is playing.
    struct Silence {
        tubes::MusicPlayer& m;
        ~Silence() { m.stopSound(); }
    } silence{music};
    tubes::Sound woosh, bolt, magic;
    tubes::Bytes raw;
    const bool haveSfx =
        soundOn &&
        res.read("WOOSH.SFX", raw, err) && tubes::decodeSfx(raw, woosh, err) &&
        res.read("LIGHTN.SFX", raw, err) && tubes::decodeSfx(raw, bolt, err) &&
        res.read("ABSMAGIC.SFX", raw, err) && tubes::decodeSfx(raw, magic, err);

    tubes::Palette pal;
    tubes::loadPalette(palRaw, pal, err);
    // The all-63 palette the splash fills with `FillChar(p^, 768, 63)`.
    const tubes::Bytes whiteRaw(768, 0x3f);
    tubes::Palette white;
    tubes::loadPalette(whiteRaw, white, err);

    tubes::Screen bg;
    bg.clear(0);
    bg.blit(cloud, 0, 0);
    {
        uint8_t* px = bg.pixelsMutable();
        const int last = tubes::kScreenWidth * tubes::kScreenHeight - 1;
        for (size_t i = 0; i < cloud.pixels.size(); ++i) {
            const int d = last - static_cast<int>(i);
            if (d >= 0) px[d] = cloud.pixels[i];
        }
    }

    screen = bg;
    if (musicOn && music.isOpen() && res.read("AMTHEME.MUS", song, err)) {
        music.play(song, err);
    }
    auto leave = [&]() {
        const int k = skip.take();
        runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
        return k == 0 ? 2 : k;
    };
    if (runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, true, skip)) {
        return leave();
    }
    if (holdRetraces(30, skip)) return leave();

    const tubes::Image& big = logo.back();
    const int bigX = (tubes::kScreenWidth - big.width) / 2;
    const int bigY = (tubes::kScreenHeight - big.height) / 2;

    if (haveSfx) music.playSound(&woosh);
    for (size_t f = 0; f < logo.size(); ++f) {
        screen = bg;
        const tubes::Image& im = logo[f];
        screen.blit(im, (tubes::kScreenWidth - im.width) / 2,
                    (tubes::kScreenHeight - im.height) / 2);
        tubes::presentScreen(ren, tex, screen, pal, rgba);
        if (shotStep == static_cast<int>(f)) { tubes::saveBmp(rgba, shotPath); return 2; }
        if (holdGameFrame(9, skip)) return leave();   // `SetFrameRate(9)`
    }

    // `2178:0424` onward: five strikes, one arm each, coordinates as literals.
    static const int kBoltX[5] = {0x3f, 0x09, 0x11c, 0x2e, 0xb6};
    static const int kBoltY[5] = {0x07, 0xa4, 0x47, 0x32, 0x0f};
    for (size_t n = 0; n < bolts.size() && n < 5; ++n) {
        if (haveSfx) music.playSound(&bolt);
        screen = bg;
        screen.blit(bolts[n], kBoltX[n], kBoltY[n]);
        screen.blit(big, bigX, bigY);
        // `SetDAC(white)`, page flip, `SetDAC(INTRO.PAL)` - and `23e7:003d`
        // waits a retrace before each upload, so the flash is that long.
        tubes::presentScreen(ren, tex, screen, white, rgba);
        waitRetraces(1);
        tubes::presentScreen(ren, tex, screen, pal, rgba);
        if (shotStep == static_cast<int>(n) + 6) {
            tubes::saveBmp(rgba, shotPath);
            return 2;
        }
        if (holdGameFrame(4, skip)) return leave();   // `SetFrameRate(4)`
    }

    if (holdRetraces(15, skip)) return leave();
    if (haveSfx) music.playSound(&magic);
    screen.clear(0);                            // `2321:01e6`, the page clear
    screen.blit(big, bigX, bigY);
    screen.blit(writing, 0x48, 0x58);
    tubes::presentScreen(ren, tex, screen, pal, rgba);
    if (shotStep == 11) { tubes::saveBmp(rgba, shotPath); return 2; }
    for (int n = 6; n > 0; --n) {
        if (holdRetraces(10, skip)) return leave();
    }

    runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
    return 0;
}

// ---- The opening cutscene, `1b2e:1651` -------------------------------------
//
// Five pages of the game's own story. The page data is generated - see
// cutscene.h - so what is here is only the machinery: the two-track player,
// the panel, and the frame clock.


// The cutscene's own art: two frame lists of .GFX, loaded by name. Ten files
// fill 26 slots and sixteen fill 17, so the same Image is shared - which is
// exactly what the original does with its pointer copies.
struct CutsceneArt {
    std::vector<tubes::Image> writeFrames;   // 26
    std::vector<tubes::Image> blowFrames;    // 17
    tubes::Image base;                       // WRITE0, Lanny's lower half
    bool ok = false;
};

CutsceneArt loadCutsceneArt(const tubes::Archive& res) {
    CutsceneArt art;
    auto fill = [&](const char* const* names, int n,
                    std::vector<tubes::Image>& out) {
        out.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            // Opaque. `1b2e:0f46` draws every animation frame through
            // `2000:389d`, which is `2321:068d` - the opaque member of the
            // blit family, the same one `1b2e:0e37` stamps the professor's
            // wave frames with. So a frame replaces its whole w x h box,
            // index-0 pixels included, and those land as colour 0 rather than
            // as "leave what was there". That is the whole of the 144 pixels
            // this screen was out by: the port had these masked, so the base
            // pose showed through the bottom of a 28 x 66 frame where the
            // original had blacked it out.
            if (!tubes::loadImage(res, names[i], out[static_cast<size_t>(i)], -1)) {
                return false;
            }
        }
        return true;
    };
    art.ok = fill(tubes::kWriteFrames, tubes::kWriteFrameCount,
                  art.writeFrames) &&
             fill(tubes::kBlowFrames, tubes::kBlowFrameCount, art.blowFrames) &&
             tubes::loadImage(res, "WRITE0.GFX", art.base, 0);
    return art;
}

// `1b2e:1651`. Returns the key that ended it - `1000:b224` stores it, and the
// attract arm at `1000:b28b` skips the demo when it is 2.
int runCutscene(const tubes::Archive& res, SDL_Renderer* ren, SDL_Texture* tex,
                tubes::Screen& screen, std::vector<uint8_t>& rgba,
                const tubes::Bytes& palRaw, const tubes::Palette& pal,
                int fadeSteps, SkipWatch& skip, tubes::MusicPlayer& music,
                bool musicOn, bool soundOn, const tubes::Image* board,
                bool haveBoard, const tubes::Sprite* atoms,
                const bool* haveAtom, const tubes::Font& small, bool haveSmall,
                int shotPage, int shotTick, const std::string& shotPath) {
    const CutsceneArt art = loadCutsceneArt(res);
    if (!art.ok || !haveBoard || !haveSmall) return 0;

    // The three sounds are this screen's own.
    // The voice MUST be silenced before the sounds below go out of scope:
    // the audio callback holds a bare pointer at whichever one is playing.
    struct Silence {
        tubes::MusicPlayer& m;
        ~Silence() { m.stopSound(); }
    } silence{music};
    tubes::Sound sounds[3];
    const char* const kNames[3] = {"WHATTHE.SFX", "NOOOO.SFX", "BUBBLE.SFX"};
    bool haveSound[3] = {};
    for (int i = 0; i < 3; ++i) {
        tubes::Bytes raw;
        std::string err;
        haveSound[i] = soundOn && res.read(kNames[i], raw, err) &&
                       tubes::decodeSfx(raw, sounds[i], err);
    }
    auto play = [&](const char* name) {
        if (!name) return;
        for (int i = 0; i < 3; ++i) {
            if (haveSound[i] && std::strcmp(name, kNames[i]) == 0) {
                music.playSound(&sounds[i]);
            }
        }
    };

    // A capture runs the pages at full speed: the frame sequence is what
    // has to be right, not the wall clock, and `--cutscene 4` would
    // otherwise sit through 36 seconds of the earlier pages first.
    const bool capturing = shotPage >= 0;

    // `[DS:0x1d6e]` and `[DS:0x1d6f]`, the two global frame counters. They
    // live across pages, which is what lets page 4 start part way in.
    int frameA = 0, frameB = 0;
    // The beaker is drawn once before the fade and then left on the page, so
    // it stays put through the pages where track B is driving something else
    // - page 2, where B is the eighth element's atom. The port recomposes
    // every frame, so it has to remember the last frame B left it on. The
    // diff found this: page 2 was missing the atoms inside the beaker.
    int beakerFrame = 0;
    // `1b2e:1e6b`: the fourth page ends by copying both animation rectangles
    // from the other video page (`2321:024d`, a page-to-page rect copy). What
    // that leaves is not symmetric, and the capture shows this: on page
    // 5 the original still shows Lanny in full and the beaker is gone. So the
    // copy restored a page that still had him and no longer had it.
    //
    // The page bookkeeping behind that is not fully traced - `[0x2376]` is
    // flipped once before each Animate rather than per frame, and which page
    // holds what by then depends on the whole run. The outcome is read off
    // seven captures that all agree; the mechanism is marked as unread.

    // The original draws onto a page, and the only thing that keeps that
    // distinguishable from "an overlay over the board" is that its blits are
    // opaque: a frame writes colour 0 where its art is transparent, and black
    // is not the same as letting the blackboard through. The port kept the
    // figures in a layer stamped with index 0 meaning "not painted", which is
    // exactly the difference the fourth page's 144 pixels measured. They are
    // drawn straight onto the composed page here instead.
    //
    // The base pose is the exception and stays masked: `1b2e:1a91` draws it
    // through `2000:3921`, the masked thunk, because it goes over the board.
    // A page's own furniture: the bevelled panel, the story text and, on page
    // 2, the eight elements' atoms. Drawn once, over whatever is there.
    auto layout = [&](const tubes::CutscenePage& page) {
        for (int i = 0; i < page.count; ++i) {
            const tubes::CutsceneItem& it = page.items[i];
            switch (it.kind) {
            case tubes::CutsceneItem::kBar:
                drawPanel(screen, it.x, it.y, it.a, it.b);
                break;
            case tubes::CutsceneItem::kText:
                tubes::drawText(screen, small, it.x, it.y,
                                static_cast<uint8_t>(it.a),
                                static_cast<uint8_t>(it.b), it.text);
                break;
            case tubes::CutsceneItem::kAtom:
                if (it.a > 0 && it.a < tubes::kCellStates && haveAtom[it.a]) {
                    screen.draw(atoms[it.a], it.x, it.y);
                }
                break;
            }
        }
    };

    // One tick's worth of figures, drawn onto the page after its furniture.
    auto drawFigures = [&](const tubes::CutscenePage& page, bool aLive,
                           bool bLive) {
        screen.blit(art.base, tubes::kCutsceneBaseX, tubes::kCutsceneBaseY);
        if (!aLive && !bLive) {
            // Past the fourth page. `1b2e:1e58`..`1e84` copy both animation
            // boxes from the other page, and each track's frozen frame is
            // what is left - which an opaque blit of those two frames over
            // the base pose reproduces exactly, box and all.
            screen.blit(art.writeFrames[tubes::kWriteFrameCount - 1], 86, 122);
            screen.blit(art.blowFrames[tubes::kBlowFrameCount - 1], 258, 119);
            return;
        }
        if (!bLive) {
            // The beaker is still on the page from the last tick that drew
            // it, even while B is driving the eighth element's atom.
            screen.blit(art.blowFrames[static_cast<size_t>(
                            beakerFrame < tubes::kBlowFrameCount
                                ? beakerFrame : 0)],
                        258, 119);
        }
        if (aLive) {
            screen.blit(art.writeFrames[static_cast<size_t>(
                            frameA < tubes::kWriteFrameCount ? frameA : 0)],
                        page.a.x, page.a.y);
        }
        if (bLive) {
            if (page.b.w == 16) {
                screen.blit(art.blowFrames[static_cast<size_t>(
                                beakerFrame < tubes::kBlowFrameCount
                                    ? beakerFrame : 0)],
                            258, 119);
                if (frameB > 0 && frameB < tubes::kCellStates && haveAtom[frameB]) {
                    screen.draw(atoms[frameB], page.b.x, page.b.y);
                }
            } else {
                screen.blit(art.blowFrames[static_cast<size_t>(
                                frameB < tubes::kBlowFrameCount ? frameB : 0)],
                            page.b.x, page.b.y);
            }
        }
    };

    // The scene with no page on it: `1b2e:1a80`..`1ab6`, which is what the
    // fade-in reveals. Three literal draws, and they are spelled out here
    // rather than routed through `drawFigures` because the first page has no
    // B track at all - its whole `CutsceneTrack` is zeroed, so asking
    // `drawFigures` for it put the beaker at `(page.b.x, page.b.y)` = (0, 0)
    // for the one frame before the fade. Reported from play as a glitch in
    // the top-left corner, and it was exactly that.
    auto scene = [&]() {
        screen.clear(0);
        screen.blit(*board, 0, tubes::kCutsceneBoardY);
        screen.blit(art.base, tubes::kCutsceneBaseX, tubes::kCutsceneBaseY);
        screen.blit(art.writeFrames[0], 86, 122);      // `1b2e:1a96`
        screen.blit(art.blowFrames[0], 258, 119);      // `1b2e:1aa9`
    };

    auto compose = [&](const tubes::CutscenePage& page, bool aLive,
                       bool bLive) {
        screen.clear(0);
        screen.blit(*board, 0, tubes::kCutsceneBoardY);
        // The page's furniture, then the animation over it. That is the
        // original's order - `1b2e:1cdf` draws the panel, the text and the
        // eight elements' atoms once, and the Animate call then paints on top
        // every tick - and it is the whole reason Flashium flashes: the
        // static draw beside its name is a type 4 ball, and track B cycles a
        // ball through colours 1..7 at the same coordinates. Stamping the
        // figures first put the static ball back on top and it never moved.
        layout(page);
        drawFigures(page, aLive, bLive);
    };

    if (musicOn && music.isOpen()) {
        tubes::Bytes song;
        std::string err;
        if (res.read(tubes::kCutsceneMusic, song, err)) music.play(song, err);
    }

    // `1b2e:1ad4`: the fade reveals the board and the two figures, with no
    // page text up yet - the first page's panel is drawn after it. The scene
    // has to be composed first: without this the fade brought up whatever the
    // Absolute Magic splash had left on the screen, which is what a player
    // sees as the splash's last frame corrupting.
    scene();
    if (runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, true, skip)) {
        const int k = skip.take();
        runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
        return k;
    }
    if (!capturing && holdRetraces(tubes::kCutsceneOpenDelay, skip)) {
        const int k = skip.take();
        runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
        return k;
    }

    int ended = 0;
    for (int p = 0; p < tubes::kCutscenePageCount && !ended; ++p) {
        const tubes::CutscenePage& page = tubes::kCutscenePages[p];
        // `1b2e:1b00`, `1cda`, `1d4e`, `1e08`: each page seeds the counters
        // it is about to drive. The generator does not carry these because
        // they are stores, not arguments - see cutscene.cpp's header.
        if (p == 0) frameA = 0;
        if (p == 1) frameB = 0;
        if (p == 2) frameB = 1;
        if (p == 3) { frameA = 10; frameB = 4; }

        // `[BP+0x32] * 7` iterations of `Delay(10)`, so `seconds` seconds.
        const int ticks = page.seconds * 7;
        bool aDone = page.a.count == 0, bDone = page.b.count == 0;
        for (int t = 0; t < ticks; ++t) {
            compose(page, !aDone, !bDone);
            tubes::presentScreen(ren, tex, screen, pal, rgba);
            if (shotPage == p && t == (shotTick >= 0 ? shotTick : ticks / 2)) {
                tubes::saveBmp(rgba, shotPath);
                return 2;
            }
            if (!capturing &&
                holdRetraces(tubes::kCutsceneFrameRetraces, skip)) {
                // `1b2e:112a` onward, transliterated: Enter or Space sets the
                // page's countdown to 1, so the page ends and the next one
                // begins; ESC does that and sets the return code to 2, which
                // is what leaves the cutscene. The port had every key ending
                // the whole thing, which made Enter a skip button rather than
                // the page-turner the original gives you.
                if (skip.take() == 2) ended = 2;
                break;
            }
            // Advance, then wrap - the original increments after drawing.
            if (!aDone) {
                if (page.a.soundFrame == frameA && page.a.sound) {
                    play(page.a.sound);
                }
                if (++frameA > page.a.count) {
                    // A one-shot track stops. The original simply stops
                    // drawing and its last frame stays on the page, so the
                    // port - which recomposes every tick - has to hold that
                    // frame rather than wrap to 1. Without this the explosion
                    // snapped back to an intact beaker at its own climax.
                    if (page.a.count == 25) {
                        frameA = page.a.count;
                        aDone = true;
                    } else {
                        frameA = 1;
                    }
                }
            }
            if (!bDone) {
                if (page.b.sound &&
                    (page.b.soundFrame == frameB ||
                     (page.b.soundFrame < 0 && !music.soundBusy()))) {
                    play(page.b.sound);
                }
                if (page.b.w == 60) beakerFrame = frameB;
                if (++frameB > page.b.count) {
                    if (page.b.count == 16) {
                        frameB = page.b.count;
                        bDone = true;
                    } else {
                        frameB = 1;
                    }
                }
            }
        }
        if (!ended) play(page.soundAfter);
        // `1b2e:1e6b`: the fourth page ends with the two box copies, and
        // then each track's frozen frame is what is left on the page.
        (void)0;
    }

    runSplashFade(ren, tex, screen, palRaw, rgba, fadeSteps, false, skip);
    return ended;
}

}  // namespace tubes
