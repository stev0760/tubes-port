// tubes-port - an SDL reimplementation of Tubes (Absolute Magic, 1994).
//
// Ships no game data. Assets are read at runtime from the user's own copy of
// the original game; point --gamedir at the directory holding TUBES.RES.

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game.h"
#include "gfx.h"
#include "mus.h"
#include "opl.h"
#include "res.h"
#include "screen.h"

namespace {

// Playfield geometry.
//
// The cell size is real: atom sprites measure 16x13. The tube walls in
// GAMEFG.GFX occupy x 10..73 and x 246..309, leaving x 74..245 clear, so the
// grid sits in that gap. Column and row counts are a playable guess, not
// recovered from the original - see docs/reversing-notes.md.
constexpr int kCellW = 16;
constexpr int kCellH = 13;
// Recovered from 1000:3a67: three parallel grids are indexed [i * 6 + j] with
// the inner loop running 1..6 and the outer 1..5. So the playfield is 6 x 5,
// not the 7 x 10 guessed from the manual. Which index is the column is not
// yet proven - see docs/reversing-notes.md.
constexpr int kCols = 6;
constexpr int kRows = 5;
constexpr int kGridX = (tubes::kScreenWidth - kCols * kCellW) / 2;
constexpr int kGridY = 190 - kRows * kCellH;
constexpr int kTubeY = kGridY - kCellH - 2;
constexpr float kFallHeight = static_cast<float>(kTubeY - 16);

const char* kAtomSprites[tubes::kAtomCount] = {
    "REDBALL.CSP",  "BLUEBALL.CSP", "GRENBALL.CSP", "YELWBALL.CSP",
    "PURPBALL.CSP", "CYANBALL.CSP", "PINKBALL.CSP", "GOLDBALL.CSP",
};

struct Options {
    std::string gameDir = ".";
    int scale = 0;              // 0 = pick the largest that fits
    std::string screenshot;     // render one frame here and exit
    int autoFrames = 0;         // simulate N scripted frames first
    std::string music = "TUBES.MUS";   // song to play; empty disables audio
    std::string renderMus;      // render a song to WAV and exit
    std::string dumpRegs;       // print the OPL register stream and exit
    double renderSeconds = 0;   // 0 = one pass, songs loop forever
    bool help = false;
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "--gamedir" || a == "-g") && i + 1 < argc) {
            o.gameDir = argv[++i];
        } else if (a == "--scale" && i + 1 < argc) {
            o.scale = std::atoi(argv[++i]);
        } else if (a == "--screenshot" && i + 1 < argc) {
            o.screenshot = argv[++i];
        } else if (a == "--auto" && i + 1 < argc) {
            o.autoFrames = std::atoi(argv[++i]);
        } else if (a == "--music" && i + 1 < argc) {
            o.music = argv[++i];
        } else if (a == "--no-music") {
            o.music.clear();
        } else if (a == "--render-mus" && i + 2 < argc) {
            o.music = argv[++i];
            o.renderMus = argv[++i];
        } else if (a == "--seconds" && i + 1 < argc) {
            o.renderSeconds = std::atof(argv[++i]);
        } else if (a == "--dump-regs" && i + 1 < argc) {
            o.dumpRegs = argv[++i];
        } else if (a == "--help" || a == "-h") {
            o.help = true;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
            o.help = true;
        }
    }
    return o;
}

void usage() {
    std::printf(
        "tubes-port - SDL reimplementation of Tubes\n"
        "\n"
        "  --gamedir DIR     directory holding your TUBES.RES (default: .)\n"
        "  --scale N         integer scale factor (default: fit the display)\n"
        "  --screenshot FILE render one frame to a BMP and exit\n"
        "  --auto N          simulate N scripted frames first (for testing)\n"
        "  --music NAME      song to play (default: TUBES.MUS)\n"
        "  --no-music        start silent\n"
        "  --render-mus NAME OUT.wav   render a song to WAV and exit\n"
        "  --seconds N       length for --render-mus (default: one pass)\n"
        "  --dump-regs NAME  print the OPL2 register stream and exit\n"
        "  --help\n"
        "\n"
        "Controls: left/right move the test tube, Down speeds the atom,\n"
        "Ctrl or Space releases a held atom into the beaker, Esc quits.\n"
        "\n"
        "You need your own copy of Tubes; no game data ships with this.\n");
}

