#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

#include "animations/band/sprite_types.h"

namespace fs = std::filesystem;

int main() {
    using when::animations::band::SpriteSequence;
    using when::animations::band::load_sprite_sequence_from_directory;

    const fs::path sprites_root{"assets/sprites"};
    assert(fs::exists(sprites_root) && fs::is_directory(sprites_root));

    const std::vector<std::string> members{"guitarist", "bassist", "drummer", "vocal"};
    const std::vector<std::string> forbidden_files{"idle.txt", "normal.txt", "fast.txt", "spotlight.txt", "spotlight_hi.txt"};

    for (const auto& member : members) {
        const fs::path member_root = sprites_root / member;
        assert(fs::exists(member_root) && fs::is_directory(member_root));

        for (const auto& name : forbidden_files) {
            const fs::path legacy = member_root / name;
            assert(!fs::exists(legacy) && "Legacy state-based sprite files should be removed");
        }

        SpriteSequence sequence;
        try {
            sequence = load_sprite_sequence_from_directory(member_root);
        } catch (...) {
            assert(false && "Failed to load sprite sequence from directory; see logs for details");
        }

        assert(!sequence.empty() && "Sprite sequence must contain at least one frame");

        const auto& first_frame = sequence.front();
        assert(!first_frame.rows.empty() && "Sprite frame should contain rows");
        const int expected_width = static_cast<int>(first_frame.rows.front().size());
        const int expected_height = static_cast<int>(first_frame.rows.size());
        assert(expected_width > 0 && expected_height > 0 && "Sprite frames must have positive dimensions");

        for (const auto& frame : sequence.frames) {
            assert(!frame.rows.empty() && "Sprite frame should contain rows");
            assert(static_cast<int>(frame.rows.size()) == expected_height && "All frames must share the same height");
            for (const auto& row : frame.rows) {
                assert(static_cast<int>(row.size()) == expected_width && "All frames must share the same width");
            }
        }
    }

    const fs::path demo_root = sprites_root / "directory_demo";
    assert(fs::exists(demo_root) && fs::is_directory(demo_root));

    SpriteSequence demo_sequence;
    try {
        demo_sequence = load_sprite_sequence_from_directory(demo_root);
    } catch (...) {
        assert(false && "Directory demo sequence failed to load");
    }

    assert(demo_sequence.size() == 3 && "Directory demo must expose three frames");
    assert(demo_sequence.at(0).rows.front() == "A");
    assert(demo_sequence.at(1).rows.front() == "B");
    assert(demo_sequence.at(2).rows.front() == "C");

    return 0;
}
