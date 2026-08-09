// The port's drawing, everything that does not need SDL. See screens.h.

#include "screens.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "wave_text.h"

namespace tubes {

// Used only by the screens below, so `static` rather than declared in
// screens.h - they are not part of anything's interface.

static std::string padLeft(const std::string& s, size_t w);

static void drawScene(tubes::Screen& screen, const tubes::Image* board, bool haveBoard,
               const SceneArt& art, const ScenePose& pose);

static void drawTypingCursor(tubes::Screen& screen, int cx, int cy, int phase);

static void fillHiScorePanel(tubes::Screen& screen);

static std::string hiScoreScoreText(uint32_t score);


// The HUD, transliterated from `1000:42ae` (the labels, drawn once at session
// setup) and `1000:5707` (the numbers, redrawn every frame).
//
//     SetFont(small);                                    { 8 x 8, advance 6 }
//     OutText(  1, 1, 127, $81, 'Chains');
//     OutText(288, 1, 127, $81, 'Drops');
//     SetFont(big);                                     { 8 x 16, advance 8 }
//     ...
//     OutTextCentred(0, 319, 0, 127, $81, Str(score));
//     OutText(32, 0, 127, $81, Str(chains:3));
//     if (drops > 0) and (drops < 255) then
//         OutText(265, 0, 127, $81, Str(drops))
//     else begin
//         SetFont(small);  OutText(270, 1, 127, $81, 'No');  SetFont(big)
//     end
//
// `$81` is the shadow bit plus mode 1, so every glyph is drawn twice: once
// flat in index 0 at (x+1, y+1), then again walking down the palette one index
// per scanline from 127. The palette holds a cyan ramp at 112..127, which is
// where the HUD's colour comes from - there is no second colour constant
// anywhere. Confirmed against a captured frame: the pixels of "Chains" read
// 127, 126, 125 ... down its eight rows, exactly one per scanline.
//
// The width-3 field on `chains` is the reason its digit sits at x = 48 rather
// than 32 - the two leading spaces advance without drawing.
void drawHud(tubes::Screen& screen, const tubes::Game& game,
             const tubes::Font& big, const tubes::Font& small, bool haveBig,
             bool haveSmall) {
    constexpr uint8_t kHudColour = 127;
    constexpr uint8_t kHudMode = tubes::textmode::kShadow |
                                 tubes::textmode::kFadeDown;

    if (haveSmall) {
        tubes::drawText(screen, small, 1, 1, kHudColour, kHudMode, "Chains");
        tubes::drawText(screen, small, 288, 1, kHudColour, kHudMode, "Drops");
    }
    if (!haveBig) return;

    tubes::drawTextCentred(screen, big, 0, 319, 0, kHudColour, kHudMode,
                           std::to_string(game.score()));

    std::string chains = std::to_string(game.chains());
    while (chains.size() < 3) chains.insert(chains.begin(), ' ');
    tubes::drawText(screen, big, 32, 0, kHudColour, kHudMode, chains);

    // `1000:576f`. The 255 arm is the drop counter having wrapped past zero,
    // which is the game-over condition - so "No" is on screen for the frame
    // that ends the session as well as for the last one before it.
    const int drops = game.dropsRemaining();
    if (drops > 0 && drops < 255) {
        tubes::drawText(screen, big, 265, 0, kHudColour, kHudMode,
                        std::to_string(drops));
    } else if (haveSmall) {
        tubes::drawText(screen, small, 270, 1, kHudColour, kHudMode, "No");
    }

    // The award in flight, centred under the score - `1000:582a`. Colour 168
    // with mode 3, which brightens to the middle of the cell and dims again
    // rather than ramping one way.
    if (!haveSmall || game.scorePending() <= 0) return;
    constexpr uint8_t kPopColour = 168;
    constexpr uint8_t kPopMode = tubes::textmode::kShadow |
                                 tubes::textmode::kPeak;
    tubes::drawTextCentred(screen, small, 0, 319, 13, kPopColour, kPopMode,
                           "+" + std::to_string(game.scorePending()));
    // `1000:586a` - the multiplier only appears when it is worth more than
    // one.
    if (game.scoreMultiplier() > 1) {
        tubes::drawTextCentred(screen, small, 0, 319, 21, kPopColour, kPopMode,
                               "x" + std::to_string(game.scoreMultiplier()));
    }
}

// The briefing screen, `1000:86b8`. The dispatch half lives in wave.cpp; this
// is its presentation, and every coordinate here is a literal the original
// pushes - see wave_text.cpp.
//
//     load GAMEBG<Random(10)+1>, re-rolled until it differs from the last
//     SetFont(big);   OutTextCentred(0, 319, 45, 159, 3, 'Wave ' + Str(n))
//                     OutTextCentred(0, 319, 48, 159, 3, '____________')
//     SetFont(small); <the objective routine's lines and illustration>
//                     OutText(76, 150, 155, 1, 'You are allowed N drops.')
//     wait for a key
//
// The one thing not settled from code is the justification of the padded
// fields. Turbo Pascal's `:width` right-justifies and `Str(n:2)` clearly does,
// so the colour names are right-justified here too; no capture of an original
// briefing has been taken to confirm it.
static std::string padLeft(const std::string& s, size_t w) {
    return s.size() >= w ? s : std::string(w - s.size(), ' ') + s;
}

static void drawScene(tubes::Screen& screen, const tubes::Image* board, bool haveBoard,
               const SceneArt& art, const ScenePose& pose) {
    const int slideX = pose.slideX;
    const int slideY = pose.slideY;
    const int profFrame = pose.profFrame;
    const tubes::Image* corners = art.corners;
    const bool* haveCorner = art.haveCorner;
    screen.clear(0);
    // `1000:8db4` / the briefing: the held image goes to (0, 12), not to the
    // origin. The port drew it at (0, 0) for several sessions and the pixel
    // diff never caught it, because every region ever measured was inside the
    // white slide - where the two agree by construction.
    if (haveBoard) screen.blit(*board, 0, tubes::kBoardY);

    uint8_t* px = screen.pixelsMutable();
    auto fillRect = [&](int x, int y, int w, int h, uint8_t colour) {
        for (int yy = y; yy < y + h; ++yy) {
            if (yy < 0 || yy >= tubes::kScreenHeight) continue;
            for (int xx = x; xx < x + w; ++xx) {
                if (xx < 0 || xx >= tubes::kScreenWidth) continue;
                px[static_cast<size_t>(yy) * tubes::kScreenWidth + xx] = colour;
            }
        }
    };

    // `1b2e:0656` runs before the frame and slide, so the professor, his books
    // and the roller bar go down first - and the frame paints over none of
    // them, because it spans x 62..257 and he stands at 267.
    // The professor is two draws, and the sprite sizes are what say so:
    // `POINTER0` is 44 x 79 - the whole figure, legs and book stack - while
    // `POINTER1..3` are 44 x 39, his upper body only. `1b2e:0656` lays down
    // `POINTER0` masked (`2321:0711`), and then `1b2e:0e37` stamps the wave
    // frame opaquely (`2321:068d`) over his top half once every ten retraces.
    // Drawing only the wave frame erases him from the waist down; drawing it
    // masked leaves the base pose's arm showing through it.
    //
    // No separate `BOOKS.GFX` draw: the normal arm never reaches one. `BOOKS`
    // is used by the clap and jump arms, where he stands at a different
    // height.
    if (pose.jumpFrame > 0) {
    // `1b2e:0656`'s `DS:0x20e3` arm: `BOOKS` at its own place, then the hop
    // frame at `(267, 97 - 3 * frame)`. Both masked - the arm uses
    // `2321:0711` and `1b2e:0b8f` uses `2000:3921`, which is the same
    // routine. No `POINTER0` here at all; the jump frames are the whole
    // figure, 71 tall against the standing pose's 79.
        if (art.haveBooks) {
            screen.blit(*art.books, tubes::kJumpBooksX, tubes::kJumpBooksY);
        }
        const int f = pose.jumpFrame;
        if (art.haveJump && f >= 1 && f <= tubes::kJumpFrames &&
            art.haveJump[f - 1]) {
            screen.blit(art.jump[f - 1], tubes::kJumpX, tubes::jumpY(f));
        }
    } else {
    if (art.havePointer && art.havePointer[0]) {
        screen.blit(art.pointer[0], tubes::kProfX, tubes::kProfY);
    }
    if (art.havePointer && profFrame > 0 && profFrame < 4 &&
        art.havePointer[profFrame]) {
        screen.blit(art.pointer[profFrame], tubes::kProfX, tubes::kProfY);
    }
    }
    // `1b2e:084e` stamps `POINTERT` over whatever pose is up, which is why it
    // comes after both of the draws above and not instead of them.
    if (pose.jokeFace && art.havePointerT) {
        screen.blit(*art.pointerT, tubes::kProfX, tubes::kProfY);
    }
    // The mouth, `1b2e:0cd1`, stamped over his face at (276, 133) - a third
    // draw on top of the two above, and only while he is talking. The wave
    // frames carry their own mouth, so `ProfessorIdle` reports 0 for this the
    // moment the gesture starts.
    // ...and only while he is standing. The mouth's (276, 133) is nine right
    // and twelve down from the standing pose's origin; the jump arm puts him
    // at (267, 94..88), so (276, 133) is over blackboard.
    // Structurally it cannot happen in the original either - the mouth is
    // drawn by `1b2e:0cd1` and the jump by `1b2e:0b8f`, and no screen runs
    // both - but the port drives one professor from two clocks, so it has to
    // be said. Reported from play: a mouth left floating beside him.
    if (pose.jumpFrame == 0 && art.haveTalk && pose.mouthFrame >= 1 &&
        pose.mouthFrame <= 5 && art.haveTalk[pose.mouthFrame - 1]) {
        screen.blit(art.talk[pose.mouthFrame - 1], tubes::kTalkX,
                    tubes::kTalkY);
    }

    // The frame behind the slide. Its height is the one thing about it that
    // moves: `1b2e:0656` reads `DS:0xbba` for it and `1b2e:0510` steps that
    // word down the `kRollDown` table when the scene is first built.
    fillRect(tubes::kFrameX, tubes::kFrameY, tubes::kFrameW, pose.frameH,
             tubes::kFrameColour);
    // The roller bar rides the frame's bottom edge - `Draw(57, DS:0xbba + 26)`
    // in `1b2e:0656`, where `DS:0xbba` is the frame height as it rolls down.
    if (art.haveBar) {
        screen.blit(*art.bar, tubes::kBarX, pose.frameH + tubes::kBarDY);
    }
    // While the screen is still rolling there is no slide on it - `1b2e:0510`
    // draws only the growing rect and the bar, and the slide arrives with the
    // `1b2e:0a11` that follows. Putting it down early leaves a white panel
    // hanging in front of a screen that has not reached it yet.
    if (pose.frameH < tubes::kFrameH) return;

    // The slide itself. Its resting place, (74, 31, 172, 132, 17), is exactly
    // the rectangle this file used to carry as "measured, not decompiled" -
    // the measurement was right, and it is now derived.
    fillRect(slideX, slideY, tubes::kSlideW, tubes::kSlideH, tubes::kSlideColour);

    // Four 4x4 corner clips, `UL`/`UR`/`DL`/`DRCORNER.GFX`, each 20 bytes = a
    // 4-byte header plus 4 x 4. `1b2e:097a` places them at the slide's origin
    // plus (0,0), (168,0), (0,128) and (168,128).
    const int cx[4] = {slideX, slideX + tubes::kCornerDX, slideX, slideX + tubes::kCornerDX};
    const int cy[4] = {slideY, slideY, slideY + tubes::kCornerDY, slideY + tubes::kCornerDY};
    for (int i = 0; i < 4; ++i) {
        if (haveCorner[i]) screen.blit(corners[i], cx[i], cy[i]);
    }

    // The wrong slide goes on the blank one, in `1b2e:084e`'s own order:
    // `FillRect`, the four corners, then `FLASH.GFX` at the slide's origin.
    if (pose.jokeSlide && art.haveFlash) {
        screen.blit(*art.flash, slideX, slideY);
    }
}

void drawBriefing(tubes::Screen& screen, const tubes::Game& game,
                  const tubes::Image* bg, bool haveBg,
                  const tubes::Font& big, const tubes::Font& small,
                  bool haveBigF, bool haveSmallF,
                  const tubes::Sprite* atoms, const bool* haveAtom,
                  const tubes::Sprite* furn, const bool* haveFurn,
                  int8_t decorBall, const SceneArt& art,
                  const ScenePose& pose) {
    drawScene(screen, bg, haveBg, art, pose);
    if (pose.slideIsBusy()) return;

    const tubes::WaveObjective& obj = game.objective();
    const int count = obj.counter;
    const int8_t colour = obj.reqColour >= 1 && obj.reqColour <= 7
                              ? obj.reqColour
                              : static_cast<int8_t>(1);
    const std::string name = tubes::kElementNames[colour];

    if (haveBigF) {
        tubes::drawTextCentred(screen, big, 0, 319, tubes::kBriefTitleY,
                               tubes::kBriefTitleColour, tubes::kBriefTitleMode,
                               tubes::kBriefTitle +
                                   std::to_string(game.progress().wave));
        tubes::drawTextCentred(screen, big, 0, 319, tubes::kBriefRuleY,
                               tubes::kBriefTitleColour, tubes::kBriefTitleMode,
                               tubes::kBriefRule);
    }
    if (!haveSmallF) return;

    const tubes::Briefing& b = tubes::briefingFor(
        tubes::objectiveForWave(game.progress().wave, game.edition()));
    for (int i = 0; i < b.lineCount; ++i) {
        const tubes::BriefLine& l = b.lines[i];
        std::string s;
        switch (l.fmt) {
            case tubes::BriefFmt::kLiteral:  s = l.a; break;
            case tubes::BriefFmt::kCount:
                s = std::string(l.a) + padLeft(std::to_string(count), 2) + l.b;
                break;
            case tubes::BriefFmt::kCountName:
                s = std::string(l.a) + padLeft(std::to_string(count), 2) + l.b +
                    padLeft(name, 9) + l.c;
                break;
            case tubes::BriefFmt::kName:
                s = std::string(l.a) + padLeft(name, 9) + l.b;
                break;
            case tubes::BriefFmt::kNameWide:
                s = std::string(l.a) +
                    padLeft(tubes::kElementNames[obj.disabledColour >= 1 &&
                                                         obj.disabledColour <= 7
                                                     ? obj.disabledColour
                                                     : 1],
                            10) +
                    l.b;
                break;
            case tubes::BriefFmt::kNameOnly: s = name; break;
        }
        if (l.x < 0) {
            tubes::drawTextCentred(screen, small, 0, 319, l.y, l.colour, l.mode, s);
        } else {
            tubes::drawText(screen, small, l.x, l.y, l.colour, l.mode, s);
        }
    }

    for (int i = 0; i < b.ballCount; ++i) {
        const tubes::BriefBall& ball = b.balls[i];
        int8_t type = colour;
        switch (ball.kind) {
            case tubes::BriefBallKind::kRequired: type = colour; break;
            case tubes::BriefBallKind::kCrystal:  type = tubes::kCrystal; break;
            case tubes::BriefBallKind::kRandom:   type = decorBall; break;
            case tubes::BriefBallKind::kMarker:
                if (haveFurn[kMarker]) screen.draw(furn[kMarker], ball.x, ball.y);
                continue;
        }
        if (type >= 1 && type < tubes::kTypeCount && haveAtom[type]) {
            screen.draw(atoms[type], ball.x, ball.y);
        }
    }

    tubes::drawText(screen, small, tubes::kBriefDropsX, tubes::kBriefDropsY,
                    tubes::kBriefBodyColour, 1,
                    std::string(tubes::kBriefDropsA) +
                        std::to_string(game.dropsRemaining()) +
                        tubes::kBriefDropsB);
}

// The end-of-session banner, `1000:5d64`. It is drawn by `1000:3a67` itself,
// over whatever the play field was left showing, rather than on a fresh
// screen - so the caller must not clear first.
void drawBanner(tubes::Screen& screen, tubes::Banner banner,
                const tubes::Font& heading, bool haveHeading,
                const tubes::Font& small, bool haveSmall, bool showHint) {
    if (banner == tubes::Banner::kNone || !haveHeading) return;
    const tubes::BannerText t = tubes::bannerText(banner);
    tubes::drawTextCentred(screen, heading, 0, 319, tubes::kBannerY,
                           tubes::kBannerColour, tubes::kBannerMode, t.line1);
    tubes::drawTextCentred(screen, heading, 0, 319, tubes::kBannerRuleY,
                           tubes::kBannerColour, tubes::kBannerMode, t.rule);
    // `1000:5ed7`, only on the abort arm and only when saving is enabled.
    if (showHint && haveSmall) {
        tubes::drawTextCentred(screen, small, 0, 319, tubes::kAbortHintY,
                               tubes::kBannerColour, tubes::kAbortHintMode,
                               tubes::kAbortHint);
    }
}

// The stats screen, `1000:8da5`. The rows are built once, when the screen is
// entered, because building them is what accumulates the running chain total -
// see `buildStatsScreen`. This function only draws what it is given.
void drawStats(tubes::Screen& screen, const std::vector<tubes::StatsRow>& rows,
               const tubes::Image* board, bool haveBoard, const SceneArt& art,
               const tubes::Font& heading, const tubes::Font& label,
               const tubes::Font& number, bool haveHeading, bool haveLabel,
               bool haveNumber, const ScenePose& pose) {
    // `1000:8db4` blits the held image through `2321:068d` at (0, 12) and then
    // calls `1b2e:0a11`, the same classroom scene the briefing uses - so the
    // stats land on the blackboard's white slide, not on the play backdrop.
    // The held image is `BLACKBRD.GFX`: 48644 bytes is exactly 320 x 152 plus
    // a header, and 12 + 152 = 164 is the only placement that fits.
    //
    // The slide is always at rest here - the drop animation belongs to the
    // first briefing and `DS:0x210e` has long since been set by this point.
    // So is the projector screen: `1000:8da5` reaches the scene through
    // `1b2e:0656`, which reads `DS:0xbba` at rest, not through `1b2e:0510`.
    drawScene(screen, board, haveBoard, art, pose);
    if (pose.slideIsBusy()) return;

    for (const tubes::StatsRow& r : rows) {
        const tubes::Font* f = nullptr;
        switch (r.font) {
        case tubes::StatsFont::kHeading: if (haveHeading) f = &heading; break;
        case tubes::StatsFont::kLabel:   if (haveLabel)   f = &label;   break;
        case tubes::StatsFont::kNumber:  if (haveNumber)  f = &number;  break;
        }
        if (!f) continue;
        tubes::drawTextCentred(screen, *f, 0, 319, r.y, r.colour, r.mode,
                               r.text);
    }
}

// The wave-75 ending, `1000:9499`. Two screens over the same classroom the
// stats and briefing use, with `DS:0x20e3` set so the professor hops.
//
//     page 1   `1b2e:0656`; `1b2e:0a11`; the heading and the story text
//     page 2   `1b2e:0a11` again - which wipes the slide - then `PRIZE.GFX` at
//              `((320 - w) div 2, (200 - h) div 2)`
//
// Each held by `1b2e:0b8f(0x1e)`, the jump wait: thirty seconds or a key.
void drawEnding(tubes::Screen& screen, int page, const tubes::Image* board,
                bool haveBoard, const SceneArt& art, const ScenePose& pose,
                const tubes::Font& heading, bool haveHeading,
                const tubes::Font& small, bool haveSmall,
                const tubes::Image* prize, bool havePrize) {
    drawScene(screen, board, haveBoard, art, pose);
    if (page >= 2) {
        // `1000:9644`: centred by size, not by a literal - 52 x 125 lands at
        // (134, 37), and computing it is what the original does.
        if (havePrize) {
            screen.blit(*prize, (tubes::kScreenWidth - prize->width) / 2,
                        (tubes::kScreenHeight - prize->height) / 2);
        }
        return;
    }
    for (int i = 0; i < tubes::kEndingLineCount; ++i) {
        const tubes::EndingLine& l = tubes::kEndingLines[i];
        const bool big = l.font == tubes::EndingFont::kHeading;
        if (big ? !haveHeading : !haveSmall) continue;
        const tubes::Font& f = big ? heading : small;
        if (l.centred) {
            tubes::drawTextCentred(screen, f, l.left, l.right, l.y, l.colour,
                                   l.mode, l.text);
        } else {
            tubes::drawText(screen, f, l.left, l.y, l.colour, l.mode, l.text);
        }
    }
}

// The Continue screen, `1000:8c38`. Drawn over whatever is already there - the
// original never clears, which is why the stats screen stays behind it.
void drawContinue(tubes::Screen& screen, int ticksLeft,
                  const tubes::Font& heading, bool haveHeading,
                  const tubes::Font& number, bool haveNumber) {
    if (haveHeading) {
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kContinueTitleY,
                               tubes::kContinueTitleColour, tubes::textmode::kPeak,
                               "Continue");
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kContinueRuleY,
                               tubes::kContinueTitleColour, tubes::textmode::kPeak,
                               "______");
    }
    if (haveNumber) {
        tubes::drawTextCentred(screen, number, 0, 319, tubes::kContinueCountY,
                               tubes::kContinueCountColour,
                               tubes::textmode::kFadeUp,
                               std::to_string(ticksLeft));
    }
}

