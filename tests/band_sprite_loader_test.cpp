#include <cassert>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>

#include "animations/band/sprite_types.h"

namespace fs = std::filesystem;

namespace {
fs::path write_temp_file(const std::string& name, const std::string& contents) {
    fs::path path = fs::temp_directory_path() / name;
    std::ofstream out(path, std::ios::trunc);
    out << contents;
    return path;
}

fs::path make_temp_directory(const std::string& prefix) {
    static constexpr char hex_digits[] = "0123456789abcdef";
    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, 15);

    auto base = fs::temp_directory_path();
    for (int attempt = 0; attempt < 32; ++attempt) {
        std::string suffix;
        suffix.reserve(8);
        for (int i = 0; i < 8; ++i) {
            suffix.push_back(hex_digits[dist(rd)]);
        }

        fs::path candidate = base / fs::path(prefix + suffix);
        std::error_code ec;
        if (fs::create_directory(candidate, ec)) {
            return candidate;
        }

        if (ec && ec != std::errc::file_exists) {
            throw std::system_error(ec, "Failed to create temporary directory");
        }
    }

    throw std::runtime_error("Unable to create temporary directory");
}
}

int main() {
    using when::animations::band::SpritePlayer;
    using when::animations::band::SpriteSequence;
    using when::animations::band::load_sprite_frames_from_file;
    using when::animations::band::load_sprite_sequence_from_directory;
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

    // Directory loader: unsorted files should be ordered alphabetically
    fs::path directory = make_temp_directory("sprite_seq_test_");

    auto write_dir_frame = [&](const std::string& name, const std::string& contents) {
        fs::path path = directory / name;
        std::ofstream out(path, std::ios::trunc);
        out << contents;
        return path;
    };

    write_dir_frame("frame_b.txt", "B\n");
    write_dir_frame("frame_a.txt", "A\n");
    write_dir_frame("frame_c.txt", "C\n");

    SpriteSequence directory_sequence = load_sprite_sequence_from_directory(directory);
    assert(directory_sequence.size() == 3);
    assert(directory_sequence.at(0).rows.front() == "A");
    assert(directory_sequence.at(1).rows.front() == "B");
    assert(directory_sequence.at(2).rows.front() == "C");

    // Mixed dimensions across files should throw
    write_dir_frame("frame_d.txt", "DD\n");
    bool dimension_threw = false;
    try {
        (void)load_sprite_sequence_from_directory(directory);
    } catch (const std::runtime_error&) {
        dimension_threw = true;
    }
    assert(dimension_threw && "Expected mismatched frame dimensions to throw");

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

