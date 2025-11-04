#pragma once

#include <random>
#include <string>
#include <vector>

#include "animation.h"

namespace why {
namespace animations {

class CyberRainAnimation : public Animation {
public:
    CyberRainAnimation();
    ~CyberRainAnimation() override;

    void init(notcurses* nc, const AppConfig& config) override;
    void update(float delta_time,
                const AudioMetrics& metrics,
                const std::vector<float>& bands,
                float beat_strength) override;
    void render(notcurses* nc) override;

    void activate() override;
    void deactivate() override;

    bool is_active() const override { return is_active_ || has_visible_cells(); }
    int get_z_index() const override { return z_index_; }
    ncplane* get_plane() const override { return plane_; }

    void bind_events(const AnimationConfig& config, events::EventBus& bus) override;

private:
    struct CellState {
        float intensity = 0.0f;
        std::string glyph;
    };

    struct ActiveDrop {
        float head_row = 0.0f;
        float head_column = 0.0f;
        float speed_rows_per_s = 0.0f;
        float horizontal_speed_cols_per_s = 0.0f;
        int length = 0;
        float strength = 0.0f;
        std::string glyph;
    };

    void refresh_dimensions();
    bool load_glyphs_from_file(const std::string& path);
    void spawn_rain_column(int column, float activation, float delta_time);
    void update_drops(float delta_time);
    void remove_finished_drops();
    float compute_high_frequency_energy(const std::vector<float>& bands) const;
    bool has_visible_cells() const;

    ncplane* plane_ = nullptr;
    unsigned int plane_rows_ = 0;
    unsigned int plane_cols_ = 0;
    int plane_origin_y_ = 0;
    int plane_origin_x_ = 0;

    std::vector<CellState> cells_;
    std::vector<ActiveDrop> active_drops_;

    std::vector<std::string> glyphs_;
    std::string glyphs_file_path_;

    int z_index_ = 0;
    bool is_active_ = true;

    int trigger_band_index_ = -1;
    float high_freq_threshold_ = 0.55f;

    float base_scan_speed_cols_per_s_ = 10.0f;
    float scan_speed_boost_cols_per_s_ = 14.0f;
    float scan_position_ = 0.0f;
    bool scan_direction_right_ = true;

    float persistence_duration_s_ = 0.6f;
    float persistence_timer_ = 0.0f;
    float fade_duration_s_ = 0.9f;
    float activation_level_ = 0.0f;
    float activation_smoothing_s_ = 0.12f;

    float drop_rate_base_per_s_ = 1.5f;
    float drop_rate_boost_per_s_ = 6.5f;
    int drop_length_min_ = 4;
    int drop_length_max_ = 9;
    float drop_speed_min_rows_per_s_ = 10.0f;
    float drop_speed_max_rows_per_s_ = 22.0f;

    float drop_spawn_accumulator_ = 0.0f;

    float rain_angle_degrees_ = 0.0f;
    float horizontal_slope_ = 0.0f;

    std::mt19937 rng_;
    std::uniform_real_distribution<float> unit_dist_;
};

} // namespace animations
} // namespace why
