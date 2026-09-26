#include "gamedir.h"

#include <filesystem>
#include <system_error>

#include "paths.h"

namespace tubes {

const char* const kGameDataFile = "TUBES.RES";

namespace {

namespace fs = std::filesystem;

// Absolute, with `.` and `..` folded away, in native separators. Falls back
// to the lexical form when the filesystem cannot answer, so a lookup never
// throws on a strange path - it just does not find anything there.
std::string normalised(const fs::path& p) {
    std::error_code ec;
    fs::path abs = fs::absolute(p, ec);
    if (ec) abs = p;
    abs = abs.lexically_normal();
    abs.make_preferred();
    std::string s = utf8Of(abs);
    // A trailing separator (as `SDL_GetBasePath` returns) would make the same
    // directory compare unequal to itself.
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\') &&
           !(s.size() == 3 && s[1] == ':')) {
        s.pop_back();
    }
    return s;
}

void addWithSubfolder(std::vector<std::string>& out, const std::string& dir) {
    if (dir.empty()) return;
    const std::string base = normalised(fsPath(dir));
    const std::string sub = normalised(fsPath(base) / "TUBES");
    for (const std::string& d : {base, sub}) {
        bool seen = false;
        for (const std::string& t : out) seen = seen || t == d;
        if (!seen) out.push_back(d);
    }
}

}  // namespace

bool hasGameData(const std::string& dir) {
    std::error_code ec;
    return fs::is_regular_file(fsPath(dir) / kGameDataFile, ec);
}

GameDirLookup findGameDir(
    const GameDirSearch& search,
    const std::function<bool(const std::string&)>& isGameDir) {
    GameDirLookup out;
    std::vector<std::string> candidates;

    if (!search.given.empty()) {
        out.given = true;
        fs::path given = fsPath(search.given);
        // A dropped file means the folder it is in.
        std::error_code ec;
        if (fs::is_regular_file(given, ec)) given = given.parent_path();
        addWithSubfolder(candidates, utf8Of(given));
    } else {
        addWithSubfolder(candidates, search.cwd);
        addWithSubfolder(candidates, search.exeDir);
        if (!search.remembered.empty()) {
            // Only the directory itself: it is where the game WAS, and a
            // subfolder of it was never the answer.
            const std::string r = normalised(fsPath(search.remembered));
            bool seen = false;
            for (const std::string& t : candidates) seen = seen || t == r;
            if (!seen) candidates.push_back(r);
        }
    }

    for (const std::string& dir : candidates) {
        out.tried.push_back(dir);
        if (isGameDir(dir)) {
            out.dir = dir;
            break;
        }
    }
    return out;
}

std::string gameNotFoundMessage(const GameDirLookup& lookup,
                                const std::string& exeName) {
    std::string m;
    m += "tubes-port plays Tubes, the 1994 DOS game, from the game's own\n"
         "data files. It does not include them, and it could not find them.\n"
         "\n";
    m += std::string("It needs ") + kGameDataFile +
         " (and DRIVERS.RES beside it, for music).\n";
    m += "It looked in:\n";
    for (const std::string& d : lookup.tried) m += "    " + d + "\n";
    m += "\n";
    if (lookup.given) {
        m += "That is the folder you gave it. Check it is the one that\n"
             "holds " + std::string(kGameDataFile) + ".\n\n";
    }
    m += "To fix it, do one of these:\n"
         "  - Put " + exeName + " in the folder that holds " +
         kGameDataFile + ",\n"
         "    and run it from there.\n"
         "  - Drag that folder onto " + exeName + ".\n"
         "It remembers the folder, so after that it runs from anywhere.\n"
         "\n"
         "No copy of Tubes? Any copy works, including the free shareware\n"
         "release, which the Internet Archive hosts:\n"
         "    https://archive.org/details/msdos_TUBES_shareware\n"
         "Unzip it and put " + exeName + " beside " + kGameDataFile + ".\n";
    return m;
}

}  // namespace tubes
