// The player's own files, and the one gate that decides whether they may be
// written.
//
// Three files belong to the person running the port, not to the port:
// `TUBES.HSC` and `TUBES.SAV` in their game directory, and the port's own
// settings file. A scripted run - a screenshot, a demo replay, a captured
// state, anything driven by a flag rather than by a person - must not touch
// any of them, because what a capture compares against has to stay still.
//
// That rule used to be three copies of `if (harness) return;`, one at the top
// of each writer, remembered by hand. It was missed once: `--auto-advance`
// walked a whole session, qualified for a high score and wrote a real
// `TUBES.HSC` into the player's game directory, silently changing what every
// later capture compared against. Nothing in the scripted paths used to write
// anything, so nobody thought about it.
//
// So the guard is a value instead of a habit. A scripted run is handed a
// blocked `PlayerFiles`, an interactive one a writing `PlayerFiles`, and the
// writers do not ask which they hold. **Every new write to a file the player
// owns goes through here** - a path that does not is the bug this class
// exists to make visible.
//
// Nothing else changes hands: the harness's own outputs - a `--screenshot`
// BMP, `--render-mus`, `--make-save`, `--demo-csv` - are written to a path the
// caller named on the command line and are not the player's files.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tubes {

class PlayerFiles {
public:
    // A person is playing: writes go out.
    static PlayerFiles writing() { return PlayerFiles(true); }
    // A flag is driving: every write is refused and counted.
    static PlayerFiles blocked() { return PlayerFiles(false); }

    // Both return true only when bytes actually reached the disk, so a caller
    // that wants to report a failed save can, and a refusal reads the same as
    // a failure to a caller that does not care which it was.
    bool writeBytes(const std::string& path, const std::vector<uint8_t>& bytes);
    bool writeText(const std::string& path, const std::string& text);

    bool writes() const { return writes_; }
    // Counted rather than merely blocked, so a test can prove a scripted run
    // tried to write and did not, which is stronger than proving no file
    // appeared - a file can be absent because the code path was never reached.
    int refusals() const { return refusals_; }

private:
    explicit PlayerFiles(bool writes) : writes_(writes) {}

    bool writes_ = true;
    int refusals_ = 0;
};

}  // namespace tubes
