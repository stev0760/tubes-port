#pragma once

// The port's version, and what the three numbers are a promise about.
//
// This is a reimplementation of a 1994 binary, so the usual semver question -
// "what is the public API?" - has an unusual answer. The interface is
// **whatever outside this repository depends on the port**, and that splits
// the same way `docs/reversing-notes.md` already splits proven from invented.
//
// MAJOR - a consumer outside this repository stops working. A CLI flag
//   removed, renamed, or given a new meaning; a machine-read output
//   (`--dump-regs`, `--dump-save`, `--demo-csv`) reshaped where the consumer
//   is not in `tools/` - the rig at `~/Dev/tubes-tooling/` is outside this
//   repo on purpose and cannot be updated in the same commit; a `settings.cfg`
//   key repurposed so an existing file silently means something new; or a
//   piece of the port's OWN invented behaviour changed or withdrawn - control
//   rebinding, Graphics Options, the edition picker, `--fade-steps`.
//
//   And one thing that is not a compatibility break at all but is the only
//   event large enough to deserve the digit: **1.0.0 is the claim that the
//   port is done against the original** - every screen ported, every rule read
//   rather than fitted, and a full Tubes 101 playthrough completed clean.
//   Nothing else moves the major digit to 1.
//
// MINOR - something gained, with nothing having to change to keep working: a
//   new flag, a new port-side option or screen, another decoded format, or a
//   rule newly transliterated where the port carried a marked placeholder.
//
// PATCH - a fidelity correction, in either direction: the port did something
//   the original does not, or failed to do something it does, and now matches.
//   **This is the common case and it stays a patch even when the game plays
//   differently afterwards.** The specification is the 1994 binary, not the
//   port's last release, so a corrected rule means the port stopped playing
//   WRONGLY - the objective counter decremented by nothing, the clear timer
//   run one statement early, Flashium's wildcard wrong in two ways. Ranking
//   those as MINOR or MAJOR would measure them against the port's own past
//   output, which is exactly the authority `CLAUDE.md`'s prime directive
//   refuses to grant it everywhere else. Also crashes, leaks, build fixes,
//   tests and comments.
//
// NOT VERSIONED BY THIS NUMBER AT ALL - `TUBES.SAV` and `TUBES.HSC`. They are
//   the ORIGINAL's formats, byte-exact, and a player may alternate between the
//   port and the 1994 binary against one install. The port does not own them
//   and has no authority to change them: a release that writes a file the
//   original refuses is not a new major version, it is broken, and the next
//   patch un-breaks it.
//
// Where it is shown: `--version`, the `--help` banner, and the edition picker,
// which is the port's own screen. No screen the original draws carries it, and
// the window still says `Tubes`.

// One source of truth for the whole project, macros rather than only
// constants for two reasons. The string below is assembled from these by the
// preprocessor, so the number and the text it prints cannot be edited out of
// agreement - the failure a hand-written string invites. And `#define` lines
// are an unambiguous anchor for `CMakeLists.txt`, which greps this file rather
// than carrying a second copy that can drift.
#define TUBES_VERSION_MAJOR 0
#define TUBES_VERSION_MINOR 9
#define TUBES_VERSION_PATCH 0

#define TUBES_VERSION_STRINGIFY_(x) #x
#define TUBES_VERSION_STRINGIFY(x) TUBES_VERSION_STRINGIFY_(x)
#define TUBES_VERSION_STRING                         \
    TUBES_VERSION_STRINGIFY(TUBES_VERSION_MAJOR) "." \
    TUBES_VERSION_STRINGIFY(TUBES_VERSION_MINOR) "." \
    TUBES_VERSION_STRINGIFY(TUBES_VERSION_PATCH)

namespace tubes {

constexpr int kVersionMajor = TUBES_VERSION_MAJOR;
constexpr int kVersionMinor = TUBES_VERSION_MINOR;
constexpr int kVersionPatch = TUBES_VERSION_PATCH;

// `0.9.0`.
constexpr const char* kVersion = TUBES_VERSION_STRING;

// `tubes-port 0.9.0` - the one printable identity, and one constant rather
// than two formatted strings for a reason that is load-bearing: `main.cpp`
// prints it for `--version` and `textscreen.cpp` writes it on the picker, and
// `main.cpp` is not in `tubes-tests` and cannot be, since it includes SDL. So
// a test of this constant is the only coverage the CLI's version line can
// have, and both surfaces are then correct by construction.
constexpr const char* kVersionLine = "tubes-port " TUBES_VERSION_STRING;

}  // namespace tubes

// Deliberately not left defined: these are generic names and nothing outside
// this header needs them. The three numeric macros stay, because CMake reads
// them.
#undef TUBES_VERSION_STRINGIFY_
#undef TUBES_VERSION_STRINGIFY
#undef TUBES_VERSION_STRING
