#include <notcurses/notcurses.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>

#include "random_text_animation.h"
#include "animation_event_utils.h"

namespace why {
namespace animations {

RandomTextAnimation::RandomTextAnimation()
    : rng_(std::chrono::steady_clock::now().time_since_epoch().count()),
      quote_dist_(0, 0) {}

RandomTextAnimation::~RandomTextAnimation() {
    if (plane_) {
        ncplane_destroy(plane_);
    }
}

void RandomTextAnimation::init(notcurses* nc, const AppConfig& config) {
    ncplane* stdplane = notcurses_stdplane(nc);
    unsigned int plane_rows = 0;
    unsigned int plane_cols = 0;
    ncplane_dim_yx(stdplane, &plane_rows, &plane_cols);

    ncplane_options p_opts{};
    p_opts.rows = plane_rows;
    p_opts.cols = plane_cols;
    p_opts.y = 0;
    p_opts.x = 0;
    plane_ = ncplane_create(stdplane, &p_opts);
    if (!plane_) {
        // std::cerr << "[RandomTextAnimation::init] Failed to create ncplane!" << std::endl;
    } else {
        // std::clog << "[RandomTextAnimation::init] ncplane created successfully." << std::endl;
    }

    // Set configuration from config file
    for (const auto& anim_config : config.animations) {
        if (anim_config.type == "RandomText") {
            z_index_ = anim_config.z_index;
            is_active_ = anim_config.initially_active;
            trigger_band_index_ = anim_config.trigger_band_index;
            trigger_threshold_ = anim_config.trigger_threshold;
            trigger_beat_min_ = anim_config.trigger_beat_min;
            trigger_beat_max_ = anim_config.trigger_beat_max;
            if (!anim_config.text_file_path.empty()) {
                text_file_path_ = anim_config.text_file_path;
            }
            if (anim_config.type_speed_words_per_s > 0.0f) {
                type_speed_words_per_s_ = anim_config.type_speed_words_per_s;
            }
            if (anim_config.display_duration_s > 0.0f) {
                display_duration_s_ = anim_config.display_duration_s;
            }
            if (anim_config.fade_duration_s > 0.0f) {
                fade_duration_s_ = anim_config.fade_duration_s;
            }
            if (anim_config.trigger_cooldown_s >= 0.0f) {
                trigger_cooldown_s_ = anim_config.trigger_cooldown_s;
            }
            if (anim_config.max_active_lines > 0) {
                max_active_lines_ = anim_config.max_active_lines;
            }

            min_y_ratio_ = std::clamp(anim_config.random_text_min_y_ratio, 0.0f, 1.0f);
            max_y_ratio_ = std::clamp(anim_config.random_text_max_y_ratio, 0.0f, 1.0f);
            if (max_y_ratio_ < min_y_ratio_) {
                std::swap(min_y_ratio_, max_y_ratio_);
            }
            break;
        }
    }

    load_quotes();
}

void RandomTextAnimation::activate() {
    is_active_ = true;
    if (plane_) {
        ncplane_set_fg_rgb8(plane_, 255, 255, 255); // White foreground
        ncplane_set_bg_rgb8(plane_, 0, 0, 0);     // Black background
    }
    time_since_last_trigger_ = trigger_cooldown_s_;
    plane_needs_clear_ = false;
}

void RandomTextAnimation::deactivate() {
    is_active_ = false;
    if (active_lines_.empty() && plane_) {
        ncplane_erase(plane_);
        plane_needs_clear_ = false;
    }
}

void RandomTextAnimation::update(float delta_time,
                                 const AudioMetrics& metrics,
                                 const std::vector<float>& bands,
                                 float beat_strength) {
    if (!plane_) return;

    (void)metrics;

    const bool had_lines_before_update = !active_lines_.empty();

    time_since_last_trigger_ += delta_time;

    // Only attempt to spawn new lines if the animation is currently active
    if (is_active_) {
        float audio_value = beat_strength;
        if (trigger_band_index_ >= 0 && trigger_band_index_ < static_cast<int>(bands.size())) {
            audio_value = bands[static_cast<std::size_t>(trigger_band_index_)];
        }

        const bool beat_in_range = beat_strength >= trigger_beat_min_ && beat_strength <= trigger_beat_max_;
        const bool condition_met = beat_in_range && audio_value >= trigger_threshold_;
        if (condition_met) {
            if (trigger_cooldown_s_ <= 0.0f) {
                spawn_line();
                time_since_last_trigger_ = 0.0f;
            } else if (time_since_last_trigger_ >= trigger_cooldown_s_) {
                const int spawn_budget = std::max(1, static_cast<int>(time_since_last_trigger_ / trigger_cooldown_s_));
                for (int i = 0; i < spawn_budget; ++i) {
                    spawn_line();
                    if (static_cast<int>(active_lines_.size()) >= max_active_lines_) {
                        break;
                    }
                }
                time_since_last_trigger_ = std::fmod(time_since_last_trigger_, trigger_cooldown_s_);
            }
        }
    }

    // Always update existing lines, regardless of whether the animation is currently spawning new ones
    for (auto& line : active_lines_) {
        if (!line.completed) {
            if (line.char_interval <= 0.0f || type_speed_words_per_s_ <= 0.0f) {
                line.revealed_chars = line.text.size();
                line.completed = true;
                line.time_since_last_char = 0.0f;
            } else {
                line.time_since_last_char += delta_time;
                while (line.time_since_last_char >= line.char_interval && line.revealed_chars < line.text.size()) {
                    line.time_since_last_char -= line.char_interval;
                    ++line.revealed_chars;
                }
                if (line.revealed_chars >= line.text.size()) {
                    line.revealed_chars = line.text.size();
                    line.completed = true;
                    line.time_since_last_char = 0.0f;
                }
            }
        } else if (!line.fading_out) {
            line.display_elapsed += delta_time;
            if (line.display_elapsed >= display_duration_s_) {
                line.fading_out = true;
                line.fade_elapsed = 0.0f;
            }
        } else {
            line.fade_elapsed += delta_time;
        }
    }

    active_lines_.erase(
        std::remove_if(active_lines_.begin(),
                        active_lines_.end(),
                        [&](const DisplayedLine& line) {
                            if (!line.fading_out) {
                                return false;
                            }
                            if (fade_duration_s_ <= 0.0f) {
                                return true;
                            }
                            return line.fade_elapsed >= fade_duration_s_;
                        }),
        active_lines_.end());

    if (had_lines_before_update && active_lines_.empty()) {
        plane_needs_clear_ = true;
    }

    clamp_line_positions();
}

void RandomTextAnimation::render(notcurses* nc) {
    if (!plane_) return;

    (void)nc;

    ncplane_erase(plane_);
    plane_needs_clear_ = false;

    unsigned int plane_rows = 0;
    unsigned int plane_cols = 0;
    ncplane_dim_yx(plane_, &plane_rows, &plane_cols);

    ncplane_set_bg_rgb8(plane_, 0, 0, 0);

    for (const auto& line : active_lines_) {
        if (line.revealed_chars == 0 || line.text.empty()) {
            continue;
        }

        float fade_factor = 1.0f;
        if (line.fading_out && fade_duration_s_ > 0.0f) {
            fade_factor = std::max(0.0f, 1.0f - (line.fade_elapsed / fade_duration_s_));
        }

        const unsigned char intensity = static_cast<unsigned char>(std::clamp(fade_factor, 0.0f, 1.0f) * 255.0f);
        ncplane_set_fg_rgb8(plane_, intensity, intensity, intensity);

        int y = plane_rows > 0 ? std::clamp(line.y_pos, 0, static_cast<int>(plane_rows) - 1) : 0;
        int max_x = plane_cols > 0 ? static_cast<int>(plane_cols) - 1 : 0;
        int x = std::clamp(line.x_pos, 0, std::max(0, max_x));

        ncplane_putnstr_yx(plane_, y, x, line.revealed_chars, line.text.c_str());
    }
}

void RandomTextAnimation::load_quotes() {
    quotes_.clear();

    auto trim = [](std::string s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
        return s;
    };

    auto try_load = [&](const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            std::string cleaned = trim(line);
            if (!cleaned.empty()) {
                quotes_.push_back(cleaned);
            }
        }
        return !quotes_.empty();
    };

