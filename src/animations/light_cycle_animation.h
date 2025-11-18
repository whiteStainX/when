#pragma once

#include <cstdint>
#include <deque>
#include <random>
#include <vector>

#include "animation.h"

namespace when {
namespace animations {

struct LightCycleColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

struct LightCycleTrailPoint {
    float x = 0.5f;
    float y = 0.5f;
    float spawn_time = 0.0f;
    float thickness = 1.0f;
    float intensity = 1.0f;
    LightCycleColor color;
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

    struct LightCycleCycle {
        Orientation orientation = Orientation::Horizontal;
        int direction_sign = 1;
        float head_x = 0.0f;
        float head_y = 0.0f;
        float anchor_coordinate = 0.5f;
        bool has_turned = false;
        float time_since_turn = 0.0f;
        float time_since_spawn = 0.0f;
        float thickness = 1.0f;
        float glow = 0.5f;
        float speed_multiplier = 1.0f;
        float min_turn_delay = 0.0f;
        LightCycleColor color;
    };

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
    void spawn_cycle();
    void update_cycle(LightCycleCycle& cycle,
                      float delta_time,
                      const AudioFeatures& features,
                      bool turn_trigger);
    void turn_cycle(LightCycleCycle& cycle, const AudioFeatures& features);
    int choose_direction(Orientation orientation,
                         const AudioFeatures& features,
                         const LightCycleCycle& cycle);
    bool cycle_inside_bounds(const LightCycleCycle& cycle) const;
    bool cycle_past_bounds(const LightCycleCycle& cycle) const;
    void append_trail_sample(const LightCycleCycle& cycle);
    void trim_trail();

    ncplane* plane_ = nullptr;
    bool is_active_ = false;
    int z_index_ = 0;
    unsigned int plane_rows_ = 0;
    unsigned int plane_cols_ = 0;

    std::deque<LightCycleTrailPoint> trail_;
    std::vector<LightCycleCycle> cycles_;
    std::vector<std::uint8_t> braille_masks_;
    std::vector<LightCycleColor> accumulation_buffer_;

    std::mt19937 rng_;

    float elapsed_time_ = 0.0f;
    float time_since_last_spawn_ = 0.0f;

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

    static constexpr std::size_t kMaxActiveCycles = 4;
    static constexpr float kEntryMargin = 0.12f;
    static constexpr float kMinSpawnInterval = 0.35f;
    static constexpr float kMaxSpawnInterval = 1.4f;
    static constexpr float kMinTurnDelay = 0.15f;

    LightCycleColor trail_color_ = {0.15f, 0.7f, 1.0f};
    LightCycleColor head_color_ = {0.35f, 0.9f, 1.0f};
};

} // namespace animations
} // namespace when

