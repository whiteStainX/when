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

struct SpriteFrame {
    int width = 0;
    int height = 0;
    std::vector<std::string> rows;

    bool empty() const { return rows.empty(); }
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

    void set_sequence(const std::vector<SpriteFrame>* sequence);
    void set_fps(float fps);
    void set_phase_lock(bool enabled);

    void reset();
    void update(float delta_seconds, float beat_phase, float bar_phase);

    const SpriteFrame& current() const;
    bool has_sequence() const { return sequence_ && !sequence_->empty(); }

private:
    const std::vector<SpriteFrame>* sequence_ = nullptr;
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
SpriteSet load_sprite_set(const std::filesystem::path& root, const SpriteFileSet& files);

} // namespace band
} // namespace animations
} // namespace when

