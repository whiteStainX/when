#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "animations/band/sprite_types.h"

namespace fs = std::filesystem;

namespace {
fs::path write_temp_file(const std::string& name, const std::string& contents) {
    fs::path path = fs::temp_directory_path() / name;
    std::ofstream out(path, std::ios::trunc);
    out << contents;
    return path;
}
}

int main() {
    using when::animations::band::SpritePlayer;
    using when::animations::band::SpriteSequence;
    using when::animations::band::load_sprite_frames_from_file;
    using when::animations::band::load_sprite_sequence_from_file;

    // Valid file with two frames
    auto valid_path = write_temp_file("sprite_valid.txt",
                                      "abc\nabc\n\nabc\nabc\n");
    auto frames = load_sprite_frames_from_file(valid_path);
    assert(frames.size() == 2);
    assert(frames.front().width == 3);
    assert(frames.front().height == 2);

    SpriteSequence sequence = load_sprite_sequence_from_file(valid_path);
    assert(sequence.size() == frames.size());
    assert(sequence.front().width == 3);
    assert(sequence.front().height == 2);

    // Invalid due to inconsistent width
    auto invalid_path = write_temp_file("sprite_invalid.txt",
                                        "abc\nzz\n");
    bool threw = false;
    try {
        (void)load_sprite_frames_from_file(invalid_path);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw && "Expected width mismatch to throw");

    auto height_mismatch_path = write_temp_file("sprite_height.txt",
                                               "aa\naa\n\naa\n");
    threw = false;
    try {
        (void)load_sprite_frames_from_file(height_mismatch_path);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw && "Expected height mismatch to throw");

    // Sprite player progression
    SpritePlayer player;
    player.set_sequence(&sequence);
    player.set_fps(2.0f); // 0.5s per frame

    player.update(0.25f, 0.0f, 0.0f);
    assert(&player.current() == &sequence.frames.front());

    player.update(0.5f, 0.0f, 0.0f);
    assert(&player.current() == &sequence.frames.back());

    // Compatibility shim: allow direct vector binding until full migration
    player.set_sequence(&frames);
    player.reset();
    player.update(0.5f, 0.0f, 0.0f);
    assert(&player.current() == &frames.back());

    // Phase lock test: should advance once on beat reset
    player.set_phase_lock(true);
    player.reset();
    player.set_sequence(&sequence);
    player.update(0.1f, 0.2f, 0.0f);
    assert(&player.current() == &sequence.frames.front());
    player.update(0.1f, 0.1f, 0.0f); // no wrap yet
    assert(&player.current() == &sequence.frames.front());
    player.update(0.1f, 0.9f, 0.0f);
    assert(&player.current() == &sequence.frames.front());
    player.update(0.1f, 0.1f, 0.0f); // wrap around (0.1 < 0.9 - 0.5)
    assert(&player.current() == &sequence.frames.back());

    return 0;
}

