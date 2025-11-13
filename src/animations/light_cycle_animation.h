#pragma once

#include <cstdint>
#include <deque>
#include <random>
#include <vector>

#include "animation.h"

namespace when {
namespace animations {

struct LightCycleTrailPoint {
    float x = 0.5f;
    float y = 0.5f;
    float spawn_time = 0.0f;
    float thickness = 1.0f;
    float intensity = 1.0f;
};

struct LightCycleColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

class LightCycleAnimation : public Animation {
public:
    LightCycleAnimation();
    ~LightCycleAnimation() override;

    void init(notcurses* nc, const AppConfig& config) override;
    void update(float delta_time, const AudioMetrics& metrics, const AudioFeatures& features) override;
    void render(notcurses* nc) override;
    void activate() override;
    void deactivate() override;

    bool is_active() const override;
    int get_z_index() const override;
    ncplane* get_plane() const override;

    void bind_events(const AnimationConfig& config, events::EventBus& bus) override;

private:
    enum class Orientation { Horizontal, Vertical };

    void create_or_resize_plane(notcurses* nc);
    void draw_frame(int frame_y, int frame_x, int frame_height, int frame_width);
    bool render_point(float normalized_x,
                      float normalized_y,
                      float brightness,
                      float thickness,
                      const LightCycleColor& color_scale,
                      int frame_y,
                      int frame_x,
                      int interior_height,
                      int interior_width);
    float compute_trail_brightness(float age) const;
    void ensure_cycle_seeded();
    void append_trail_sample(float thickness, float intensity);
    void trim_trail();
    void attempt_turn(const AudioFeatures& features, bool forced);
    int choose_direction(Orientation orientation, const AudioFeatures& features);
    void clamp_head_to_bounds();

    ncplane* plane_ = nullptr;
    bool is_active_ = false;
    int z_index_ = 0;
    unsigned int plane_rows_ = 0;
    unsigned int plane_cols_ = 0;

    std::deque<LightCycleTrailPoint> trail_;
    std::vector<std::uint8_t> braille_masks_;
    std::vector<LightCycleColor> accumulation_buffer_;

    std::mt19937 rng_;

    Orientation orientation_ = Orientation::Horizontal;
    int direction_sign_ = 1;
    float head_x_ = 0.5f;
    float head_y_ = 0.5f;
    float anchor_coordinate_ = 0.5f;
    AudioFeatures last_features_{};

    float elapsed_time_ = 0.0f;
    float time_since_last_turn_ = 0.0f;
    float current_thickness_ = 1.0f;
    float glow_intensity_ = 0.5f;

    // Configuration
    float base_speed_ = 0.35f;
    float energy_speed_scale_ = 0.45f;
    float tail_duration_s_ = 5.0f;
    float tail_fade_power_ = 1.6f;
    float turn_cooldown_s_ = 0.35f;
    float beat_turn_threshold_ = 0.55f;
    float energy_turn_threshold_ = 0.85f;
    float thickness_min_ = 0.6f;
    float thickness_max_ = 3.6f;
    float thickness_smoothing_ = 0.18f;
    float intensity_smoothing_ = 0.16f;

    LightCycleColor trail_color_ = {0.15f, 0.7f, 1.0f};
    LightCycleColor head_color_ = {0.35f, 0.9f, 1.0f};
};

} // namespace animations
} // namespace when

