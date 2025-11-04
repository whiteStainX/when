#pragma once

#include <random>
#include <string>
#include <vector>

#include "animation.h"

namespace why {
namespace animations {

class BreatheAnimation : public Animation {
public:
    BreatheAnimation();
    ~BreatheAnimation() override;

    void init(notcurses* nc, const AppConfig& config) override;
    void update(float delta_time,
                const AudioMetrics& metrics,
                const std::vector<float>& bands,
                float beat_strength) override;
    void render(notcurses* nc) override;

    void activate() override;
    void deactivate() override;

    bool is_active() const override { return is_active_; }
    int get_z_index() const override { return z_index_; }
    ncplane* get_plane() const override { return plane_; }

    void bind_events(const AnimationConfig& config, events::EventBus& bus) override;

private:
    void configure_from_app(const AppConfig& config);
    void create_plane(notcurses* nc);
    void refresh_dimensions();
    void reset_buffers();
    bool load_glyphs_from_file(const std::string& path);
    void ensure_glyphs_loaded();

    void update_noise_table();
    void decay_intensities(float delta_time);
    void draw_shape(float brightness);
    void draw_line(int x0, int y0, int x1, int y1, float intensity);
    void stamp_cell(int x, int y, float intensity);

    float compute_audio_energy(const AudioMetrics& metrics,
                               const std::vector<float>& bands,
                               float beat_strength) const;

    ncplane* plane_ = nullptr;
    unsigned int plane_rows_ = 0u;
    unsigned int plane_cols_ = 0u;
    int plane_origin_y_ = 0;
    int plane_origin_x_ = 0;

    int z_index_ = 0;
    bool is_active_ = true;

    std::vector<std::string> glyphs_;
    std::string glyphs_file_path_;
    bool glyphs_loaded_ = false;

    std::vector<float> cell_intensities_;

    int points_ = 64;
    float min_radius_ = 6.0f;
    float max_radius_ = 14.0f;
    float audio_radius_influence_ = 10.0f;
    float smoothing_time_s_ = 0.18f;
    float noise_amount_ = 0.3f;
    float rotation_speed_rad_s_ = 0.35f;
    float vertical_scale_ = 0.55f;
    float base_pulse_hz_ = 0.35f;
    float audio_pulse_weight_ = 0.65f;

    int audio_band_index_ = -1;
    float rms_weight_ = 1.0f;
    float beat_weight_ = 0.6f;
    float band_weight_ = 0.5f;

    float persistence_duration_s_ = 0.8f;
    float fade_duration_s_ = 1.2f;

    float smoothed_energy_ = 0.0f;
    float breathing_phase_ = 0.0f;
    float rotation_angle_ = 0.0f;

    std::vector<float> noise_phases_;
    std::mt19937 rng_;
};

} // namespace animations
} // namespace why