// `1000:9757`'s typing cursor: a 4 x 4 block whose colour walks 0x91..0x9e and
// back, one step a frame, so it pulses rather than blinks. The save screen's
// description field uses the same loop and therefore the same cursor.
static void drawTypingCursor(tubes::Screen& screen, int cx, int cy, int phase) {
    uint8_t* px = screen.pixelsMutable();
    const uint8_t col = static_cast<uint8_t>(tubes::kHsCursorBase + phase);
    for (int yy = cy; yy < cy + tubes::kHsCursorSize; ++yy) {
        if (yy < 0 || yy >= tubes::kScreenHeight) continue;
        for (int xx = cx; xx < cx + tubes::kHsCursorSize; ++xx) {
            if (xx < 0 || xx >= tubes::kScreenWidth) continue;
            px[static_cast<size_t>(yy) * tubes::kScreenWidth + xx] = col;
        }
    }
}

// `2321:060b(10, 37, 299, 118, 111)`, the panel both high-score screens put
// over the chalkboard. It is what hides the equations chalked into
// `BLACKBRD.GFX`, so the board reads as blank behind the table.
static void fillHiScorePanel(tubes::Screen& screen) {
    uint8_t* px = screen.pixelsMutable();
    for (int y = tubes::kHsPanelY; y < tubes::kHsPanelY + tubes::kHsPanelH; ++y) {
        if (y < 0 || y >= tubes::kScreenHeight) continue;
        for (int x = tubes::kHsPanelX;
             x < tubes::kHsPanelX + tubes::kHsPanelW; ++x) {
            if (x < 0 || x >= tubes::kScreenWidth) continue;
            px[static_cast<size_t>(y) * tubes::kScreenWidth + x] =
                tubes::kHsPanelColour;
        }
    }
}

