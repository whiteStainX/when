#include "lightning_wave_animation.h"

#include "animation_event_utils.h"
#include "glyph_utils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>

namespace why {
namespace animations {

namespace {
constexpr const char* kDefaultGlyphFilePath = "assets/lightning_wave.txt";
constexpr const char* kDefaultGlyphs = " .:•●";
constexpr float kMinTriggerThreshold = 0.001f;
constexpr float kMaxTriggerThreshold = 1.0f;
}

LightningWaveAnimation::LightningWaveAnimation()
    : glyphs_(parse_glyphs(kDefaultGlyphs)),
      glyphs_file_path_(kDefaultGlyphFilePath) {}

LightningWaveAnimation::~LightningWaveAnimation() {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }
}

void LightningWaveAnimation::init(notcurses* nc, const AppConfig& config) {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }

    glyphs_file_path_ = kDefaultGlyphFilePath;
    glyphs_ = parse_glyphs(kDefaultGlyphs);
    glyphs_loaded_ = true;

    plane_rows_ = 0u;
    plane_cols_ = 0u;
    plane_origin_y_ = 0;
    plane_origin_x_ = 0;

    z_index_ = 0;
    is_active_ = false;

    trigger_band_index_ = -1;
    trigger_threshold_ = 0.05f;
    persistence_duration_s_ = 1.5f;
    fade_duration_s_ = 1.25f;
    activation_timer_s_ = 0.0f;
    scroll_speed_cols_per_s_ = 36.0f;
    scroll_accumulator_ = 0.0f;
    pending_column_injection_ = false;

    history_.clear();
    column_buffer_.clear();

    configure_from_app(config);
    create_plane(nc);
    refresh_dimensions();
    reset_history();
}

void LightningWaveAnimation::configure_from_app(const AppConfig& config) {
    for (const auto& anim_config : config.animations) {
        if (anim_config.type == "LightningWave") {
            z_index_ = anim_config.z_index;
            is_active_ = anim_config.initially_active;
            trigger_band_index_ = anim_config.trigger_band_index;

            if (anim_config.trigger_threshold > 0.0f) {
                trigger_threshold_ = std::clamp(anim_config.trigger_threshold,
                                                kMinTriggerThreshold,
                                                kMaxTriggerThreshold);
            }

            if (anim_config.display_duration_s > 0.0f) {
                persistence_duration_s_ = anim_config.display_duration_s;
            }
            if (anim_config.fade_duration_s > 0.0f) {
                fade_duration_s_ = anim_config.fade_duration_s;
            }
            if (anim_config.wave_speed_cols_per_s > 0.0f) {
                scroll_speed_cols_per_s_ = anim_config.wave_speed_cols_per_s;
            }

            if (!anim_config.glyphs_file_path.empty()) {
                glyphs_file_path_ = anim_config.glyphs_file_path;
                glyphs_loaded_ = false;
            }

            if (anim_config.plane_y) {
                plane_origin_y_ = *anim_config.plane_y;
            }
            if (anim_config.plane_x) {
                plane_origin_x_ = *anim_config.plane_x;
            }
            if (anim_config.plane_rows) {
                plane_rows_ = static_cast<unsigned int>(std::max(1, *anim_config.plane_rows));
            }
            if (anim_config.plane_cols) {
                plane_cols_ = static_cast<unsigned int>(std::max(1, *anim_config.plane_cols));
            }

            break;
        }
    }
}

void LightningWaveAnimation::create_plane(notcurses* nc) {
    if (!nc) {
        return;
    }

    ncplane* stdplane = notcurses_stdplane(nc);
    unsigned int std_rows = 0;
    unsigned int std_cols = 0;
    ncplane_dim_yx(stdplane, &std_rows, &std_cols);

    if (plane_rows_ == 0u || plane_rows_ > std_rows) {
        plane_rows_ = std_rows;
    }
    if (plane_cols_ == 0u || plane_cols_ > std_cols) {
        plane_cols_ = std_cols;
    }

    plane_origin_y_ = std::clamp(plane_origin_y_, 0, static_cast<int>(std_rows));
    plane_origin_x_ = std::clamp(plane_origin_x_, 0, static_cast<int>(std_cols));

    ncplane_options opts{};
    opts.rows = plane_rows_;
    opts.cols = plane_cols_;
    opts.y = plane_origin_y_;
    opts.x = plane_origin_x_;

    plane_ = ncplane_create(stdplane, &opts);
}

void LightningWaveAnimation::refresh_dimensions() {
    if (!plane_) {
        return;
    }

    unsigned int rows = 0;
    unsigned int cols = 0;
    ncplane_dim_yx(plane_, &rows, &cols);
    if (rows != plane_rows_ || cols != plane_cols_) {
        plane_rows_ = rows;
        plane_cols_ = cols;
        ensure_history_capacity();
    }
}

