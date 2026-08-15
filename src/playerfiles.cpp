#include "playerfiles.h"

#include <fstream>

namespace tubes {

bool PlayerFiles::writeBytes(const std::string& path,
                             const std::vector<uint8_t>& bytes) {
    if (!writes_) {
        ++refusals_;
        return false;
    }
    if (path.empty()) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

bool PlayerFiles::writeText(const std::string& path, const std::string& text) {
    if (!writes_) {
        ++refusals_;
        return false;
    }
    if (path.empty()) return false;
    std::ofstream f(path);
    if (!f) return false;
    f << text;
    return static_cast<bool>(f);
}

}  // namespace tubes
