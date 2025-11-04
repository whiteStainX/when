#pragma once

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
    void create_or_resize_plane(notcurses* nc);

    ncplane* plane_ = nullptr;
    int z_index_ = 0;
    bool is_active_ = true;

    unsigned int plane_rows_ = 0;
    unsigned int plane_cols_ = 0;
    int plane_origin_y_ = 0;
    int plane_origin_x_ = 0;
};

} // namespace animations
} // namespace when

