#include "paths.h"

#include <cstring>

namespace tubes {

std::filesystem::path fsPath(const std::string& utf8) {
    // `u8path` is the C++17 spelling for "these bytes are UTF-8". C++20
    // deprecates it in favour of `char8_t`, which this project does not use.
    return std::filesystem::u8path(utf8);
}

std::string utf8Of(const std::filesystem::path& p) {
    return p.u8string();
}

std::FILE* openFile(const std::string& utf8, const char* mode) {
#ifdef _WIN32
    // Modes are ASCII, so widening them byte by byte is exact.
    wchar_t wmode[8] = {};
    for (size_t i = 0; i + 1 < sizeof(wmode) / sizeof(wmode[0]) && mode[i]; ++i) {
        wmode[i] = static_cast<wchar_t>(mode[i]);
    }
    return _wfopen(fsPath(utf8).c_str(), wmode);
#else
    return std::fopen(utf8.c_str(), mode);
#endif
}

}  // namespace tubes
