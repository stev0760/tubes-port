#include "wave_text.h"

// Transcribed from `1000:62f1` .. `1000:8581`. Every coordinate is a literal
// push; every string is at the offset the routine loads into DI. See
// docs/reversing-notes.md, "The briefing screen's presentation half".

namespace tubes {
namespace {

using F = BriefFmt;
using K = BriefBallKind;

constexpr uint8_t B = kBriefBodyColour;      // 155, the objective
constexpr uint8_t M = kBriefModifierColour;  // 169, the modifier

#define LIT(x, y, c, s) {x, y, c, 1, F::kLiteral, s, nullptr, nullptr}

// --- mode 6, the marked atoms ---------------------------------------------
const BriefLine kMarked[] = {
    LIT(76, 85, B, "Form chains to remove marked"),
    LIT(76, 95, B, "atoms from the beaker."),
    {-1, 125, B, 1, F::kCount, "Marked Atoms: ", "", nullptr},
};
const BriefBall kMarkedBalls[] = {
    {150, 110, K::kRandom}, {152, 111, K::kMarker},
};

const BriefLine kMarkedCovered[] = {
    LIT(76, 75, B, "Form chains to remove marked"),
    LIT(76, 85, B, "atoms from  the  beaker  but"),
    LIT(76, 95, B, "first you'll have to  remove"),
    LIT(76, 105, B, "the atoms covering them."),
    {-1, 120, B, 1, F::kCount, "Marked Atoms: ", "", nullptr},
};

const BriefLine kMarkedXenon[] = {
    LIT(76, 70, B, "Form chains to remove marked"),
    LIT(76, 80, B, "atoms from  the  beaker  but"),
    LIT(76, 90, B, "first  you'll  have  to  use"),
    LIT(76, 100, B, "Anti-Matter  to  remove  the"),
    LIT(76, 110, B, "Xenons surrounding them."),
    {-1, 125, B, 1, F::kCount, "Marked Atoms: ", "", nullptr},
};

// --- mode 5, the Mischief Crystals ----------------------------------------
const BriefLine kCrystals[] = {
    LIT(76, 75, B, "Using Anti-Matter remove all"),
    LIT(76, 85, B, "the Mischief  Crystals  that"),
    LIT(76, 95, B, "are contaminating the beaker."),
    {-1, 125, B, 1, F::kCount, "Mischief Crystals: ", "", nullptr},
};
const BriefBall kCrystalBalls[] = {{150, 110, K::kCrystal}};

// --- mode 3, a colour is required -----------------------------------------
const BriefLine kFlashiumWave[] = {
    {76, 85, B, 1, F::kCount, "Form   ", "    chains    using", nullptr},
    LIT(76, 95, B, "Flashium to advance  to  the"),
    LIT(76, 105, B, "next wave."),
};

const BriefLine kShownAtom[] = {
    {76, 75, B, 1, F::kCount, "Form ", " chains using the", nullptr},
    LIT(76, 85, B, "following atom to advance..."),
    // The atom's name under it, in its own colour and mode - `1000:68b7`.
    {-1, 125, 165, 2, F::kNameOnly, nullptr, nullptr, nullptr},
};
const BriefBall kShownAtomBalls[] = {{152, 110, K::kRequired}};

const BriefLine kHorizColour[] = {
    {76, 75, B, 1, F::kCount, "Form ", " horizontal chains", nullptr},
    {76, 85, B, 1, F::kName, "using ", ".", nullptr},
};
const BriefBall kHorizColourBalls[] = {
    {130, 110, K::kRequired}, {150, 110, K::kRequired}, {170, 110, K::kRequired},
};

const BriefLine kVertColour[] = {
    {76, 75, B, 1, F::kCount, "Form ", " vertical chains", nullptr},
    {76, 85, B, 1, F::kName, "using ", ".", nullptr},
};
const BriefBall kVertColourBalls[] = {
    {150, 95, K::kRequired}, {150, 110, K::kRequired}, {150, 125, K::kRequired},
};

const BriefLine kDiagColour[] = {
    {76, 75, B, 1, F::kCount, "Form ", " diagonal chains", nullptr},
    {76, 85, B, 1, F::kName, "using ", ".", nullptr},
};
// Both diagonals, side by side - the briefing saying "either direction" in
// pictures, the same thing the scoring says by giving them one counter.
const BriefBall kDiagColourBalls[] = {
    {100, 95, K::kRequired}, {115, 110, K::kRequired}, {130, 125, K::kRequired},
    {205, 95, K::kRequired}, {190, 110, K::kRequired}, {175, 125, K::kRequired},
};

// --- mode 4, live through N atoms -----------------------------------------
const BriefLine kSurvive[] = {
    LIT(76, 85, B, "Form as many chains  as  you"),
    LIT(76, 95, B, "possibly can to live through"),
    {76, 105, B, 1, F::kCount, "", " atoms.", nullptr},
};

const BriefLine kSurviveHidden[] = {
    LIT(76, 80, B, "Form as many chains  as  you"),
    LIT(76, 90, B, "possibly can to live through"),
    {76, 100, B, 1, F::kCount, "", " atoms  that  are   hidden", nullptr},
    LIT(76, 110, B, "until they leave a tube."),
};

const BriefLine kSurviveDisabled[] = {
    LIT(76, 80, B, "Form as many chains  as  you"),
    LIT(76, 90, B, "possibly can to live through"),
    {76, 100, B, 1, F::kCount, "", " atoms.", nullptr},
    {75, 115, M, 1, F::kNameWide, "", " is  disabled  for", nullptr},
    LIT(75, 125, M, "the duration  of  this  wave"),
    LIT(75, 135, M, "and will not disappear."),
};

// --- mode 3, the Task Display drives it -----------------------------------
const BriefLine kTaskColour[] = {
    {75, 80, B, 1, F::kCount, "Form   ", "    chains    using", nullptr},
    LIT(75, 90, B, "the colour specified in  the"),
    LIT(75, 100, B, "Task Display during the game."),
    LIT(75, 115, M, "The  colour  required   will"),
    LIT(75, 125, M, "change after completing each"),
    LIT(75, 135, M, "task."),
};

const BriefLine kTaskChain[] = {
    {75, 70, B, 1, F::kCountName, "Form  ", "   ", "  chains"},
    LIT(75, 80, B, "using the chain specified in"),
    LIT(75, 90, B, "the Task Display during  the"),
    LIT(75, 100, B, "game."),
    LIT(75, 115, M, "The  chain   required   will"),
    LIT(75, 125, M, "change after completing each"),
    LIT(75, 135, M, "task."),
};

const BriefLine kTaskBoth[] = {
    {75, 70, B, 1, F::kCount, "Form   ", "    chains    using", nullptr},
    LIT(75, 80, B, "the colour & chain specified"),
    LIT(75, 90, B, "in the Task  Display  during"),
    LIT(75, 100, B, "the game."),
    LIT(75, 115, M, "The colour & chain  required"),
    LIT(75, 125, M, "will change after completing"),
    LIT(75, 135, M, "each task."),
};

const BriefLine kTaskColourTimed[] = {
    {75, 80, B, 1, F::kCount, "Form   ", "    chains    using", nullptr},
    LIT(75, 90, B, "the colour specified in  the"),
    LIT(75, 100, B, "Task Display during the game."),
    LIT(75, 115, M, "The  colour  required   will"),
    LIT(75, 125, M, "change every 45 seconds."),
};

const BriefLine kTaskChainTimed[] = {
    {75, 70, B, 1, F::kCountName, "Form  ", "   ", "  chains"},
    LIT(75, 80, B, "using the chain specified in"),
    LIT(75, 90, B, "the Task Display during  the"),
    LIT(75, 100, B, "game."),
    LIT(75, 115, M, "The  chain   required   will"),
    LIT(75, 125, M, "change every 45 seconds."),
};

const BriefLine kTaskBothTimed[] = {
    {75, 70, B, 1, F::kCount, "Form   ", "    chains    using", nullptr},
    LIT(75, 80, B, "the colour & chain specified"),
    LIT(75, 90, B, "in the Task  Display  during"),
    LIT(75, 100, B, "the game."),
    LIT(75, 115, M, "The colour & chain  required"),
    LIT(75, 125, M, "will change every 45 seconds."),
};

// --- mode 3, any atom ------------------------------------------------------
const BriefLine kAny[] = {
    {76, 85, B, 1, F::kCount, "Form  ", "  chains  using  any", nullptr},
    LIT(76, 95, B, "atom to advance to the  next"),
    LIT(76, 105, B, "wave."),
};

const BriefLine kAnyPrefill[] = {
    {76, 80, B, 1, F::kCount, "Form  ", "  chains  using  any", nullptr},
    LIT(76, 90, B, "atom to advance to the  next"),
    LIT(76, 100, B, "wave."),
    LIT(75, 115, M, "The  beaker   will   already"),
    LIT(75, 125, M, "contain atoms."),
};

const BriefLine kAnyMorph[] = {
    {76, 80, B, 1, F::kCount, "Form  ", "  chains  using  any", nullptr},
    LIT(76, 90, B, "atom to advance to the  next"),
    LIT(76, 100, B, "wave."),
    LIT(75, 115, M, "The atoms in the beaker will"),
    LIT(75, 125, M, "morph  into   another   atom"),
    LIT(75, 135, M, "every 45 seconds."),
};

// --- mode 2, an orientation is required -----------------------------------
const BriefLine kHorizAny[] = {
    {76, 70, B, 1, F::kCount, "Form ", "...", nullptr},
    LIT(76, 125, B, "Horizontal Chains using any"),
    LIT(76, 135, B, "atoms."),
};
const BriefBall kHorizAnyBalls[] = {
    {130, 95, K::kRequired}, {150, 95, K::kRequired}, {170, 95, K::kRequired},
};

const BriefLine kVertAny[] = {
    {76, 70, B, 1, F::kCount, "Form ", "...", nullptr},
    LIT(76, 125, B, "Vertical Chains  using  any"),
    LIT(76, 135, B, "atoms."),
};
const BriefBall kVertAnyBalls[] = {
    {150, 80, K::kRequired}, {150, 95, K::kRequired}, {150, 110, K::kRequired},
};

const BriefLine kDiagAny[] = {
    {76, 70, B, 1, F::kCount, "Form ", "...", nullptr},
    LIT(76, 125, B, "Diagonal Chains  using  any"),
    LIT(76, 135, B, "atoms."),
};
const BriefBall kDiagAnyBalls[] = {
    {100, 80, K::kRequired}, {115, 95, K::kRequired}, {130, 110, K::kRequired},
    {205, 80, K::kRequired}, {190, 95, K::kRequired}, {175, 110, K::kRequired},
};

const BriefLine kMysteryWave[] = {
    LIT(76, 70, B, "In Mystery Wave you are  not"),
    LIT(76, 80, B, "told what the task is  until"),
    LIT(76, 90, B, "you complete it  once.   The"),
    LIT(76, 100, B, "Task  Display  in  the  game"),
    LIT(76, 110, B, "will remain  off  until  you"),
    LIT(76, 120, B, "discover  what   wave   this"),
    LIT(76, 130, B, "really is."),
};

#undef LIT

template <int N>
constexpr int len(const BriefLine (&)[N]) { return N; }
template <int N>
constexpr int len(const BriefBall (&)[N]) { return N; }

}  // namespace

const char* const kBriefTitle = "Wave ";
const char* const kBriefRule = "____________";
const char* const kBriefDropsA = "You are allowed ";
const char* const kBriefDropsB = " drops.";

const char* const kElementNames[8] = {
    "", "Redium", "Greenium", "Bluium", "Cyanium",
    "Purplium", "Yellowium", "Pinkium",
};

const Briefing& briefingFor(Objective o) {
    static const Briefing table[] = {
        {kMarked, len(kMarked), kMarkedBalls, len(kMarkedBalls)},
        {kMarkedCovered, len(kMarkedCovered), nullptr, 0},
        {kMarkedXenon, len(kMarkedXenon), nullptr, 0},
        {kCrystals, len(kCrystals), kCrystalBalls, len(kCrystalBalls)},
        {kFlashiumWave, len(kFlashiumWave), nullptr, 0},
        {kShownAtom, len(kShownAtom), kShownAtomBalls, len(kShownAtomBalls)},
        {kHorizColour, len(kHorizColour), kHorizColourBalls, len(kHorizColourBalls)},
        {kVertColour, len(kVertColour), kVertColourBalls, len(kVertColourBalls)},
        {kDiagColour, len(kDiagColour), kDiagColourBalls, len(kDiagColourBalls)},
        {kSurvive, len(kSurvive), nullptr, 0},
        {kSurviveHidden, len(kSurviveHidden), nullptr, 0},
        {kSurviveDisabled, len(kSurviveDisabled), nullptr, 0},
        {kTaskColour, len(kTaskColour), nullptr, 0},
        {kTaskChain, len(kTaskChain), nullptr, 0},
        {kTaskBoth, len(kTaskBoth), nullptr, 0},
        {kTaskColourTimed, len(kTaskColourTimed), nullptr, 0},
        {kTaskChainTimed, len(kTaskChainTimed), nullptr, 0},
        {kTaskBothTimed, len(kTaskBothTimed), nullptr, 0},
        {kAny, len(kAny), nullptr, 0},
        {kAnyPrefill, len(kAnyPrefill), nullptr, 0},
        {kAnyMorph, len(kAnyMorph), nullptr, 0},
        {kHorizAny, len(kHorizAny), kHorizAnyBalls, len(kHorizAnyBalls)},
        {kVertAny, len(kVertAny), kVertAnyBalls, len(kVertAnyBalls)},
        {kDiagAny, len(kDiagAny), kDiagAnyBalls, len(kDiagAnyBalls)},
        {kMysteryWave, len(kMysteryWave), nullptr, 0},
    };
    const int i = static_cast<int>(o);
    static const Briefing empty{nullptr, 0, nullptr, 0};
    if (i < 0 || i >= static_cast<int>(sizeof(table) / sizeof(table[0]))) {
        return empty;
    }
    return table[i];
}

}  // namespace tubes
