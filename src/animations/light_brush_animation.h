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
};

struct TrailPoint {
    float x = 0.0f;
    float y = 0.0f;
    float spawn_time = 0.0f;
};

class BrushStroke {
public:
    StrokeParticle head;
    std::deque<TrailPoint> trail;
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
    void create_or_resize_plane(notcurses* nc);
    void draw_frame(int frame_y, int frame_x, int frame_height, int frame_width);
    void render_point(float normalized_x,
                      float normalized_y,
                      std::uint8_t intensity,
                      int frame_y,
                      int frame_x,
                      int interior_height,
                      int interior_width);
    void spawn_particles(int count, bool heavy, float treble_envelope);

    ncplane* plane_ = nullptr;
    bool is_active_ = false;
    int z_index_ = 0;
    unsigned int plane_rows_ = 0;
    unsigned int plane_cols_ = 0;
    std::vector<BrushStroke> strokes_;
    float elapsed_time_ = 0.0f;
    std::mt19937 rng_;
};

} // namespace animations
} // namespace when

