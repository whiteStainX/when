#include "cyber_rain_animation.h"
#include "animation_event_utils.h"
#include "glyph_utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace why {
namespace animations {

namespace {
constexpr const char* kDefaultGlyphFilePath = "assets/cyber_rain.txt";
constexpr const char* kDefaultGlyphs = R"(|/\\-_=+*<>[]{}())";
constexpr float kDefaultHighFreqThreshold = 0.55f;
constexpr float kDefaultPersistenceDuration = 0.6f;
constexpr float kDefaultFadeDuration = 0.9f;
constexpr float kDefaultBaseScanSpeed = 10.0f;
constexpr float kDefaultScanSpeedBoost = 14.0f;
constexpr float kDefaultDropRateBase = 1.5f;
constexpr float kDefaultDropRateBoost = 6.5f;
constexpr int kDefaultDropLengthMin = 4;
constexpr int kDefaultDropLengthMax = 9;
constexpr float kDefaultDropSpeedMin = 10.0f;
constexpr float kDefaultDropSpeedMax = 22.0f;
constexpr float kDefaultActivationSmoothing = 0.12f;
constexpr float kDefaultRainAngleDegrees = 0.0f;
constexpr float kMaxRainAngleDegrees = 80.0f;
constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

} // namespace

CyberRainAnimation::CyberRainAnimation()
    : glyphs_(parse_glyphs(kDefaultGlyphs)),
      glyphs_file_path_(kDefaultGlyphFilePath),
      rng_(static_cast<std::mt19937::result_type>(
          std::chrono::steady_clock::now().time_since_epoch().count())),
      unit_dist_(0.0f, 1.0f) {}

CyberRainAnimation::~CyberRainAnimation() {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }
}