bool loadImage(const tubes::Archive& res, const std::string& name,
               tubes::Image& out, int transparent) {
    tubes::Bytes raw;
    std::string err;
    if (!res.read(name, raw, err) || !tubes::decodeGfx(raw, out, err)) {
        std::fprintf(stderr, "  %s: %s\n", name.c_str(), err.c_str());
        return false;
    }
    out.transparent = transparent;
    return true;
}

bool loadSprite(const tubes::Archive& res, const std::string& name,
                tubes::Sprite& out) {
    tubes::Bytes raw;
    std::string err;
    if (!res.read(name, raw, err) || !tubes::decodeCsp(raw, out, err)) {
        std::fprintf(stderr, "  %s: %s\n", name.c_str(), err.c_str());
        return false;
    }
    return true;
}

uint8_t readKeyboard() {
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    uint8_t b = 0;
    if (k[SDL_SCANCODE_UP]) b |= tubes::button::kUp;
    if (k[SDL_SCANCODE_DOWN]) b |= tubes::button::kDown;
    if (k[SDL_SCANCODE_LEFT]) b |= tubes::button::kLeft;
    if (k[SDL_SCANCODE_RIGHT]) b |= tubes::button::kRight;
    if (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL] ||
        k[SDL_SCANCODE_SPACE]) {
        b |= tubes::button::kA;
    }
    if (k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT]) b |= tubes::button::kB;
    return b;
}

// A scripted player, used by --auto so the game loop can be exercised
// headless. It stacks like colours together, which is enough to drive real
// matches and cascades rather than scattering atoms at random.
uint8_t scriptedInput(const tubes::Game& game) {
    uint8_t b = tubes::button::kDown;

    if (game.heldAtom() != tubes::kEmpty) {
        const tubes::Board& board = game.board();
        int target = -1;

        // Prefer a column whose topmost atom already matches what we hold.
        for (int c = 0; c < board.cols(); ++c) {
            int free = board.dropRow(c);
            if (free < 0) continue;
            if (free + 1 < board.rows() &&
                board.at(c, free + 1) == game.heldAtom()) {
                target = c;
                break;
            }
        }
        // Otherwise use the emptiest column, to keep the beaker level.
        if (target < 0) {
            int best = -1;
            for (int c = 0; c < board.cols(); ++c) {
                int free = board.dropRow(c);
                if (free > best) {
                    best = free;
                    target = c;
                }
            }
        }
        if (target < 0) return b;

        if (game.tubeColumn() < target) return b | tubes::button::kRight;
        if (game.tubeColumn() > target) return b | tubes::button::kLeft;
        return b | tubes::button::kA;
    }

    // Nothing held: line up under the falling atom to catch it.
    if (game.falling().active && game.falling().column != game.tubeColumn()) {
        return b | (game.falling().column > game.tubeColumn()
                        ? tubes::button::kRight
                        : tubes::button::kLeft);
    }
    return b;
}

}  // namespace

// One pass of a song, in seconds. Songs loop forever, so an offline render
// needs an explicit length from somewhere.
double songSeconds(const tubes::Bytes& song) {
    std::vector<tubes::MusEvent> events;
    std::string err;
    if (!tubes::parseMus(song, events, err)) return 0.0;
    uint32_t ticks = 0;
    for (const auto& ev : events) ticks += ev.delta;
    return ticks / tubes::kMusTickHz;
}

bool writeWav(const std::string& path, const std::vector<int16_t>& samples,
              int sampleRate, std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        error = "cannot write " + path;
        return false;
    }
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
    const uint16_t channels = 2, bits = 16;
    const uint32_t byteRate = sampleRate * channels * bits / 8;
    const uint16_t blockAlign = channels * bits / 8;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);                     // PCM
    u16(channels);
    u32(static_cast<uint32_t>(sampleRate));
    u32(byteRate);
    u16(blockAlign);
    u16(bits);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    std::fwrite(samples.data(), 1, dataBytes, f);
    std::fclose(f);
    return true;
}