// `Str(score:10)`: right-justified in ten characters, which is what puts the
// digits' right edge in the same column on every row.
static std::string hiScoreScoreText(uint32_t score) {
    std::string s = std::to_string(score);
    if (static_cast<int>(s.size()) < tubes::kHsScoreWidth) {
        s.insert(s.begin(),
                 tubes::kHsScoreWidth - static_cast<int>(s.size()), ' ');
    }
    return s;
}

// The high-score entry screen, `1000:96db`. It draws over the classroom scene
// the stats screen left up - the original re-blits the held image and the
// roller bar and then puts a panel over them, so the caller supplies the same
// background it always does.
void drawHiScores(tubes::Screen& screen, const tubes::HiScoreBankData& bank,
                  const tubes::Font& heading, bool haveHeading,
                  const tubes::Font& body, bool haveBody, int editRow,
                  const std::string& editName, int cursorPhase) {
    fillHiScorePanel(screen);

    if (haveHeading) {
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kHsTitleY,
                               tubes::kHsTitleColour, tubes::textmode::kPeak,
                               tubes::kHsTitle);
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kHsRuleY,
                               tubes::kHsTitleColour, tubes::textmode::kPeak,
                               tubes::kHsRule);
    }
    if (!haveBody) return;

    for (int i = 1; i <= tubes::kHiScoreShown; ++i) {
        const int y = tubes::hiScoreRowY(i);
        const tubes::HiScoreEntry& e = bank.rows[i - 1];
        // The row being typed shows the live text, not what is in the table.
        const std::string name = (i == editRow) ? editName : e.name;
        tubes::drawText(screen, body, tubes::kHsNameX, y, tubes::kHsRowColour,
                        tubes::textmode::kPeak, name);
        tubes::drawText(screen, body, tubes::kHsScoreX, y, tubes::kHsRowColour,
                        tubes::textmode::kPeak, hiScoreScoreText(e.score));

        // `1000:9757`: a 4 x 4 block just past the last character, its colour
        // walking 0x91..0x9e.
        if (i == editRow && cursorPhase > 0) {
            drawTypingCursor(screen,
                             static_cast<int>(name.size()) * 8 +
                                 tubes::kHsCursorDX,
                             y + tubes::kHsCursorDY, cursorPhase);
        }
    }
}