void CyberRainAnimation::init(notcurses* nc, const AppConfig& config) {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }

    glyphs_file_path_ = kDefaultGlyphFilePath;
    glyphs_ = parse_glyphs(kDefaultGlyphs);
    z_index_ = 0;
    is_active_ = true;
    trigger_band_index_ = -1;
    high_freq_threshold_ = kDefaultHighFreqThreshold;
    persistence_duration_s_ = kDefaultPersistenceDuration;
    fade_duration_s_ = kDefaultFadeDuration;
    base_scan_speed_cols_per_s_ = kDefaultBaseScanSpeed;
    scan_speed_boost_cols_per_s_ = kDefaultScanSpeedBoost;
    drop_rate_base_per_s_ = kDefaultDropRateBase;
    drop_rate_boost_per_s_ = kDefaultDropRateBoost;
    drop_length_min_ = kDefaultDropLengthMin;
    drop_length_max_ = kDefaultDropLengthMax;
    drop_speed_min_rows_per_s_ = kDefaultDropSpeedMin;
    drop_speed_max_rows_per_s_ = kDefaultDropSpeedMax;
    activation_smoothing_s_ = kDefaultActivationSmoothing;
    rain_angle_degrees_ = kDefaultRainAngleDegrees;
    horizontal_slope_ = std::tan(rain_angle_degrees_ * kDegreesToRadians);

    plane_rows_ = 0;
    plane_cols_ = 0;
    plane_origin_y_ = 0;
    plane_origin_x_ = 0;

    ncplane* stdplane = notcurses_stdplane(nc);
    unsigned int std_rows = 0;
    unsigned int std_cols = 0;
    ncplane_dim_yx(stdplane, &std_rows, &std_cols);

    int desired_y = 0;
    int desired_x = 0;
    int desired_rows = static_cast<int>(std_rows);
    int desired_cols = static_cast<int>(std_cols);
    bool has_custom_rows = false;
    bool has_custom_cols = false;

    for (const auto& anim_config : config.animations) {
        if (anim_config.type == "CyberRain") {
            z_index_ = anim_config.z_index;
            is_active_ = anim_config.initially_active;
            trigger_band_index_ = anim_config.trigger_band_index;
            if (anim_config.trigger_threshold > 0.0f) {
                high_freq_threshold_ = anim_config.trigger_threshold;
            }
            if (!anim_config.glyphs_file_path.empty()) {
                glyphs_file_path_ = anim_config.glyphs_file_path;
            } else if (!anim_config.text_file_path.empty()) {
                glyphs_file_path_ = anim_config.text_file_path;
            }
            if (anim_config.display_duration_s > 0.0f) {
                persistence_duration_s_ = anim_config.display_duration_s;
            }
            if (anim_config.fade_duration_s > 0.0f) {
                fade_duration_s_ = anim_config.fade_duration_s;
            }
            if (anim_config.type_speed_words_per_s > 0.0f) {
                base_scan_speed_cols_per_s_ = anim_config.type_speed_words_per_s;
            }
            if (anim_config.trigger_cooldown_s > 0.0f) {
                scan_speed_boost_cols_per_s_ = anim_config.trigger_cooldown_s * 10.0f;
            }
            const float clamped_angle = std::clamp(anim_config.rain_angle_degrees,
                                                  -kMaxRainAngleDegrees,
                                                  kMaxRainAngleDegrees);
            rain_angle_degrees_ = clamped_angle;
            if (anim_config.plane_y) {
                desired_y = *anim_config.plane_y;
            }
            if (anim_config.plane_x) {
                desired_x = *anim_config.plane_x;
            }
            if (anim_config.plane_rows) {
                if (*anim_config.plane_rows > 0) {
                    desired_rows = *anim_config.plane_rows;
                    has_custom_rows = true;
                }
            }
            if (anim_config.plane_cols) {
                if (*anim_config.plane_cols > 0) {
                    desired_cols = *anim_config.plane_cols;
                    has_custom_cols = true;
                }
            }
            break;
        }
    }

    horizontal_slope_ = std::tan(rain_angle_degrees_ * kDegreesToRadians);

    if (!load_glyphs_from_file(glyphs_file_path_)) {
        if (glyphs_file_path_ != kDefaultGlyphFilePath) {
            if (!load_glyphs_from_file(kDefaultGlyphFilePath)) {
                glyphs_ = parse_glyphs(kDefaultGlyphs);
            }
        } else {
            glyphs_ = parse_glyphs(kDefaultGlyphs);
        }
    }

    if (std_rows > 0) {
        plane_origin_y_ = std::clamp(desired_y, 0, static_cast<int>(std_rows) - 1);
    } else {
        plane_origin_y_ = 0;
    }

    if (std_cols > 0) {
        plane_origin_x_ = std::clamp(desired_x, 0, static_cast<int>(std_cols) - 1);
    } else {
        plane_origin_x_ = 0;
    }

    const unsigned int available_rows = (std_rows > static_cast<unsigned int>(plane_origin_y_))
                                            ? std_rows - static_cast<unsigned int>(plane_origin_y_)
                                            : 0u;
    const unsigned int available_cols = (std_cols > static_cast<unsigned int>(plane_origin_x_))
                                            ? std_cols - static_cast<unsigned int>(plane_origin_x_)
                                            : 0u;

    if (has_custom_rows && available_rows > 0u) {
        plane_rows_ = static_cast<unsigned int>(std::clamp(desired_rows, 1, static_cast<int>(available_rows)));
    } else {
        plane_rows_ = available_rows;
    }

    if (has_custom_cols && available_cols > 0u) {
        plane_cols_ = static_cast<unsigned int>(std::clamp(desired_cols, 1, static_cast<int>(available_cols)));
    } else {
        plane_cols_ = available_cols;
    }

    if (plane_rows_ == 0u) {
        plane_rows_ = std_rows;
        plane_origin_y_ = 0;
    }
    if (plane_cols_ == 0u) {
        plane_cols_ = std_cols;
        plane_origin_x_ = 0;
    }

    if (plane_rows_ == 0u || plane_cols_ == 0u) {
        plane_ = nullptr;
        cells_.clear();
        active_drops_.clear();
        return;
    }

    ncplane_options opts{};
    opts.rows = plane_rows_;
    opts.cols = plane_cols_;
    opts.y = plane_origin_y_;
    opts.x = plane_origin_x_;
    plane_ = ncplane_create(stdplane, &opts);

    if (plane_) {
        ncplane_dim_yx(plane_, &plane_rows_, &plane_cols_);
        cells_.assign(static_cast<std::size_t>(plane_rows_) * plane_cols_, {});
    } else {
        cells_.clear();
    }

    active_drops_.clear();
    scan_position_ = 0.0f;
    scan_direction_right_ = true;
    persistence_timer_ = 0.0f;
    activation_level_ = 0.0f;
    drop_spawn_accumulator_ = 0.0f;
}

