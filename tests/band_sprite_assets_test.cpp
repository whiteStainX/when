#include <cassert>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "animations/band/sprite_types.h"

namespace fs = std::filesystem;

int main() {
    using when::animations::band::SpriteFileSet;
    using when::animations::band::load_sprite_set;

    const fs::path sprites_root{"assets/sprites"};
    assert(fs::exists(sprites_root) && fs::is_directory(sprites_root));

    const std::vector<std::string> members{"guitarist", "bassist", "drummer", "vocal"};

    const SpriteFileSet required_files{
        .idle = "idle.txt",
        .normal = "normal.txt",
        .fast = "fast.txt",
        .spotlight = "spotlight.txt",
        .spotlight_hi = std::nullopt,
    };

    for (const auto& member : members) {
        const fs::path member_root = sprites_root / member;
        assert(fs::exists(member_root) && fs::is_directory(member_root));

        when::animations::band::SpriteSet set;
        try {
            set = load_sprite_set(member_root, required_files);
        } catch (...) {
            assert(false && "Sprite set failed to load; see logs for details");
        }

        assert(!set.idle.empty() && "Idle animation must have frames");
        assert(!set.normal.empty() && "Normal animation must have frames");
        assert(!set.fast.empty() && "Fast animation must have frames");
        assert(!set.spotlight.empty() && "Spotlight animation must have frames");
    }

    return 0;
}
