#pragma once

#include <string>
#include <vector>

#include "animation.h"

namespace why {
namespace animations {

class LightningWaveAnimation : public Animation {
public:
    LightningWaveAnimation();
    ~LightningWaveAnimation() override;

    void init(notcurses* nc, const AppConfig& config) override;
    void update(float delta_time,
                const AudioMetrics& metrics,
                const std::vector<float>& bands,
                float beat_strength) override;
    void render(notcurses* nc) override;

    void activate() override;
    void deactivate() override;

    bool is_active() const override;
    int get_z_index() const override { return z_index_; }
    ncplane* get_plane() const override { return plane_; }

    void bind_events(const AnimationConfig& config, events::EventBus& bus) override;

private:
    void configure_from_app(const AppConfig& config);
    void create_plane(notcurses* nc);
    void refresh_dimensions();
    void reset_history();
    void ensure_history_capacity();
    void shift_history(int steps);
    void blend_into_latest_column(const std::vector<float>& column);
    void fade_history(float delta_time);
    bool has_visible_history() const;
    bool bands_triggered(const std::vector<float>& bands) const;
    bool load_glyphs_from_file(const std::string& path);
    void ensure_glyphs_loaded();

    ncplane* plane_ = nullptr;
    unsigned int plane_rows_ = 0;
    unsigned int plane_cols_ = 0;
    int plane_origin_y_ = 0;
    int plane_origin_x_ = 0;

    int z_index_ = 0;
    bool is_active_ = false;

    int trigger_band_index_ = -1;
    float trigger_threshold_ = 0.05f;
    float persistence_duration_s_ = 1.5f;
    float fade_duration_s_ = 1.25f;
    float activation_timer_s_ = 0.0f;
    float scroll_speed_cols_per_s_ = 36.0f;
    float scroll_accumulator_ = 0.0f;
    bool pending_column_injection_ = false;

    std::vector<float> history_;
    std::vector<float> column_buffer_;

    std::vector<std::string> glyphs_;
    std::string glyphs_file_path_;
    bool glyphs_loaded_ = false;
};

} // namespace animations
} // namespace why