void CyberRainAnimation::activate() {
    is_active_ = true;
    persistence_timer_ = 0.0f;
}

void CyberRainAnimation::deactivate() {
    is_active_ = false;
}

void CyberRainAnimation::update(float delta_time,
                                const AudioMetrics& /*metrics*/,
                                const std::vector<float>& bands,
                                float /*beat_strength*/) {
    if (!plane_) return;

    refresh_dimensions();

    if (!cells_.empty()) {
        const float decay = (fade_duration_s_ > 0.0f)
                                ? std::clamp(delta_time / fade_duration_s_, 0.0f, 1.0f)
                                : 1.0f;
        for (auto& cell : cells_) {
            cell.intensity = std::max(0.0f, cell.intensity - decay);
            if (cell.intensity <= 0.01f) {
                cell.glyph.clear();
            }
        }
    }

    if (is_active_) {
        const float high_freq = compute_high_frequency_energy(bands);
        const bool triggered = high_freq >= high_freq_threshold_;
        if (triggered) {
            persistence_timer_ = persistence_duration_s_;
        } else if (persistence_timer_ > 0.0f) {
            persistence_timer_ = std::max(0.0f, persistence_timer_ - delta_time);
        }

        float target_activation = 0.0f;
        if (triggered) {
            const float denominator = std::max(1e-3f, 1.0f - high_freq_threshold_);
            target_activation = std::clamp((high_freq - high_freq_threshold_) / denominator, 0.0f, 1.0f);
        }
        if (persistence_timer_ > 0.0f && persistence_duration_s_ > 0.0f) {
            const float persistence_factor = std::clamp(persistence_timer_ / persistence_duration_s_, 0.0f, 1.0f);
            target_activation = std::max(target_activation, persistence_factor);
        }

        target_activation = std::clamp(target_activation, 0.0f, 1.0f);

        if (activation_level_ < target_activation) {
            if (activation_smoothing_s_ > 0.0f) {
                activation_level_ = std::min(target_activation,
                                             activation_level_ + delta_time / activation_smoothing_s_);
            } else {
                activation_level_ = target_activation;
            }
        } else {
            if (activation_smoothing_s_ > 0.0f) {
                activation_level_ = std::max(target_activation,
                                             activation_level_ - delta_time / activation_smoothing_s_);
            } else {
                activation_level_ = target_activation;
            }
        }

        activation_level_ = std::clamp(activation_level_, 0.0f, 1.0f);

        if (activation_level_ > 0.0f && plane_cols_ > 0u) {
            const float speed = base_scan_speed_cols_per_s_ + scan_speed_boost_cols_per_s_ * activation_level_;
            if (scan_direction_right_) {
                scan_position_ += speed * delta_time;
                if (scan_position_ >= static_cast<float>(plane_cols_ - 1)) {
                    scan_position_ = static_cast<float>(plane_cols_ - 1);
                    scan_direction_right_ = false;
                }
            } else {
                scan_position_ -= speed * delta_time;
                if (scan_position_ <= 0.0f) {
                    scan_position_ = 0.0f;
                    scan_direction_right_ = true;
                }
            }

            int column = static_cast<int>(std::lround(scan_position_));
            column = std::clamp(column, 0, static_cast<int>(plane_cols_) - 1);
            spawn_rain_column(column, activation_level_, delta_time);
        }
    } else {
        persistence_timer_ = 0.0f;
        activation_level_ = 0.0f;
    }

    update_drops(delta_time);
    remove_finished_drops();
}