void LightningWaveAnimation::reset_history() {
    ensure_history_capacity();
    std::fill(history_.begin(), history_.end(), 0.0f);
    std::fill(column_buffer_.begin(), column_buffer_.end(), 0.0f);
    activation_timer_s_ = 0.0f;
    scroll_accumulator_ = 0.0f;
}

void LightningWaveAnimation::ensure_history_capacity() {
    const std::size_t stride = static_cast<std::size_t>(plane_rows_);
    const std::size_t total = stride * static_cast<std::size_t>(plane_cols_);
    if (history_.size() != total) {
        history_.assign(total, 0.0f);
    }
    if (column_buffer_.size() != stride) {
        column_buffer_.assign(stride, 0.0f);
    }
}

void LightningWaveAnimation::shift_history(int steps) {
    if (steps <= 0 || history_.empty()) {
        return;
    }

    const int clamped_steps = std::min(steps, static_cast<int>(plane_cols_));
    const std::size_t stride = static_cast<std::size_t>(plane_rows_);
    const std::size_t shift_elements = static_cast<std::size_t>(clamped_steps) * stride;

    if (shift_elements >= history_.size()) {
        std::fill(history_.begin(), history_.end(), 0.0f);
        return;
    }

    const std::size_t keep_elements = history_.size() - shift_elements;
    std::move(history_.begin() + static_cast<std::ptrdiff_t>(shift_elements),
              history_.end(),
              history_.begin());
    std::fill(history_.begin() + static_cast<std::ptrdiff_t>(keep_elements),
              history_.end(),
              0.0f);
}

void LightningWaveAnimation::blend_into_latest_column(const std::vector<float>& column) {
    if (plane_rows_ == 0u || plane_cols_ == 0u || history_.empty()) {
        return;
    }

    const std::size_t stride = static_cast<std::size_t>(plane_rows_);
    const std::size_t offset = (static_cast<std::size_t>(plane_cols_) - 1u) * stride;
    for (std::size_t row = 0; row < stride; ++row) {
        const float value = std::clamp(column[row], 0.0f, 1.0f);
        float& cell = history_[offset + row];
        cell = std::max(cell, value);
    }
}

void LightningWaveAnimation::fade_history(float delta_time) {
    if (history_.empty()) {
        return;
    }

    const float decay = (fade_duration_s_ > 0.0f)
                            ? std::clamp(delta_time / fade_duration_s_, 0.0f, 1.0f)
                            : 1.0f;
    const float retain = 1.0f - decay;
    for (float& value : history_) {
        value = std::max(0.0f, value * retain);
    }
}

bool LightningWaveAnimation::has_visible_history() const {
    return std::any_of(history_.begin(), history_.end(), [](float v) { return v > 0.01f; });
}

bool LightningWaveAnimation::bands_triggered(const std::vector<float>& bands) const {
    if (bands.empty()) {
        return false;
    }

    if (trigger_band_index_ >= 0 &&
        trigger_band_index_ < static_cast<int>(bands.size())) {
        return bands[static_cast<std::size_t>(trigger_band_index_)] >= trigger_threshold_;
    }

    return std::any_of(bands.begin(), bands.end(), [this](float value) {
        return value >= trigger_threshold_;
    });
}

bool LightningWaveAnimation::load_glyphs_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string contents = buffer.str();
    auto glyphs = parse_glyphs(contents);
    if (glyphs.empty()) {
        return false;
    }

    glyphs_ = std::move(glyphs);
    return true;
}

void LightningWaveAnimation::ensure_glyphs_loaded() {
    if (glyphs_loaded_) {
        return;
    }

    if (load_glyphs_from_file(glyphs_file_path_)) {
        glyphs_loaded_ = true;
        return;
    }

    if (glyphs_file_path_ != kDefaultGlyphFilePath) {
        if (load_glyphs_from_file(kDefaultGlyphFilePath)) {
            glyphs_loaded_ = true;
            return;
        }
    }

    glyphs_ = parse_glyphs(kDefaultGlyphs);
    glyphs_loaded_ = true;
}

void LightningWaveAnimation::activate() {
    if (is_active_) {
        return;
    }

    is_active_ = true;
    reset_history();
}

void LightningWaveAnimation::deactivate() {
    if (!is_active_) {
        return;
    }

    is_active_ = false;
    activation_timer_s_ = 0.0f;
    pending_column_injection_ = false;
    reset_history();
    if (plane_) {
        ncplane_erase(plane_);
    }
}