// The high score viewer, `1b2e:61b6` - the menu item. One bank per page, the
// same panel and rows as the entry screen, and a heading that names the mode.
//
// No professor and no projector slide: the function draws the board, the
// panel, the ten rows, the roller bar and the title, and nothing else. The
// clap is a sound, played and replayed by the wait loop; `1b2e:0656`'s clap
// animation is a different thing and does not belong here.
void drawHiScoreViewer(tubes::Screen& screen, const tubes::HiScoreBankData& bank,
                       const char* title, const tubes::Image* board,
                       bool haveBoard, const tubes::Image* bar, bool haveBar,
                       const tubes::Font& heading, bool haveHeading,
                       const tubes::Font& script, bool haveScript) {
    screen.clear(0);
    // `2321:068d(0, 12, DS:0x2058)` - the classroom's own blackboard, at the
    // classroom's own offset.
    if (haveBoard) screen.blit(*board, 0, tubes::kBoardY);
    // ...and then the panel over it, which is why the chalked equations do not
    // show through.
    fillHiScorePanel(screen);

    if (haveScript) {
        for (int i = 1; i <= tubes::kHiScoreShown; ++i) {
            const int y = tubes::hiScoreRowY(i);
            const tubes::HiScoreEntry& e = bank.rows[i - 1];
            tubes::drawText(screen, script, tubes::kHsNameX, y,
                            tubes::kHsRowColour, tubes::textmode::kPeak,
                            e.name);
            tubes::drawText(screen, script, tubes::kHsScoreX, y,
                            tubes::kHsRowColour, tubes::textmode::kPeak,
                            hiScoreScoreText(e.score));
        }
    }

    // `2321:0711(57, 26, DS:0x2060)`: the roller bar, masked, at the top of
    // the panel - the same bar the classroom scene rides down the slide's
    // edge, parked here at its fully-drawn height.
    if (haveBar) {
        screen.blit(*bar, tubes::kHsViewBarX, tubes::kHsViewBarY);
    }
    if (haveHeading) {
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kHsViewTitleY,
                               tubes::kHsViewTitleColour,
                               tubes::textmode::kPeak, title);
    }
}

