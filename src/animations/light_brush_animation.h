#pragma once

#include <cstdint>
#include <deque>
#include <random>
#include <vector>

#include "animation.h"

namespace when {
namespace animations {

struct StrokeParticle {
    float x = 0.5f;
    float y = 0.5f;
    float vx = 0.0f;
    float vy = 0.0f;
    float age = 0.0f;
    float lifespan = 1.0f;
    float thickness = 1.0f;
};

struct TrailPoint {
    float x = 0.0f;
    float y = 0.0f;
    float spawn_time = 0.0f;
    float thickness = 1.0f;
};

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

class BrushStroke {
public:
    StrokeParticle head;
    std::deque<TrailPoint> trail;
    float base_thickness = 1.0f;
    float thickness = 1.0f;
};

struct LightBrushParameters {
    float frame_fill_ratio = 0.82f;
    float cell_width_to_height_ratio = 0.5f;
    int frame_foreground_color = 240;
    int frame_background_color = 18;
    int particle_foreground_color = 255;
    int particle_background_color = 0;
    float heavy_velocity_min = 0.08f;
    float heavy_velocity_max = 0.18f;
    float light_velocity_min = 0.18f;
    float light_velocity_max = 0.35f;
    float heavy_lifespan_min = 1.1f;
    float heavy_lifespan_max = 3.0f;
    float light_lifespan_min = 0.6f;
    float light_lifespan_max = 2.0f;
    float speed_scale_min = 0.6f;
    float speed_scale_max = 1.8f;
    float turbulence_base_strength = 0.45f;
    float attractor_radius = 0.42f;
    float seeking_strength = 1.25f;
    float thickness_min = 0.35f;
    float thickness_max = 3.6f;
    float thickness_smoothing = 0.16f;
    float thickness_radius_scale = 1.35f;
    float beat_weight_base = 0.5f;
    float beat_weight_scale = 1.5f;
    float tonal_weight_base = 0.6f;
    float tonal_weight_scale = 0.8f;
    float heavy_thickness_bias = 1.25f;
    float light_thickness_bias = 0.9f;
    float base_thickness_base = 0.5f;
    float base_thickness_beat_scale = 1.6f;
    float base_thickness_tonal_base = 0.6f;
    float base_thickness_tonal_scale = 0.8f;
};

class LightBrushAnimation : public Animation {
public:
    LightBrushAnimation();
    ~LightBrushAnimation() override;

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
    void apply_animation_config(const AnimationConfig& config);
    void create_or_resize_plane(notcurses* nc);
    void draw_frame(int frame_y, int frame_x, int frame_height, int frame_width);
    bool render_point(float normalized_x,
                      float normalized_y,
                      float brightness,
                      float thickness,
                      int frame_y,
                      int frame_x,
                      int interior_height,
                      int interior_width);
    void spawn_particles(int count,
                         bool heavy,
                         float treble_envelope,
                         float beat_strength,
                         float spectral_flatness);
    float compute_brightness(float age, float lifespan) const;

    ncplane* plane_ = nullptr;
    bool is_active_ = false;
    int z_index_ = 0;
    unsigned int plane_rows_ = 0;
    unsigned int plane_cols_ = 0;
    std::vector<BrushStroke> strokes_;
    float elapsed_time_ = 0.0f;
    std::mt19937 rng_;
    std::vector<std::uint8_t> braille_masks_;
    std::vector<Color> accumulation_buffer_;
    LightBrushParameters parameters_;
};

} // namespace animations
} // namespace when

