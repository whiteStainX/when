#include "sprite_types.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace when {
namespace animations {
namespace band {

namespace {
bool is_frame_separator(std::string_view line) {
    if (line.empty()) {
        return true;
    }
    auto trimmed_begin = line.find_first_not_of(" \t\r\n");
    if (trimmed_begin == std::string_view::npos) {
        return true;
    }
    std::string_view trimmed = line.substr(trimmed_begin);
    return trimmed == "---";
}

void finalize_frame(SpriteFrame& frame,
                    std::vector<SpriteFrame>& frames,
                    int& global_width,
                    int& global_height,
                    const std::filesystem::path& path) {
    if (frame.rows.empty()) {
        return;
    }

    frame.height = static_cast<int>(frame.rows.size());
    frame.width = static_cast<int>(frame.rows.front().size());

    if (global_width == -1 && global_height == -1) {
        global_width = frame.width;
        global_height = frame.height;
    } else if (frame.width != global_width || frame.height != global_height) {
        throw std::runtime_error("Sprite frames in " + path.string() + " have inconsistent dimensions");
    }

    frames.emplace_back(std::move(frame));
    frame = SpriteFrame{};
}
} // namespace

bool SpriteSet::has_any_frames() const {
    return !idle.empty() || !normal.empty() || !fast.empty() || !spotlight.empty() || !spotlight_hi.empty();
}

void SpritePlayer::set_sequence(const SpriteSequence* sequence) {
    sequence_container_ = sequence;
    sequence_frames_ = sequence ? &sequence->frames : nullptr;
    reset();
}

void SpritePlayer::set_sequence(const std::vector<SpriteFrame>* sequence) {
    sequence_container_ = nullptr;
    sequence_frames_ = sequence;
    reset();
}

void SpritePlayer::set_fps(float fps) {
    fps_ = std::max(0.0f, fps);
}

void SpritePlayer::set_phase_lock(bool enabled) {
    phase_lock_ = enabled;
}

void SpritePlayer::reset() {
    accumulator_ = 0.0f;
    index_ = 0;
    last_beat_phase_ = 0.0f;
}

void SpritePlayer::update(float delta_seconds, float beat_phase, float /*bar_phase*/) {
    if (!sequence_frames_ || sequence_frames_->empty()) {
        return;
    }

    if (phase_lock_) {
        float wrapped_phase = std::clamp(beat_phase, 0.0f, 1.0f);
        if (wrapped_phase < last_beat_phase_ - 0.5f) {
            // wrap detected
            index_ = (index_ + 1) % sequence_frames_->size();
        }
        last_beat_phase_ = wrapped_phase;
        return;
    }

    if (fps_ <= 0.0f) {
        return;
    }

    accumulator_ += delta_seconds;
    const float frame_duration = 1.0f / fps_;
    while (accumulator_ >= frame_duration) {
        accumulator_ -= frame_duration;
        index_ = (index_ + 1) % sequence_frames_->size();
    }
}

const SpriteFrame& SpritePlayer::current() const {
    if (!sequence_frames_ || sequence_frames_->empty()) {
        throw std::runtime_error("SpritePlayer has no active sequence");
    }
    return sequence_frames_->at(index_);
}

std::vector<SpriteFrame> load_sprite_frames_from_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open sprite file: " + path.string());
    }

    std::vector<SpriteFrame> frames;
    SpriteFrame current;
    std::string line;
    int expected_width = -1;
    int global_width = -1;
    int global_height = -1;

    while (std::getline(input, line)) {
        if (is_frame_separator(line)) {
            finalize_frame(current, frames, global_width, global_height, path);
            expected_width = -1;
            continue;
        }

        if (expected_width == -1) {
            expected_width = static_cast<int>(line.size());
        } else if (static_cast<int>(line.size()) != expected_width) {
            throw std::runtime_error("Sprite frame row width mismatch in " + path.string());
        }
        current.rows.emplace_back(line);
    }

    finalize_frame(current, frames, global_width, global_height, path);

    if (frames.empty()) {
        throw std::runtime_error("Sprite file contains no frames: " + path.string());
    }

    // verify consistent dimensions per frame
    for (const auto& frame : frames) {
        if (frame.rows.empty()) {
            throw std::runtime_error("Sprite frame missing rows in " + path.string());
        }
        const int width = static_cast<int>(frame.rows.front().size());
        for (const auto& row : frame.rows) {
            if (static_cast<int>(row.size()) != width) {
                throw std::runtime_error("Inconsistent row width inside sprite frame: " + path.string());
            }
        }
    }

    return frames;
}

SpriteSequence load_sprite_sequence_from_file(const std::filesystem::path& path) {
    SpriteSequence sequence;
    sequence.frames = load_sprite_frames_from_file(path);
    return sequence;
}

SpriteSet load_sprite_set(const std::filesystem::path& root, const SpriteFileSet& files) {
    SpriteSet set;
    const auto resolve = [&](const std::filesystem::path& rel) {
        return root / rel;
    };

    set.idle = load_sprite_frames_from_file(resolve(files.idle));
    set.normal = load_sprite_frames_from_file(resolve(files.normal));
    set.fast = load_sprite_frames_from_file(resolve(files.fast));
    set.spotlight = load_sprite_frames_from_file(resolve(files.spotlight));

    if (files.spotlight_hi) {
        set.spotlight_hi = load_sprite_frames_from_file(resolve(*files.spotlight_hi));
    }

    return set;
}

} // namespace band
} // namespace animations
} // namespace when