//
// `1b2e:52bf`'s slot list and this share their mode split: Endurance counts
// chains and Wave counts waves, right down to the heading's x, so the two
// words end in the same column.
void drawSaveScreen(tubes::Screen& screen, const tubes::SaveBankData& bank,
                    tubes::SaveBank which, int selected, bool typing,
                    const std::string& editText,
                    const tubes::Font& heading, bool haveHeading,
                    const tubes::Font& script, bool haveScript,
                    const tubes::Sprite* marker, bool haveMarker) {
    if (haveHeading) {
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kSaveTitleY,
                               tubes::kSaveTitleColour, tubes::textmode::kPeak,
                               tubes::kSaveTitle);
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kSaveRuleY,
                               tubes::kSaveTitleColour, tubes::textmode::kPeak,
                               tubes::kSaveTitleRule);
        // 1000:30b6. The headings are in the heading font; only the rows are
        // cursive.
        tubes::drawText(screen, heading, tubes::kSaveRowX, tubes::kSaveHeadY,
                        tubes::kSaveRowColour, tubes::textmode::kPeak,
                        tubes::kSaveHeadDesc);
        tubes::drawText(screen, heading, tubes::kSaveRowX,
                        tubes::kSaveHeadRuleY, tubes::kSaveRowColour,
                        tubes::textmode::kPeak, tubes::kSaveHeadDescRule);
        const bool wave = which == tubes::SaveBank::kWave;
        const int hx = wave ? tubes::kSaveWaveX : tubes::kSaveChainsX;
        tubes::drawText(screen, heading, hx, tubes::kSaveHeadY,
                        tubes::kSaveRowColour, tubes::textmode::kPeak,
                        wave ? tubes::kSaveHeadWave : tubes::kSaveHeadChains);
        tubes::drawText(screen, heading, hx, tubes::kSaveHeadRuleY,
                        tubes::kSaveRowColour, tubes::textmode::kPeak,
                        wave ? tubes::kSaveHeadWaveRule
                             : tubes::kSaveHeadChainsRule);
    }

    if (haveScript) {
        for (int i = 1; i <= tubes::kSaveSlotsShown; ++i) {
            const tubes::SaveSlot& rec = bank.slots[i - 1];
            const int y = tubes::saveRowY(i);
            // 1000:316b: a live record shows its description, an empty one the
            // "( Available )" literal. The row being typed shows the live
            // text.
            std::string left;
            const bool editing = typing && i == selected;
            if (editing) left = editText;
            else if (rec.live()) left = rec.description;
            else left = tubes::kSaveAvailable;
            // `1000:34ba` on entry and `1000:35d1` on every keystroke both
            // pass colour 0x0f, where the list rows are drawn in
            // `kSaveRowColour`. So the row being typed turns white, and that
            // is the only thing on the screen that says the editor is open -
            // there is no cursor (see below). The port drew it in the row
            // colour, so selecting a slot looked like it had done nothing,
            // and a player reported exactly that: no way to tell it was
            // waiting for a new name.
            // `1000:34ba` and `1000:35d1` pass colour $0f and mode 0 - flat,
            // no colour walk - where the list rows walk. Keeping the rows'
            // mode with the new colour is what turned the edited line into a
            // scrambled ramp instead of white: `2000:35ec` steps the palette
            // index per scanline, and index 15 is the top of its ramp, so it
            // walked straight out of the greys into whatever follows them.
            tubes::drawText(screen, script, tubes::kSaveRowX, y,
                            editing ? tubes::kSaveTypingColour
                                    : tubes::kSaveRowColour,
                            editing ? tubes::textmode::kFlat
                                    : tubes::textmode::kPeak, left);
            // 1000:31a8 sits after the two arms join, so the number is drawn
            // for every row - an empty slot shows a right-justified 0. The
            // port guarded it on `live()` and the capture said otherwise.
            const int x = which == tubes::SaveBank::kWave
                              ? tubes::kSaveWaveX : tubes::kSaveChainsX;
            tubes::drawText(screen, script, x, y, tubes::kSaveRowColour,
                            tubes::textmode::kPeak,
                            tubes::saveSlotDetail(which, rec));
            // No cursor. The high score screen pulses a 4x4 block at
            // `1000:9757`; this loop has nothing of the kind - it draws the
            // characters and erases an 8-wide cell on backspace, and that is
            // all. It was given one by analogy for one revision, which is
            // exactly the kind of invention this port refuses to add.
        }
    }

    // 1000:3357: `SRBALL` either side of the selected row.
    if (haveMarker) {
        const int y = tubes::saveRowY(selected) + tubes::kSaveMarkerDY;
        screen.draw(*marker, tubes::kSaveMarkerLeftX, y);
        screen.draw(*marker, tubes::kSaveMarkerRightX, y);
    }
}

// One Instructions slide, `1b2e:2d63`. The background is the classroom -
// `1b2e:0a11`, the same scene the briefing uses, projector slide and all - and
// the slide's text goes on the white sheet. That is why every x in the table
// is between 76 and 244: the sheet is 74..246.
//
// The two navigation lines are the exception. They are centred over the whole
// screen at y 184 and 192, which is below the sheet, on the black.
void drawInstructionSlide(tubes::Screen& screen,
                          const tubes::InstructionSlide* pages, int count,
                          int slide, bool hasNav,
                          const tubes::Image* board, bool haveBoard,
                          const SceneArt& art, const ScenePose& pose,
                          const tubes::Font& small, bool haveSmall,
                          const tubes::Font& big, bool haveBig,
                          const tubes::Sprite* atoms, const bool* haveAtom,
                          const tubes::Sprite* tube, const bool* haveTube,
                          const tubes::Sprite* furn, const bool* haveFurn) {
    drawScene(screen, board, haveBoard, art, pose);
    if (pose.slideIsBusy()) return;
    if (slide < 0 || slide >= count) return;
    // The navigation is drawn once by the original and never cleared, so it
    // belongs to the screen rather than to a page.
    const tubes::InstructionSlide& s = pages[slide];
    auto item = [&](const tubes::InstructionItem& it) {
        const tubes::Font& f = it.font ? big : small;
        const bool have = it.font ? haveBig : haveSmall;
        switch (it.kind) {
        case tubes::InstructionItem::kText:
            if (have) {
                tubes::drawText(screen, f, it.x, it.y,
                                static_cast<uint8_t>(it.colour), it.mode,
                                it.text);
            }
            break;
        case tubes::InstructionItem::kCentred:
            if (have) {
                tubes::drawTextCentred(screen, f, it.x, 319, it.y,
                                       static_cast<uint8_t>(it.colour),
                                       it.mode, it.text);
            }
            break;
        case tubes::InstructionItem::kAtom:
            // The two negatives are the slideshow's own sprites; everything
            // else is an atom type out of the game's ball table.
            if (it.colour == tubes::kInstrTestTube1) {
                if (haveTube[tubes::tubephase::kUpright]) {
                    screen.draw(tube[tubes::tubephase::kUpright], it.x, it.y);
                }
            } else if (it.colour == tubes::kInstrTestTubeS) {
                // TESTUBES.CSP - the tube's shadow, which the play field
                // already loads as furniture.
                if (haveFurn[kTestTubeShadow]) {
                    screen.draw(furn[kTestTubeShadow], it.x, it.y);
                }
            } else if (it.colour > 0 && it.colour < kCellStates &&
                       haveAtom[it.colour]) {
                screen.draw(atoms[it.colour], it.x, it.y);
            }
            break;
        }
    };
    // The two navigation lines at y 184 and 192. The Instructions, the Credits
    // and the shareware's Ordering deck all draw them - checked in each, not
    // assumed - but the wave-25 end screen draws neither, because it does not
    // page: `1000:8df8` waits with `1ac3:0b8f`, the terminal wait, and has no
    // nav strings at all. So this is a property of the deck rather than of the
    // screen it is drawn on.
    if (hasNav) {
        for (const tubes::InstructionItem& n : tubes::kInstructionNav) item(n);
    }
    for (int i = 0; i < s.count; ++i) item(s.items[i]);
}