void CyberRainAnimation::render(notcurses* /*nc*/) {
    if (!plane_) return;

    refresh_dimensions();

    ncplane_erase(plane_);
    ncplane_set_bg_rgb8(plane_, 0, 0, 0);

    if (plane_rows_ == 0u || plane_cols_ == 0u) {
        return;
    }

    for (unsigned int row = 0; row < plane_rows_; ++row) {
        for (unsigned int col = 0; col < plane_cols_; ++col) {
            const std::size_t index = static_cast<std::size_t>(row) * plane_cols_ + col;
            if (index >= cells_.size()) {
                continue;
            }
            const CellState& cell = cells_[index];
            if (cell.intensity <= 0.01f) {
                continue;
            }

            const float clamped_intensity = std::clamp(cell.intensity, 0.0f, 1.0f);
            const float glow = clamped_intensity;

            const uint8_t r = static_cast<uint8_t>(std::clamp(20.0f + 60.0f * glow, 0.0f, 255.0f));
            const uint8_t g = static_cast<uint8_t>(std::clamp(120.0f + 135.0f * glow, 0.0f, 255.0f));
            const uint8_t b = static_cast<uint8_t>(std::clamp(40.0f + 110.0f * glow, 0.0f, 255.0f));

            ncplane_set_fg_rgb8(plane_, r, g, b);
            const char* glyph = cell.glyph.empty() ? "|" : cell.glyph.c_str();
            ncplane_putstr_yx(plane_, row, col, glyph);
        }
    }
}

void CyberRainAnimation::refresh_dimensions() {
    if (!plane_) return;

    unsigned int rows = 0;
    unsigned int cols = 0;
    ncplane_dim_yx(plane_, &rows, &cols);

    if (rows != plane_rows_ || cols != plane_cols_) {
        plane_rows_ = rows;
        plane_cols_ = cols;
        cells_.assign(static_cast<std::size_t>(plane_rows_) * plane_cols_, {});
        if (plane_cols_ > 0u) {
            scan_position_ = std::clamp(scan_position_, 0.0f, static_cast<float>(plane_cols_ - 1));
        } else {
            scan_position_ = 0.0f;
        }
        for (auto& drop : active_drops_) {
            if (plane_cols_ > 0u) {
                const float max_col = static_cast<float>(plane_cols_ - 1);
                drop.head_column = std::clamp(drop.head_column, 0.0f, max_col);
            } else {
                drop.head_column = 0.0f;
            }
        }
    }
}

bool CyberRainAnimation::load_glyphs_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    auto parsed = parse_glyphs(buffer.str());
    if (parsed.empty()) {
        return false;
    }

    glyphs_ = std::move(parsed);
    return true;
}

void CyberRainAnimation::spawn_rain_column(int column, float activation, float delta_time) {
    if (plane_rows_ == 0u || plane_cols_ == 0u) {
        return;
    }
    if (column < 0 || column >= static_cast<int>(plane_cols_)) {
        return;
    }

    const float drop_rate = std::clamp(drop_rate_base_per_s_ + drop_rate_boost_per_s_ * activation,
                                       0.0f,
                                       100.0f);
    drop_spawn_accumulator_ = std::min(drop_spawn_accumulator_ + drop_rate * delta_time, 8.0f);

    int spawn_count = static_cast<int>(drop_spawn_accumulator_);
    drop_spawn_accumulator_ -= static_cast<float>(spawn_count);

    if (spawn_count < 8) {
        const float fractional = drop_spawn_accumulator_;
        if (fractional > 0.0f && unit_dist_(rng_) < fractional) {
            ++spawn_count;
            drop_spawn_accumulator_ = 0.0f;
        }
    }

    spawn_count = std::clamp(spawn_count, 0, 8);

    std::uniform_int_distribution<int> length_dist(drop_length_min_, drop_length_max_);
    std::uniform_real_distribution<float> speed_dist(drop_speed_min_rows_per_s_, drop_speed_max_rows_per_s_);

    for (int i = 0; i < spawn_count; ++i) {
        ActiveDrop drop;
        drop.head_column = static_cast<float>(column);
        drop.length = std::max(1, length_dist(rng_));
        drop.speed_rows_per_s = speed_dist(rng_);
        drop.horizontal_speed_cols_per_s = drop.speed_rows_per_s * horizontal_slope_;
        drop.strength = std::clamp(0.6f + 0.4f * activation + 0.2f * unit_dist_(rng_), 0.0f, 1.0f);
        drop.head_row = -static_cast<float>(drop.length) * unit_dist_(rng_);
        if (!glyphs_.empty()) {
            std::uniform_int_distribution<std::size_t> glyph_dist(0, glyphs_.size() - 1);
            drop.glyph = glyphs_[glyph_dist(rng_)];
        } else {
            drop.glyph = "|";
        }
        active_drops_.push_back(std::move(drop));
    }
}

