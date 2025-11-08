#pragma once

#include <random>
#include <vector>

#include <notcurses/notcurses.h>

#include "animation.h"
#include "../config.h"

namespace when {
namespace animations {

class PleasureAnimation : public Animation {
public:
    PleasureAnimation();
    ~PleasureAnimation() override;

    void init(notcurses* nc, const AppConfig& config) override;
    void update(float delta_time,
                const AudioMetrics& metrics,
                const AudioFeatures& features) override;
    void render(notcurses* nc) override;

    void activate() override;
    void deactivate() override;

    bool is_active() const override { return is_active_; }
    int get_z_index() const override { return z_index_; }
    ncplane* get_plane() const override { return plane_; }

    void bind_events(const AnimationConfig& config, events::EventBus& bus) override;

private:
    void create_or_resize_plane(notcurses* nc);
    struct LineState;

    void initialize_line_states();
    void initialize_line(LineState& line_state);
    float random_between(float min_value, float max_value);
    void load_parameters_from_config(const AnimationConfig& config_entry);
    void draw_occluded_line(std::vector<uint8_t>& cells,
                            unsigned int cell_rows,
                            unsigned int cell_cols,
                            int y1,
                            int x1,
                            int y2,
                            int x2,
                            std::vector<int>& skyline_buffer);
    void set_braille_pixel(std::vector<uint8_t>& cells,
                           unsigned int cell_cols,
                           int y,
                           int x) const;
    void blit_braille_cells(ncplane* plane,
                            const std::vector<uint8_t>& cells,
                            unsigned int cell_rows,
                            unsigned int cell_cols) const;
    void configure_history_capacity();

    ncplane* plane_ = nullptr;
    int z_index_ = 0;
    bool is_active_ = true;

    unsigned int plane_rows_ = 0;
    unsigned int plane_cols_ = 0;
    int plane_origin_y_ = 0;
    int plane_origin_x_ = 0;

    struct RidgeState {
        float current_pos = 0.0f;
        float target_pos = 0.0f;
        float current_magnitude = 0.0f;
        float target_magnitude = 0.0f;
        float noise_timer = 0.0f;
        float noise_interval = 0.0f;
        float base_pos = 0.0f;
        float phase_offset = 0.0f;
        float beat_emphasis = 0.0f;
        int band_index = 1;
    };

    struct LineState {
        std::vector<RidgeState> ridges;
        std::vector<float> line_profile;
        float highlight_pos = 0.5f;
        float highlight_strength = 0.0f;
    };

    struct PleasureParameters {
        float magnitude_scale = 4.5f;
        float history_smoothing = 0.2f;
        float global_envelope_smoothing = 0.08f;
        float profile_smoothing = 0.25f;
        float ridge_magnitude_smoothing = 0.18f;
        float ridge_position_smoothing = 0.12f;
        float center_band_width = 0.38f;
        float ridge_sigma = 0.035f;
        float ridge_position_jitter = 0.045f;
        float ridge_magnitude_jitter = 0.35f;
        float ridge_interval_min = 0.35f;
        float ridge_interval_max = 0.75f;
        float history_beat_boost = 2.2f;
        float beat_response = 0.7f;
        float beat_attack_boost = 3.0f;
        float ridge_noise_acceleration = 1.8f;
        float profile_noise_amount = 0.05f;
        float beat_phase_depth = 0.45f;
        float beat_phase_power = 1.1f;
        float beat_pulse_attack = 18.0f;
        float beat_pulse_release = 4.0f;
        float beat_phase_sway = 0.06f;
        float downbeat_flash_strength = 0.35f;
        float downbeat_flash_decay = 2.5f;
        float global_headroom = 1.3f;
        float ridge_headroom = 1.45f;
        float profile_headroom = 1.35f;
        float soft_clip_knee = 0.65f;
        float band_beat_gain = 1.25f;
        float band_beat_decay = 2.0f;
        float band_reseed_jitter = 0.08f;
        float highlight_flux_threshold = 0.22f;
        float highlight_attack = 10.0f;
        float highlight_release = 3.5f;
        float highlight_width = 0.085f;
        float highlight_gain = 0.85f;
        float highlight_position_smoothing = 0.18f;
        float highlight_flatness_threshold = 0.55f;
        float highlight_tonal_bias = 0.45f;
        int min_ridges = 3;
        int max_ridges = 5;
        int line_spacing = 3;
        int max_lines = 32;
        int baseline_margin = 4;
        int max_upward_excursion = 28;
        int max_downward_excursion = 6;
    };

    std::vector<LineState> lines_;
    std::mt19937 rng_;
    std::size_t history_capacity_ = 0u;
    float last_magnitude_ = 0.0f;
    float global_magnitude_ = 0.0f;
    float beat_pulse_ = 0.0f;
    float downbeat_flash_ = 0.0f;
    PleasureParameters params_{};
};

} // namespace animations
} // namespace when