// Rebinding the six controls. The port's own screen - the original's third
// Game Options item loads a driver, which SDL makes meaningless; see input.h.
// It is drawn in the menu's own language so it does not look bolted on: the
// page title where a page title goes, a rule under it, and one row per
// control with the same colour the menu items use.
//
// It borrows from both the classroom scene and the high score viewer and is
// not the same as either, because six labelled rows want more room than
// either was built for:
//
//   from `1b2e:0a11`   the blackboard at (0, 12) and the professor at his own
//                      (267, 121) - the "whole classroom", which the high
//                      score viewer deliberately does not have
//   from `1b2e:61b6`   the chalk panel over the board, the roller bar above
//                      it, the cursive rows and the centred title
//   its own            a narrower panel, so the professor is not painted over,
//                      and two columns instead of the viewer's name-and-score
//
// The projector slide is left out on purpose. `drawScene` always lays it down
// and it is only 172 wide - fine for a briefing's small-font prose, hopeless
// for "Button A" against "Left Ctrl / A".
void drawRebindScreen(tubes::Screen& screen, const std::string* labels,
                      int row, bool waiting, const tubes::Image* board,
                      bool haveBoard, const SceneArt& art,
                      const tubes::Font& big, bool haveBig,
                      const tubes::Font& script, bool haveScript,
                      const tubes::Font& small, bool haveSmall, int profFrame) {
    screen.clear(0);
    if (haveBoard) screen.blit(*board, 0, tubes::kBoardY);

    // The chalk panel, `2321:060b`'s colour, across the board's full width -
    // the equations are wiped everywhere, not just behind the rows. A panel
    // that stopped short left chalk showing beside the professor, which read
    // as a mistake rather than as a board.
    constexpr int kPanelY = 30;
    constexpr int kPanelH = 126;
    constexpr int kPanelRight = tubes::kHsPanelX + tubes::kHsPanelW;
    uint8_t* px = screen.pixelsMutable();
    for (int y = kPanelY; y < kPanelY + kPanelH; ++y) {
        if (y < 0 || y >= tubes::kScreenHeight) continue;
        for (int x = tubes::kHsPanelX; x < kPanelRight; ++x) {
            if (x < 0 || x >= tubes::kScreenWidth) continue;
            px[static_cast<size_t>(y) * tubes::kScreenWidth + x] =
                tubes::kHsPanelColour;
        }
    }

    // The professor goes on after the panel, so he stands in front of a clean
    // board rather than being wiped off it. Same two draws the scene makes:
    // the standing pose masked, then the wave frame stamped over his top half.
    if (art.havePointer && art.havePointer[0]) {
        screen.blit(art.pointer[0], tubes::kProfX, tubes::kProfY);
    }
    if (art.havePointer && profFrame > 0 && profFrame < 4 &&
        art.havePointer[profFrame]) {
        screen.blit(art.pointer[profFrame], tubes::kProfX, tubes::kProfY);
    }
    // The roller bar rides the top of the board rather than the top of the
    // panel. The viewer puts its title across the bar and gets away with it in
    // blue; in red on grey it is a struggle to read, and there is no room to
    // clear a 16-tall font between the board's top edge at 12 and the panel.
    // So the bar goes up and the title comes inside the panel.
    if (art.haveBar) {
        screen.blit(*art.bar, tubes::kHsViewBarX, tubes::kBoardY + 1);
    }

    if (haveBig) {
        // Centred over the panel rather than the screen, or the professor
        // pushes the title off-centre.
        tubes::drawTextCentred(screen, big, tubes::kHsPanelX, kPanelRight, 32,
                               tubes::kHsTitleColour, tubes::textmode::kPeak,
                               "Redefine Controls");
    }

    constexpr int kRow0 = 52;
    constexpr int kPitch = 15;
    constexpr int kNameX = 22;
    constexpr int kBindX = 112;
    for (int i = 0; i < tubes::kGameButtons; ++i) {
        const int y = kRow0 + i * kPitch;
    // The row being bound says "press a key" instead of showing its binding,
    // which is also how the player knows the next press is being taken.
        const bool armed = waiting && i == row;
        // `0x2f` is the game's red - it is what draws "Save Game", the abort
        // banner and the high score entry screen's own title, and measured off
        // a capture it is (215, 0, 0). Far more visible on the board's green
        // than `0x9f`, the blue the viewer's heading uses.
        const uint8_t colour = (i == row) ? tubes::kHsTitleColour
                                          : tubes::kHsRowColour;
        // Names in the viewer's cursive, because they are words on a
        // blackboard; bindings in the heading font, because "Left Ctrl" is a
        // label off a keyboard and has to be read exactly.
        if (haveScript) {
            tubes::drawText(screen, script, kNameX, y, colour,
                            tubes::textmode::kPeak, tubes::kGameButtonNames[i]);
        }
        if (haveBig) {
            tubes::drawText(screen, big, kBindX, y, colour,
                            tubes::textmode::kPeak,
                            armed ? "press a key" : labels[i]);
        }
    }
    // The hint goes where the Instructions and Credits put theirs: on the
    // black floor below the board, in `0x76` cyan, in `TINY6X8`, centred over
    // the whole screen. That is the game's own convention for "how to work
    // this screen", and off the green it needs no help to be read.
    if (haveSmall) {
        tubes::drawTextCentred(screen, small, 0, 319, tubes::kInstrNavY,
                               tubes::kInstrNavColour, tubes::textmode::kFadeUp,
                               "Enter binds - Esc exits");
    }
}

