#pragma once

#include <string>
#include <vector>

#include <notcurses/notcurses.h>

#include "animation.h"
#include "../config.h"

namespace why {
namespace animations {

class AsciiMatrixAnimation : public Animation {
public:
    AsciiMatrixAnimation();
    ~AsciiMatrixAnimation() override;

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
    ncplane* plane_ = nullptr;
    int z_index_ = 0;
    bool is_active_ = true;

    unsigned int plane_rows_ = 0;
    unsigned int plane_cols_ = 0;
    int plane_origin_y_ = 0;
    int plane_origin_x_ = 0;

    int matrix_rows_ = 16;
    int matrix_cols_ = 32;
    int configured_matrix_rows_ = 16;
    int configured_matrix_cols_ = 32;
    bool show_border_ = true;

    float beat_boost_ = 1.5f;
    float beat_threshold_ = 0.6f;

    std::vector<float> cell_values_;
    float latest_beat_strength_ = 0.0f;

    std::vector<std::string> glyphs_;
    std::string glyphs_file_path_;

    bool load_glyphs_from_file(const std::string& path);
    void ensure_dimensions_fit();
    void draw_border();
    void draw_matrix();
};

} // namespace animations
} // namespace why