    if (!try_load(text_file_path_) && text_file_path_ != "assets/dune.txt") {
        try_load("assets/dune.txt");
    }

    if (quotes_.empty()) {
        quotes_.push_back("Fear is the mind-killer.");
    }

    quote_dist_ = std::uniform_int_distribution<std::size_t>(0, quotes_.size() - 1);
}

std::string RandomTextAnimation::select_random_quote() {
    if (quotes_.empty()) {
        return {};
    }
    const std::size_t index = quote_dist_(rng_);
    return quotes_[index];
}

void RandomTextAnimation::spawn_line() {
    if (quotes_.empty()) {
        return;
    }

    if (!plane_) {
        return;
    }

    if (static_cast<int>(active_lines_.size()) >= max_active_lines_) {
        active_lines_.erase(active_lines_.begin());
    }

    DisplayedLine line;
    line.text = select_random_quote();
    if (line.text.empty()) {
        return;
    }

    line.char_interval = compute_char_interval(line.text);

    unsigned int plane_rows = 0;
    unsigned int plane_cols = 0;
    ncplane_dim_yx(plane_, &plane_rows, &plane_cols);

    if (plane_rows > 0) {
        const int max_row_index = static_cast<int>(plane_rows) - 1;
        const float clamped_min_ratio = std::clamp(min_y_ratio_, 0.0f, 1.0f);
        const float clamped_max_ratio = std::clamp(max_y_ratio_, 0.0f, 1.0f);
        const int min_row = std::clamp(static_cast<int>(std::floor(clamped_min_ratio * static_cast<float>(max_row_index))),
                                       0,
                                       std::max(0, max_row_index));
        const int max_row = std::clamp(static_cast<int>(std::ceil(clamped_max_ratio * static_cast<float>(max_row_index))),
                                       min_row,
                                       std::max(0, max_row_index));
        std::uniform_int_distribution<int> y_dist(min_row, max_row);
        line.y_pos = y_dist(rng_);
    } else {
        line.y_pos = 0;
    }

    if (plane_cols > 0) {
        std::uniform_int_distribution<int> x_dist(0, static_cast<int>(plane_cols) - 1);
        line.x_pos = x_dist(rng_);
    } else {
        line.x_pos = 0;
    }

    active_lines_.push_back(std::move(line));
}

void RandomTextAnimation::clamp_line_positions() {
    if (!plane_) {
        return;
    }

    unsigned int plane_rows = 0;
    unsigned int plane_cols = 0;
    ncplane_dim_yx(plane_, &plane_rows, &plane_cols);

    const int max_y = plane_rows > 0 ? static_cast<int>(plane_rows) - 1 : 0;
    const int max_x = plane_cols > 0 ? static_cast<int>(plane_cols) - 1 : 0;

    for (auto& line : active_lines_) {
        line.y_pos = std::clamp(line.y_pos, 0, std::max(0, max_y));
        line.x_pos = std::clamp(line.x_pos, 0, std::max(0, max_x));
    }
}

float RandomTextAnimation::compute_char_interval(const std::string& text) const {
    if (type_speed_words_per_s_ <= 0.0f) {
        return 0.0f;
    }

    std::size_t word_count = 0;
    std::size_t character_count = 0;
    bool in_word = false;

    for (unsigned char ch : text) {
        if (!std::isspace(ch)) {
            ++character_count;
            if (!in_word) {
                in_word = true;
                ++word_count;
            }
        } else {
            in_word = false;
        }
    }

    if (word_count == 0) {
        word_count = character_count > 0 ? 1 : 0;
    }

    if (word_count == 0 || character_count == 0) {
        return 0.0f;
    }

    const float average_chars_per_word = static_cast<float>(character_count) / static_cast<float>(word_count);
    const float chars_per_second = type_speed_words_per_s_ * average_chars_per_word;
    if (chars_per_second <= 0.0f) {
        return 0.0f;
    }

    return 1.0f / chars_per_second;
}

void RandomTextAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    bind_standard_frame_updates(this, config, bus);
}

} // namespace animations
} // namespace why