// The display options. This is also the port's own screen, and for the same
// reason the rebinding screen is - see `input.h`. It is deliberately the
// rebinding screen's twin: the same blackboard, the same chalk panel, the same
// professor standing in front of it, the same two fonts doing the same two
// jobs, and the same hint on the black floor. Two screens the original never
// had should at least look like each other, and like the game.
//
// The only structural difference is that a row here has a value that changes
// in place rather than a binding captured from a keypress, so Left and Right
// work the row and Enter is a synonym for Right. That is also why there is no
// armed state: nothing here waits on a second press.
void drawGraphicsScreen(tubes::Screen& screen, const tubes::GraphicsOptions& g,
                        int row, const tubes::Image* board, bool haveBoard,
                        const SceneArt& art, const tubes::Font& big,
                        bool haveBig, const tubes::Font& script,
                        bool haveScript, const tubes::Font& small,
                        bool haveSmall, int profFrame) {
    screen.clear(0);
    if (haveBoard) screen.blit(*board, 0, tubes::kBoardY);

    constexpr int kPanelY = 30;
    constexpr int kPanelH = 126;
    constexpr int kPanelRight = tubes::kHsPanelX + tubes::kHsPanelW;
    uint8_t* px = screen.pixelsMutable();
    for (int y = kPanelY; y < kPanelY + kPanelH; ++y) {
        if (y < 0 || y >= tubes::kScreenHeight) continue;
        for (int x = tubes::kHsPanelX; x < kPanelRight; ++x) {
            if (x < 0 || x >= tubes::kScreenWidth) continue;
            px[static_cast<size_t>(y) * tubes::kScreenWidth + x] =
                tubes::kHsPanelColour;
        }
    }

    if (art.havePointer && art.havePointer[0]) {
        screen.blit(art.pointer[0], tubes::kProfX, tubes::kProfY);
    }
    if (art.havePointer && profFrame > 0 && profFrame < 4 &&
        art.havePointer[profFrame]) {
        screen.blit(art.pointer[profFrame], tubes::kProfX, tubes::kProfY);
    }
    if (art.haveBar) {
        screen.blit(*art.bar, tubes::kHsViewBarX, tubes::kBoardY + 1);
    }

    if (haveBig) {
        tubes::drawTextCentred(screen, big, tubes::kHsPanelX, kPanelRight, 32,
                               tubes::kHsTitleColour, tubes::textmode::kPeak,
                               "Graphics Options");
    }

    // Five rows against the rebinding screen's six, so they start lower and
    // sit on the same pitch - the two screens should not appear to jump when
    // the player moves between them.
    constexpr int kRow0 = 60;
    constexpr int kPitch = 15;
    constexpr int kNameX = 22;
    constexpr int kValueX = 150;
    for (int i = 0; i < tubes::kGraphicsRows; ++i) {
        const int y = kRow0 + i * kPitch;
        const uint8_t colour = (i == row) ? tubes::kHsTitleColour
                                          : tubes::kHsRowColour;
        if (haveScript) {
            tubes::drawText(screen, script, kNameX, y, colour,
                            tubes::textmode::kPeak, tubes::kGraphicsRowNames[i]);
        }
        if (haveBig) {
            tubes::drawText(
                screen, big, kValueX, y, colour, tubes::textmode::kPeak,
                tubes::graphicsValueLabel(g, static_cast<tubes::GraphicsRow>(i))
                    .c_str());
        }
    }
    if (haveSmall) {
        tubes::drawTextCentred(screen, small, 0, 319, tubes::kInstrNavY,
                               tubes::kInstrNavColour, tubes::textmode::kFadeUp,
                               "Left and Right change - Esc exits");
    }
}

void drawPrompt(tubes::TextScreen& ts, const char* prompt) {
    int col = 0;
    for (const char* p = prompt; *p; ++p, ++col) {
        ts.put(col, kPromptRow, static_cast<uint8_t>(*p), kPromptAttr);
    }
}

// `2321:0ac0`, the bevelled panel the story text sits in.
void drawPanel(tubes::Screen& screen, int x, int y, int w, int h) {
    uint8_t* px = screen.pixelsMutable();
    for (const tubes::PanelFill& f : tubes::kPanelFills) {
        const int rx = x + (f.fromRight ? w : 0) + f.dx;
        const int ry = y + (f.fromBottom ? h : 0) + f.dy;
        const int rw = f.useW ? w + f.dw : 1;
        const int rh = f.useH ? h + f.dh : 1;
        for (int yy = ry; yy < ry + rh; ++yy) {
            if (yy < 0 || yy >= tubes::kScreenHeight) continue;
            for (int xx = rx; xx < rx + rw; ++xx) {
                if (xx < 0 || xx >= tubes::kScreenWidth) continue;
                px[static_cast<size_t>(yy) * tubes::kScreenWidth + xx] =
                    f.colour;
            }
        }
    }
}

void drawPaused(tubes::Screen& screen, const tubes::Font& heading,
                bool haveHeading) {
    if (!haveHeading) return;
    tubes::drawTextCentred(screen, heading, 0, 319, tubes::kBannerY,
                           tubes::kBannerColour, tubes::textmode::kPeak,
                           "Game Paused");
    tubes::drawTextCentred(screen, heading, 0, 319, tubes::kBannerRuleY,
                           tubes::kBannerColour, tubes::textmode::kPeak,
                           "_________");
}

// The F1 Help overlay, `1000:2e1c`. The original copies the play field to page
// 2 and writes over that; the port composes whole frames, so the caller has
// already drawn the field and this goes on top of it - the same arrangement
// `drawSaveScreen` uses.
//
// `mode` is `DS:0x1d4e`: 1 Endurance, 2 Wave. Only the left-hand note differs.
void drawHelpScreen(tubes::Screen& screen, int mode,
                    const tubes::Font& heading, bool haveHeading,
                    const tubes::Font& small, bool haveSmall) {
    if (haveHeading) {
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kHelpTitleY,
                               tubes::kHelpTitleColour, tubes::textmode::kPeak,
                               tubes::kHelpTitle);
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kHelpRuleY,
                               tubes::kHelpTitleColour, tubes::textmode::kPeak,
                               tubes::kHelpTitleRule);
        tubes::drawTextCentred(screen, heading, 0, 319, tubes::kHelpPromptY,
                               tubes::kHelpTitleColour, tubes::textmode::kPeak,
                               tubes::kHelpPrompt);
    }
    if (!haveSmall) return;

    tubes::drawText(screen, small, tubes::kHelpKeyX, tubes::kHelpHeadY0,
                    tubes::kHelpKeyColour, tubes::kHelpBodyMode,
                    tubes::kHelpHead0);
    tubes::drawText(screen, small, tubes::kHelpKeyX, tubes::kHelpHeadY1,
                    tubes::kHelpKeyColour, tubes::kHelpBodyMode,
                    tubes::kHelpHead1);
    for (int i = 0; i < tubes::kHelpKeyCount; ++i) {
        tubes::drawText(screen, small, tubes::kHelpKeyX,
                        tubes::kHelpKeyY0 + i * tubes::kHelpKeyPitch,
                        tubes::kHelpKeyColour, tubes::kHelpBodyMode,
                        tubes::kHelpKeys[i]);
    }

    // The drops note is unconditional; the mode note is the one arm that
    // branches, and it moves as well as changing its words.
    const bool endurance = mode == 1;
    const int noteX = endurance ? tubes::kHelpEnduranceX : tubes::kHelpWaveX;
    const int noteY0 = endurance ? tubes::kHelpEnduranceY0 : tubes::kHelpWaveY0;
    const char* const* note =
        endurance ? tubes::kHelpEndurance : tubes::kHelpWave;
    for (int i = 0; i < tubes::kHelpNoteLines; ++i) {
        tubes::drawText(screen, small, tubes::kHelpDropsX,
                        tubes::kHelpDropsY0 + i * tubes::kHelpNotePitch,
                        tubes::kHelpNoteColour, tubes::kHelpBodyMode,
                        tubes::kHelpDrops[i]);
        tubes::drawText(screen, small, noteX,
                        noteY0 + i * tubes::kHelpNotePitch,
                        tubes::kHelpNoteColour, tubes::kHelpBodyMode, note[i]);
    }
}

