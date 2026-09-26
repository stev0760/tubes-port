// Finding the player's copy of Tubes.
//
// The port reads the original's data at runtime and ships none of it, so the
// first thing it has to do is find `TUBES.RES`. It used to look in exactly one
// place, the current directory or `--gamedir`, and say so on stderr. That was
// fine from a terminal and useless from Windows Explorer: double-click the
// .exe anywhere but the game's folder and a console flashed an error and
// closed before it could be read.
//
// So when the player has not said where the game is, it is looked for, in an
// order that puts the obvious places first:
//
//   1. the current directory, and a `TUBES` folder inside it;
//   2. the executable's own directory, and a `TUBES` folder inside it -
//      the same thing on a double-click, but not from a shortcut or a
//      terminal elsewhere;
//   3. the directory that worked last time, from the settings file.
//
// A remembered directory comes last on purpose: a player who drops the .exe
// into a second install - the shareware beside the registered, say - means
// that one, and the current directory has to win.
//
// When the player HAS said - `--gamedir DIR`, or a folder dropped onto the
// executable, which Windows passes as the first argument - only that is
// looked in, plus its `TUBES` subfolder. Guessing past an explicit answer
// would hide the mistake in it. A dropped FILE means its folder, so dragging
// `TUBES.EXE` or `TUBES.RES` itself onto the port works as well.
//
// Nothing here touches SDL. The caller supplies the executable's directory
// and shows the message; this decides where to look and what to say.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace tubes {

// The file whose presence identifies a Tubes install.
extern const char* const kGameDataFile;

struct GameDirSearch {
    std::string given;       // --gamedir or a dropped path; empty if neither
    std::string cwd;         // the current directory
    std::string exeDir;      // where the executable is; empty if unknown
    std::string remembered;  // from the settings file; empty if none
};

struct GameDirLookup {
    std::string dir;                 // where the game is; empty if not found
    std::vector<std::string> tried;  // every directory looked in, in order
    bool given = false;              // the player named the place
};

// True when `dir` holds the game data. The default for `findGameDir`.
bool hasGameData(const std::string& dir);

// Every path in and out is UTF-8 and absolute, in the platform's own
// separators, so it can be shown to the player and stored as it is.
GameDirLookup findGameDir(
    const GameDirSearch& search,
    const std::function<bool(const std::string&)>& isGameDir = hasGameData);

// What to tell a player whose game could not be found: what the port needs,
// where it looked, how to fix it, and where to get a copy. `exeName` is how
// the player sees the program, `tubes-port.exe` on Windows.
std::string gameNotFoundMessage(const GameDirLookup& lookup,
                                const std::string& exeName);

}  // namespace tubes
