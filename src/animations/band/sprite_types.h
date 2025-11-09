#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace when {
namespace animations {
namespace band {

#ifndef WHEN_BAND_ENABLE_DIRECTORY_LAYOUT
#define WHEN_BAND_ENABLE_DIRECTORY_LAYOUT 0
#endif

struct SpriteFrame {
    int width = 0;
    int height = 0;
    std::vector<std::string> rows;

    bool empty() const { return rows.empty(); }
};

struct SpriteSequence {
    std::vector<SpriteFrame> frames;

    bool empty() const { return frames.empty(); }
    std::size_t size() const { return frames.size(); }
    const SpriteFrame& at(std::size_t index) const { return frames.at(index); }
    const SpriteFrame& front() const { return frames.front(); }
    const SpriteFrame& back() const { return frames.back(); }
};

struct SpriteSet {
    std::vector<SpriteFrame> idle;
    std::vector<SpriteFrame> normal;
    std::vector<SpriteFrame> fast;
    std::vector<SpriteFrame> spotlight;
    std::vector<SpriteFrame> spotlight_hi;

    bool has_any_frames() const;
};

class SpritePlayer {
public:
    SpritePlayer() = default;

    void set_sequence(const SpriteSequence* sequence);
    void set_sequence(const std::vector<SpriteFrame>* sequence); // compatibility shim
    void set_fps(float fps);
    void set_phase_lock(bool enabled);

    void reset();
    void update(float delta_seconds, float beat_phase, float bar_phase);

    const SpriteFrame& current() const;
    bool has_sequence() const { return sequence_frames_ && !sequence_frames_->empty(); }

private:
    const SpriteSequence* sequence_container_ = nullptr;
    const std::vector<SpriteFrame>* sequence_frames_ = nullptr;
    float fps_ = 6.0f;
    float accumulator_ = 0.0f;
    std::size_t index_ = 0;
    bool phase_lock_ = false;
    float last_beat_phase_ = 0.0f;
};

struct SpriteFileSet {
    std::filesystem::path idle;
    std::filesystem::path normal;
    std::filesystem::path fast;
    std::filesystem::path spotlight;
    std::optional<std::filesystem::path> spotlight_hi;
};

std::vector<SpriteFrame> load_sprite_frames_from_file(const std::filesystem::path& path);
SpriteSequence load_sprite_sequence_from_file(const std::filesystem::path& path);
SpriteSequence load_sprite_sequence_from_directory(const std::filesystem::path& directory);
SpriteSequence load_sprite_sequence(const std::filesystem::path& path);
SpriteSet load_sprite_set(const std::filesystem::path& root, const SpriteFileSet& files);

} // namespace band
} // namespace animations
} // namespace when