// Prints every register write the sequencer makes, for diffing against
// tools/mus_decode.py. If the two streams agree, the port is correct
// independently of whether the synthesis sounds right.
int dumpRegisters(const tubes::Archive& drivers, const tubes::Bytes& song,
                  std::string& err) {
    tubes::RegisterLog log;
    const int ticks = static_cast<int>(songSeconds(song) * tubes::kMusTickHz) + 2;
    if (!tubes::MusicPlayer::logRegisters(drivers, song, ticks, log, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    for (const auto& w : log.writes) {
        std::printf("%u %02x %02x\n", w.tick, w.reg, w.value);
    }
    return 0;
}

int renderSong(const tubes::Archive& drivers, const tubes::Bytes& song,
               const std::string& out, double seconds, std::string& err) {
    if (seconds <= 0.0) seconds = songSeconds(song);
    if (seconds <= 0.0) {
        std::fprintf(stderr, "error: could not determine song length\n");
        return 1;
    }
    constexpr int kRate = 44100;
    std::vector<int16_t> samples;
    if (!tubes::MusicPlayer::renderOffline(drivers, song, seconds, kRate,
                                           samples, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (!writeWav(out, samples, kRate, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("wrote %s (%.2f s, %zu frames)\n", out.c_str(), seconds,
                samples.size() / 2);
    return 0;
}

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    if (opt.help) {
        usage();
        return 0;
    }

    const std::string resPath = opt.gameDir + "/TUBES.RES";
    tubes::Archive res;
    std::string err;
    if (!res.open(resPath, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        std::fprintf(stderr,
                     "\nPoint --gamedir at a directory containing TUBES.RES "
                     "from your copy of the game.\n");
        return 1;
    }
    std::printf("opened %s (%zu resources)\n", resPath.c_str(),
                res.entries().size());

    // DRIVERS.RES holds FMMUSIC.DRV, whose tables the sequencer needs. Music
    // is optional: a missing or broken driver must not stop the game.
    tubes::Archive drivers;
    std::string driverErr;
    const bool haveDrivers =
        drivers.open(opt.gameDir + "/DRIVERS.RES", driverErr);

    if (!opt.dumpRegs.empty() || !opt.renderMus.empty()) {
        if (!haveDrivers) {
            std::fprintf(stderr, "error: %s\n", driverErr.c_str());
            return 1;
        }
        tubes::Bytes song;
        if (!res.read(opt.music, song, err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        if (!opt.dumpRegs.empty()) return dumpRegisters(drivers, song, err);
        return renderSong(drivers, song, opt.renderMus, opt.renderSeconds, err);
    }

    tubes::Bytes palRaw;
    tubes::Palette pal;
    if (!res.read("TUBES.PAL", palRaw, err) ||
        !tubes::loadPalette(palRaw, pal, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    tubes::Image background;
    tubes::Image foreground;
    bool haveBg = loadImage(res, "GAMEBG1.GFX", background, -1);
    bool haveFg = loadImage(res, "GAMEFG.GFX", foreground, 0);

    tubes::Sprite atoms[tubes::kAtomCount];
    int loaded = 0;
    for (int i = 0; i < tubes::kAtomCount; ++i) {
        if (loadSprite(res, kAtomSprites[i], atoms[i])) ++loaded;
    }
    tubes::Sprite testTube;
    bool haveTube = loadSprite(res, "TESTUBE1.CSP", testTube);
    std::printf("loaded %d/%d atoms, test tube %s\n", loaded,
                static_cast<int>(tubes::kAtomCount),
                haveTube ? "ok" : "missing");

    tubes::Game game(kCols, kRows, tubes::Difficulty::k101, 0x9E3779B9u);
    game.setFallHeight(kFallHeight);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    int scale = opt.scale;
    if (scale <= 0) {
        SDL_DisplayMode dm;
        scale = 3;
        if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
            int fit = std::min(dm.w / tubes::kScreenWidth,
                               dm.h / tubes::kScreenHeight);
            scale = std::max(1, std::min(fit - 1, 6));
        }
    }

    SDL_Window* win = SDL_CreateWindow(
        "Tubes", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        tubes::kScreenWidth * scale, tubes::kScreenHeight * scale,
        SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren =
        win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : nullptr;
    if (win && !ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!win || !ren) {
        std::fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         tubes::kScreenWidth,
                                         tubes::kScreenHeight);

    // Run the scripted player before drawing, so a headless screenshot shows
    // a populated beaker rather than the empty opening frame.
    for (int f = 0; f < opt.autoFrames; ++f) {
        (void)f;
        game.update(scriptedInput(game), 1.0f / 60.0f);
    }
    if (opt.autoFrames) {
        std::printf(
            "simulated %d frames: %d atoms, score %d, chains %d, drops %d/%d%s\n",
            opt.autoFrames, game.board().count(), game.score(), game.chains(),
            game.drops(), game.dropLimit(), game.gameOver() ? ", GAME OVER" : "");
    }

    // Music is best-effort: a missing DRIVERS.RES or a busy audio device
    // must not stop the game from being playable.
    tubes::MusicPlayer music;
    if (!opt.music.empty() && opt.screenshot.empty()) {
        std::string musicErr;
        tubes::Bytes song;
        if (!haveDrivers) {
            std::fprintf(stderr, "music disabled: %s\n", driverErr.c_str());
        } else if (!music.open(drivers, musicErr) ||
                   !res.read(opt.music, song, musicErr) ||
                   !music.play(song, musicErr)) {
            std::fprintf(stderr, "music disabled: %s\n", musicErr.c_str());
        } else {
            std::printf("playing %s through Nuked-OPL3\n", opt.music.c_str());
        }
    }

    tubes::Screen screen;
    std::vector<uint8_t> rgba;
    bool running = true;
    Uint32 last = SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN &&
                (ev.key.keysym.sym == SDLK_ESCAPE ||
                 ev.key.keysym.sym == SDLK_q)) {
                running = false;
            }
        }

        Uint32 now = SDL_GetTicks();
        float dt = static_cast<float>(now - last) / 1000.0f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;    // a stall must not teleport atoms

        if (opt.screenshot.empty()) game.update(readKeyboard(), dt);

        screen.clear(0);
        if (haveBg) screen.blit(background);
        if (haveFg) screen.blit(foreground);

        const tubes::Board& b = game.board();
        for (int r = 0; r < b.rows(); ++r) {
            for (int c = 0; c < b.cols(); ++c) {
                int8_t v = b.at(c, r);
                if (v == tubes::kEmpty) continue;
                screen.draw(atoms[v], kGridX + c * kCellW, kGridY + r * kCellH);
            }
        }

        const tubes::Falling& f = game.falling();
        if (f.active && f.colour != tubes::kEmpty) {
            screen.draw(atoms[f.colour], kGridX + f.column * kCellW,
                        static_cast<int>(f.y));
        }
        if (haveTube) {
            screen.draw(testTube, kGridX + game.tubeColumn() * kCellW - 3,
                        kTubeY - 40);
        }
        if (game.heldAtom() != tubes::kEmpty) {
            screen.draw(atoms[game.heldAtom()],
                        kGridX + game.tubeColumn() * kCellW, kTubeY);
        }

        screen.toRgba(pal, rgba);
        SDL_UpdateTexture(tex, nullptr, rgba.data(), tubes::kScreenWidth * 4);

        int winW = 0, winH = 0;
        SDL_GetRendererOutputSize(ren, &winW, &winH);
        int s = std::max(1, std::min(winW / tubes::kScreenWidth,
                                     winH / tubes::kScreenHeight));
        SDL_Rect dst{(winW - tubes::kScreenWidth * s) / 2,
                     (winH - tubes::kScreenHeight * s) / 2,
                     tubes::kScreenWidth * s, tubes::kScreenHeight * s};

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        SDL_RenderPresent(ren);

        if (!opt.screenshot.empty()) {
            SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
                rgba.data(), tubes::kScreenWidth, tubes::kScreenHeight, 32,
                tubes::kScreenWidth * 4, SDL_PIXELFORMAT_RGBA32);
            if (surf) {
                SDL_SaveBMP(surf, opt.screenshot.c_str());
                SDL_FreeSurface(surf);
                std::printf("wrote %s\n", opt.screenshot.c_str());
            }
            running = false;
        }

        SDL_Delay(16);
    }

    music.stop();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