// The title screen, `1b2e:52bf`, and its menu, `1b2e:4d80`.
//
// `TUBESBG.GFX` and `TUBESFG.GFX` are a background/foreground pair - the word
// TUBES drawn as a connected tube network - and the atom travels INSIDE the
// pipes. That works here for the same reason it works on the play field: the
// atom is drawn, then a 16 x 13 box of the foreground is stamped back over it,
// so the pipe walls come back on top and the ball shows through only where the
// foreground is transparent. Same routine, `2321:0874`, same 16 x 13 box.
void drawTitle(tubes::Screen& screen, const tubes::Image& bg,
               const tubes::Screen& fgScene, bool haveArt,
               const tubes::Menu& menu, const tubes::TitleAtom& atom,
               const tubes::Sprite* atoms, const bool* haveAtom, int atomBall,
               const tubes::Image* stars, const bool* haveStar,
               const tubes::Font& big, bool haveBig,
               const tubes::Image& fg, const tubes::Font& small,
               bool haveSmall) {
    screen.clear(0);
    if (haveArt) {
        screen.blit(bg);
        // `1b2e:5754`: a full-screen masked blit of the foreground,
        // `2321:0711(0, 0, fg, 320, 200)`. Without it the pipe walls are
        // simply absent - the network reads as a flat silhouette, which is
        // what happened when only the atom's own box was stamped.
        screen.blit(fg);
    }

    // `1b2e:5780`, drawn once under the artwork with the small font, which
    // `1b2e:576b` selects just before it. Mode 3, no shadow bit.
    if (haveSmall) {
        tubes::drawTextCentred(screen, small, 0, 319, 190, 157, 3,
                               "Copyright 1994 Absolute Magic");
        tubes::drawText(screen, small, 294, 190, 157, 3, "v1.0");
    }

    // The atom, then the foreground back over its box.
    if (atomBall >= 1 && atomBall < tubes::kTypeCount && haveAtom[atomBall]) {
        screen.draw(atoms[atomBall], atom.x, atom.y);
    }
    if (haveArt) screen.stamp(fgScene, atom.x, atom.y, 16, 13);

    if (!menu.up() || !haveBig) return;

    const tubes::Page p = menu.page();
    // `menuPage`, not `kMenuPages`: page 6 carries the port's extra row and
    // every layout call below already goes through the same accessor.
    const tubes::Edition ed = menu.edition();
    const tubes::MenuPage& page = tubes::menuPage(p, ed);

    // `1b2e:4743`. The title is drawn only when the page has one - page 1's is
    // empty - with its rule two rows below.
    if (page.title[0]) {
        tubes::drawTextCentred(screen, big, 0, 319, tubes::menuTitleY(p, ed),
                               tubes::kMenuTitleColour, tubes::kMenuTitleMode,
                               page.title);
        tubes::drawTextCentred(screen, big, 0, 319, tubes::menuRuleY(p, ed),
                               tubes::kMenuTitleColour, tubes::kMenuTitleMode,
                               tubes::menuRule(p, ed));
    }

    // Every item in one colour: the selection is marked by the stars alone,
    // which is why there is no highlight colour here.
    for (int i = 1; i <= page.count; ++i) {
        tubes::drawTextCentred(screen, big, 0, 319, tubes::menuItemY(p, i, ed),
                               tubes::kMenuItemColour, tubes::kMenuItemMode,
                               menu.itemText(i));
    }

    const tubes::StarPlacement s =
        tubes::placeStars(p, menu.item(), menu.itemText(menu.item()), ed);
    const int f = menu.starFrame();
    if (f >= 1 && f <= tubes::kStarFrames && haveStar[f]) {
        screen.blit(stars[f], s.xLeft, s.y);
        screen.blit(stars[f], s.xRight, s.y);
    }
}

void drawTaskDisplay(tubes::Screen& screen, const tubes::Game& game,
                     const tubes::Font& big, bool haveBig,
                     const tubes::Sprite* atoms, const bool* haveAtom,
                     const tubes::Sprite* furn, const bool* haveFurn,
                     const tubes::Sprite* smallBall, const bool* haveSmallBall) {
    const tubes::WaveObjective& obj = game.objective();
    if (!tubes::isWaveMode(obj.mode)) return;
    if (obj.counter == 0 || obj.mysteryHidden) return;

    // Flashium has no sprite of its own - the original rewrites its table slot
    // every fourth frame - and the Task Display's colour is pointed at that
    // same cycling value whenever the wave names no colour, so this ball
    // cycles for exactly the waves the original does.
    const int8_t taskColour = game.taskDisplay().colour;
    const int8_t ball = taskColour == tubes::kFlashium ? game.flashColour()
                                                       : taskColour;
    switch (obj.mode) {
        case tubes::WaveMode::kSurvive:
            if (haveAtom[ball]) screen.draw(atoms[ball], 6, 10);
            break;
        case tubes::WaveMode::kCrystals:
            if (haveAtom[tubes::kCrystal]) {
                screen.draw(atoms[tubes::kCrystal], 6, 10);
            }
            break;
        case tubes::WaveMode::kMarked:
            if (haveAtom[ball]) screen.draw(atoms[ball], 6, 10);
            if (haveFurn[kMarker]) screen.draw(furn[kMarker], 8, 11);
            break;
        default:
            // Modes 2 and 3: `1000:2894` lays three half-size balls out in the
            // shape of the required chain. Pitch 7 across and 5 down, which is
            // what an 8x7 sprite wants.
            if (ball >= 1 && ball <= 7 && haveSmallBall[ball]) {
                const tubes::Sprite& s = smallBall[ball];
                const uint8_t chain = game.taskDisplay().chain;
                if (chain == tubes::chaincode::kVertical) {
                    screen.draw(s, 10, 9);
                    screen.draw(s, 10, 14);
                    screen.draw(s, 10, 19);
                } else if (chain == tubes::chaincode::kHorizontal) {
                    screen.draw(s, 3, 14);
                    screen.draw(s, 10, 14);
                    screen.draw(s, 17, 14);
                } else {
                    // Both diagonals count, so the picture alternates between
                    // them every flash tick rather than committing to one.
                    if (!game.taskDisplay().diagonalFlip) {
                        screen.draw(s, 3, 9);
                        screen.draw(s, 17, 19);
                    } else {
                        screen.draw(s, 17, 9);
                        screen.draw(s, 3, 19);
                    }
                    screen.draw(s, 10, 14);
                }
            }
            break;
    }

    if (!haveBig) return;
    const std::string n = std::to_string(obj.counter);
    const int x = obj.counter < 10 ? 10 : (obj.counter <= 99 ? 6 : 2);
    tubes::drawText(screen, big, x, 10, 127, tubes::textmode::kFadeDown, n);
}

}  // namespace tubes