bool LightningWaveAnimation::is_active() const {
    return is_active_;
}

void LightningWaveAnimation::update(float delta_time,
                                    const AudioMetrics& /*metrics*/,
                                    const std::vector<float>& bands,
                                    float /*beat_strength*/) {
    if (!plane_ || plane_rows_ == 0u || plane_cols_ == 0u) {
        return;
    }

    refresh_dimensions();
    fade_history(delta_time);

    if (activation_timer_s_ > 0.0f) {
        activation_timer_s_ = std::max(0.0f, activation_timer_s_ - delta_time);
    }

    scroll_accumulator_ += scroll_speed_cols_per_s_ * delta_time;
    int shift_steps = static_cast<int>(scroll_accumulator_);
    if (shift_steps > 0) {
        shift_history(shift_steps);
        scroll_accumulator_ -= static_cast<float>(shift_steps);
    }

    if (pending_column_injection_ && !bands.empty()) {
        const float max_band = *std::max_element(bands.begin(), bands.end());
        if (max_band > 0.0f) {
            const std::size_t band_count = bands.size();
            const std::size_t rows = static_cast<std::size_t>(std::max(1u, plane_rows_));
            const float inv_max = 1.0f / max_band;

            for (std::size_t row = 0; row < rows; ++row) {
                float normalized_row = (rows > 1)
                                           ? static_cast<float>(row) / static_cast<float>(rows - 1u)
                                           : 0.0f;
                float band_position = normalized_row * static_cast<float>(band_count - 1u);
                std::size_t low_index = static_cast<std::size_t>(std::floor(band_position));
                std::size_t high_index = std::min(low_index + 1u, band_count - 1u);
                float blend = band_position - static_cast<float>(low_index);
                float band_value = bands[low_index] * (1.0f - blend) + bands[high_index] * blend;

                float scaled = std::clamp(band_value * inv_max, 0.0f, 1.0f);
                column_buffer_[row] = std::sqrt(scaled);
            }

            blend_into_latest_column(column_buffer_);
        }
    }

    pending_column_injection_ = false;
}

void LightningWaveAnimation::render(notcurses* /*nc*/) {
    if (!plane_) {
        return;
    }

    if (!is_active_ && activation_timer_s_ <= 0.0f && !has_visible_history()) {
        if (plane_) {
            ncplane_erase(plane_);
        }
        return;
    }

    ncplane_erase(plane_);

    if (plane_rows_ == 0u || plane_cols_ == 0u) {
        return;
    }

    ensure_glyphs_loaded();
    const std::size_t glyph_count = glyphs_.size();
    if (glyph_count == 0u) {
        return;
    }

    ncplane_set_fg_rgb8(plane_, 210, 220, 255);

    const std::size_t stride = static_cast<std::size_t>(plane_rows_);
    for (unsigned int col = 0; col < plane_cols_; ++col) {
        const std::size_t offset = static_cast<std::size_t>(col) * stride;
        for (unsigned int row = 0; row < plane_rows_; ++row) {
            const float intensity = std::clamp(history_[offset + row], 0.0f, 1.0f);
            if (intensity <= 0.0f) {
                continue;
            }

            const float graded = std::clamp(intensity, 0.0f, 1.0f);
            std::size_t glyph_index = static_cast<std::size_t>(std::floor(graded * static_cast<float>(glyph_count - 1u)));
            if (glyph_index >= glyph_count) {
                glyph_index = glyph_count - 1u;
            }

            const std::string& glyph = glyphs_[glyph_index];
            const int draw_row = static_cast<int>(plane_rows_ - 1u - row);
            ncplane_putstr_yx(plane_, draw_row, static_cast<int>(col), glyph.c_str());
        }
    }
}

void LightningWaveAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    AnimationConfig captured_config = config;
    captured_config.trigger_threshold = trigger_threshold_;

    auto handle = bus.subscribe<events::FrameUpdateEvent>(
        [this, captured_config](const events::FrameUpdateEvent& event) mutable {
            const bool meets_beat = evaluate_beat_condition(captured_config, event.beat_strength);
            const bool triggered = meets_beat && bands_triggered(event.bands);

            pending_column_injection_ = triggered;

            if (triggered) {
                activation_timer_s_ = persistence_duration_s_;
                if (!is_active_) {
                    activate();
                }
            }

            if (is_active_) {
                update(event.delta_time, event.metrics, event.bands, event.beat_strength);
            }

            if (!triggered && is_active_ && activation_timer_s_ <= 0.0f && !has_visible_history()) {
                deactivate();
            }
        });

    track_subscription(std::move(handle));
}

} // namespace animations
} // namespace why