void CyberRainAnimation::update_drops(float delta_time) {
    if (plane_rows_ == 0u || plane_cols_ == 0u) {
        return;
    }
    if (active_drops_.empty()) {
        return;
    }

    const float slope = horizontal_slope_;

    for (auto& drop : active_drops_) {
        drop.head_row += drop.speed_rows_per_s * delta_time;
        drop.head_column += drop.horizontal_speed_cols_per_s * delta_time;
        const int head_index = static_cast<int>(std::floor(drop.head_row));
        const int tail_index = head_index - drop.length + 1;

        for (int row = tail_index; row <= head_index; ++row) {
            if (row < 0 || row >= static_cast<int>(plane_rows_)) {
                continue;
            }
            const float offset = static_cast<float>(head_index - row);
            const float span = static_cast<float>(std::max(1, drop.length));
            float relative = 1.0f - (offset / span);
            relative = std::clamp(relative, 0.0f, 1.0f);
            const float intensity = std::clamp(drop.strength * relative, 0.0f, 1.0f);

            const float column_position = drop.head_column - slope * offset;
            const int column_index = static_cast<int>(std::lround(column_position));
            if (column_index < 0 || column_index >= static_cast<int>(plane_cols_)) {
                continue;
            }

            const std::size_t index = static_cast<std::size_t>(row) * plane_cols_
                                      + static_cast<unsigned int>(column_index);
            if (index >= cells_.size()) {
                continue;
            }

            CellState& cell = cells_[index];
            if (intensity > cell.intensity) {
                cell.intensity = intensity;
                cell.glyph = drop.glyph;
            }
        }
    }
}

void CyberRainAnimation::remove_finished_drops() {
    if (active_drops_.empty()) {
        return;
    }

    const float row_limit = static_cast<float>(plane_rows_);
    const float col_limit = static_cast<float>(plane_cols_);
    const float slope = horizontal_slope_;
    active_drops_.erase(std::remove_if(active_drops_.begin(),
                                       active_drops_.end(),
                                       [&](const ActiveDrop& drop) {
                                           const bool past_bottom = drop.head_row - drop.length >= row_limit;
                                           if (plane_cols_ == 0u) {
                                               return past_bottom;
                                           }
                                           const float head_col = drop.head_column;
                                           const float tail_col = drop.head_column - slope * static_cast<float>(drop.length - 1);
                                           const float min_col = std::min(head_col, tail_col);
                                           const float max_col = std::max(head_col, tail_col);
                                           const bool off_left = max_col < 0.0f;
                                           const bool off_right = min_col >= col_limit;
                                           return past_bottom || off_left || off_right;
                                       }),
                        active_drops_.end());
}

float CyberRainAnimation::compute_high_frequency_energy(const std::vector<float>& bands) const {
    if (bands.empty()) {
        return 0.0f;
    }

    if (trigger_band_index_ >= 0 && trigger_band_index_ < static_cast<int>(bands.size())) {
        return bands[static_cast<std::size_t>(trigger_band_index_)];
    }

    const std::size_t band_count = bands.size();
    const std::size_t start_index = band_count >= 3 ? (band_count * 2) / 3 : 0;
    if (start_index >= band_count) {
        return bands.back();
    }

    float sum = 0.0f;
    std::size_t count = 0;
    for (std::size_t i = start_index; i < band_count; ++i) {
        sum += bands[i];
        ++count;
    }

    if (count == 0) {
        return 0.0f;
    }

    return sum / static_cast<float>(count);
}

bool CyberRainAnimation::has_visible_cells() const {
    if (!active_drops_.empty()) {
        return true;
    }
    for (const auto& cell : cells_) {
        if (cell.intensity > 0.01f) {
            return true;
        }
    }
    return false;
}

void CyberRainAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    bind_standard_frame_updates(this, config, bus);
}

} // namespace animations
} // namespace why
