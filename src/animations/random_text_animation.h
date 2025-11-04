#pragma once

#include <chrono>
#include <random>
#include <string>
#include <vector>

#include "animation.h"
#include "../config.h" // Explicitly include AppConfig

namespace why {
namespace animations {

class RandomTextAnimation : public Animation {
public:
    RandomTextAnimation();
    ~RandomTextAnimation() override;

    void init(notcurses* nc, const AppConfig& config) override;
    void update(float delta_time,
                const AudioMetrics& metrics,
                const std::vector<float>& bands,
                float beat_strength) override;
    void render(notcurses* nc) override;

    void activate() override;
    void deactivate() override;

    bool is_active() const override { return is_active_ || !active_lines_.empty() || plane_needs_clear_; }
    int get_z_index() const override { return z_index_; }
    ncplane* get_plane() const override { return plane_; }

    void bind_events(const AnimationConfig& config, events::EventBus& bus) override;

private:
    struct DisplayedLine {
        std::string text;
        std::size_t revealed_chars = 0;
        float time_since_last_char = 0.0f;
        float char_interval = 0.0f;
        bool completed = false;
        float display_elapsed = 0.0f;
        float fade_elapsed = 0.0f;
        bool fading_out = false;
        int x_pos = 0;
        int y_pos = 0;
    };

    void load_quotes();
    void spawn_line();
    void clamp_line_positions();
    float compute_char_interval(const std::string& text) const;
    std::string select_random_quote();

    std::mt19937_64 rng_;
    std::uniform_int_distribution<std::size_t> quote_dist_;
    std::vector<std::string> quotes_;
    std::vector<DisplayedLine> active_lines_;
    ncplane* plane_ = nullptr;
    int z_index_ = 0;
    bool is_active_ = true; // New: internal active state
    bool plane_needs_clear_ = false;
    int trigger_band_index_ = -1;
    float trigger_threshold_ = 0.0f;
    float trigger_beat_min_ = 0.0f;
    float trigger_beat_max_ = 1.0f;
    float type_speed_words_per_s_ = 4.0f;
    float display_duration_s_ = 3.0f;
    float fade_duration_s_ = 1.0f;
    float trigger_cooldown_s_ = 0.75f;
    int max_active_lines_ = 4;
    float min_y_ratio_ = 0.0f;
    float max_y_ratio_ = 1.0f;
    float time_since_last_trigger_ = 0.0f;
    std::string text_file_path_ = "assets/dune.txt";
};

} // namespace animations
} // namespace why