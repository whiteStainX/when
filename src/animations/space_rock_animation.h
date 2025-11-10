#pragma once

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
        float size = 0.0f;
        float velocity_x = 0.0f;
        float velocity_y = 0.0f;
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
    };

    void load_parameters_from_config(const AppConfig& config);
    void create_or_resize_plane(notcurses* nc, const AppConfig& config);
    void draw_frame(int frame_y, int frame_x, int frame_height, int frame_width);
    void render_square(const Square& square,
                       int interior_y,
                       int interior_x,
                       int interior_height,
                       int interior_width);
    void spawn_squares(int count);

    ncplane* plane_ = nullptr;
    int z_index_ = 0;
    bool is_active_ = true;
    unsigned int plane_rows_ = 0;
    unsigned int plane_cols_ = 0;

    std::vector<Square> squares_;
    Parameters params_{};
    std::mt19937 rng_;
};

} // namespace animations
} // namespace when

