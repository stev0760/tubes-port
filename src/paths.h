// Every path in the port is UTF-8, and this is where it becomes a file.
//
// SDL hands the program UTF-8 everywhere: `SDL_GetPrefPath`, `SDL_GetBasePath`,
// and on Windows `argv` itself, which `SDL2main` rebuilds from the wide
// command line. The C and C++ libraries do not agree. On Windows a narrow
// `fopen` or `std::ifstream(std::string)` reads its argument in the ANSI code
// page, so any path with a character outside it - a Tubes folder called
// `Jeux rétro`, or a settings file under `C:\Users\José\AppData` - silently
// fails to open. On Linux and macOS the narrow calls already take UTF-8 and
// these are plain pass-throughs.
//
// So nothing opens a file from a `std::string` directly. It goes through
// `fsPath`, which builds a `std::filesystem::path` that knows the bytes are
// UTF-8, or `openFile`, which is `fopen` over the same conversion.

#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

namespace tubes {

std::filesystem::path fsPath(const std::string& utf8);

// The inverse, for a path the filesystem produced and the port will print,
// store or join as text.
std::string utf8Of(const std::filesystem::path& p);

// `std::fopen` with a UTF-8 path. `mode` is the usual "rb", "wb", "r"...
std::FILE* openFile(const std::string& utf8, const char* mode);

}  // namespace tubes
