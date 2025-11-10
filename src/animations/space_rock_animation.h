#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include <notcurses/notcurses.h>

#include "animation.h"

namespace when {
namespace animations {

class SpaceRockAnimation : public Animation {
public:
    struct Square {
        float x = 0.0f;
        float y = 0.0f;
        float target_x = 0.0f;
        float target_y = 0.0f;
        float size = 0.0f;
        float target_size = 0.0f;
        float size_multiplier = 1.0f;
        float age = 0.0f;
        float lifespan = 0.0f;
        std::uint8_t color_r = 0u;
        std::uint8_t color_g = 0u;
        std::uint8_t color_b = 0u;
    };

    SpaceRockAnimation();
    ~SpaceRockAnimation() override;

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
    struct Parameters {
        int spawn_base_count = 3;
        float spawn_strength_scale = 4.0f;
        float square_lifespan_s = 1.6f;
        float square_decay_rate = 1.0f;
        int max_squares_floor = 12;
        float max_squares_scale = 36.0f;
        float min_size = 0.1f;
        float max_size = 0.35f;
        float mid_beat_size_multiplier = 1.35f;
        float bass_size_scale = 1.5f;
        float treble_size_scale = 0.75f;
        float low_band_min_y = 0.55f;
        float low_band_max_y = 0.95f;
        float high_band_min_y = 0.05f;
        float high_band_max_y = 0.45f;
        float size_interp_rate = 4.0f;
        float max_jitter = 0.6f;
        float position_interp_rate = 6.0f;
    };

    void load_parameters_from_config(const AppConfig& config);
    void create_or_resize_plane(notcurses* nc, const AppConfig& config);
    void draw_frame(int frame_y, int frame_x, int frame_height, int frame_width);
    void render_square(const Square& square,
                       int interior_y,
                       int interior_x,
                       int interior_height,
                       int interior_width);
    void spawn_squares(int count, const AudioFeatures& features);
    float compute_spawn_size(const AudioFeatures& features) const;
    float compute_target_size_from_envelope(float mid_envelope) const;

    ncplane* plane_ = nullptr;
    int z_index_ = 0;
    bool is_active_ = true;
    unsigned int plane_rows_ = 0;
    unsigned int plane_cols_ = 0;

    std::vector<Square> squares_;
    Parameters params_{};
    std::mt19937 rng_;
    bool was_beat_detected_ = false;
};

} // namespace animations
} // namespace when

