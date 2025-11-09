#include <cassert>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "animations/band/sprite_types.h"

namespace fs = std::filesystem;

int main() {
    using when::animations::band::SpriteFileSet;
    using when::animations::band::SpriteSequence;
    using when::animations::band::load_sprite_sequence_from_directory;
    using when::animations::band::load_sprite_sequence_from_file;
    using when::animations::band::load_sprite_set;

    const fs::path sprites_root{"assets/sprites"};
    assert(fs::exists(sprites_root) && fs::is_directory(sprites_root));

    const std::vector<std::string> members{"guitarist", "bassist", "drummer", "vocal"};
    const std::vector<std::string> directory_members{"directory_demo"};

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

        auto ensure_sequence = [&](const fs::path& relative_path) {
            try {
                SpriteSequence sequence = load_sprite_sequence_from_file(member_root / relative_path);
                assert(!sequence.empty() && "Sprite sequence must not be empty");
            } catch (...) {
                assert(false && "Sprite sequence failed to load; see logs for details");
            }
        };

        ensure_sequence(required_files.idle);
        ensure_sequence(required_files.normal);
        ensure_sequence(required_files.fast);
        ensure_sequence(required_files.spotlight);

        // Legacy shim: continue verifying the state-based loader until full migration
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

    for (const auto& member : directory_members) {
        const fs::path member_root = sprites_root / member;
        assert(fs::exists(member_root) && fs::is_directory(member_root));

        when::animations::band::SpriteSequence sequence;
        try {
            sequence = load_sprite_sequence_from_directory(member_root);
        } catch (...) {
            assert(false && "Directory-based sprite sequence failed to load");
        }

        assert(sequence.size() == 3 && "Directory demo must expose three frames");
        assert(sequence.at(0).rows.front() == "A");
        assert(sequence.at(1).rows.front() == "B");
        assert(sequence.at(2).rows.front() == "C");
    }

    return 0;
}
