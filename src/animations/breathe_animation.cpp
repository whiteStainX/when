#include "breathe_animation.h"
#include "animation_event_utils.h"
#include "glyph_utils.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace why {
namespace animations {

namespace {
constexpr const char* kDefaultGlyphFilePath = "assets/breathe_animation.txt";
constexpr const char* kDefaultGlyphs = " .oO@#";
constexpr float kTwoPi = 6.28318530717958647692f;

std::vector<std::string> parse_glyphs_or_default(const std::string& source) {
    auto glyphs = parse_glyphs(source);
    if (glyphs.empty()) {
        glyphs.emplace_back("#");
    }
    return glyphs;
}

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float lerp(float from, float to, float alpha) {
    return from + (to - from) * alpha;
}

} // namespace

BreatheAnimation::BreatheAnimation()
    : glyphs_(parse_glyphs_or_default(kDefaultGlyphs)),
      glyphs_file_path_(kDefaultGlyphFilePath),
      rng_(std::random_device{}()) {}

BreatheAnimation::~BreatheAnimation() {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }
}

void BreatheAnimation::init(notcurses* nc, const AppConfig& config) {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }

    glyphs_file_path_ = kDefaultGlyphFilePath;
    glyphs_ = parse_glyphs_or_default(kDefaultGlyphs);
    glyphs_loaded_ = false;

    plane_rows_ = 0u;
    plane_cols_ = 0u;
    plane_origin_y_ = 0;
    plane_origin_x_ = 0;

    z_index_ = 0;
    is_active_ = true;

    points_ = 64;
    min_radius_ = 6.0f;
    max_radius_ = 14.0f;
    audio_radius_influence_ = 10.0f;
    smoothing_time_s_ = 0.18f;
    noise_amount_ = 0.3f;
    rotation_speed_rad_s_ = 0.35f;
    vertical_scale_ = 0.55f;
    base_pulse_hz_ = 0.35f;
    audio_pulse_weight_ = 0.65f;

    audio_band_index_ = -1;
    rms_weight_ = 1.0f;
    beat_weight_ = 0.6f;
    band_weight_ = 0.5f;

    persistence_duration_s_ = 0.8f;
    fade_duration_s_ = 1.2f;

    smoothed_energy_ = 0.0f;
    breathing_phase_ = 0.0f;
    rotation_angle_ = 0.0f;

    configure_from_app(config);
    create_plane(nc);
    refresh_dimensions();
    reset_buffers();
    update_noise_table();
}

void BreatheAnimation::configure_from_app(const AppConfig& config) {
    for (const auto& anim_config : config.animations) {
        if (anim_config.type == "Breathe") {
            z_index_ = anim_config.z_index;
            is_active_ = anim_config.initially_active;
            if (!anim_config.glyphs_file_path.empty()) {
                if (glyphs_file_path_ != anim_config.glyphs_file_path) {
                    glyphs_file_path_ = anim_config.glyphs_file_path;
                    glyphs_loaded_ = false;
                }
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

            if (anim_config.breathe_points > 0) {
                points_ = anim_config.breathe_points;
            }
            if (anim_config.breathe_min_radius > 0.0f) {
                min_radius_ = anim_config.breathe_min_radius;
            }
            if (anim_config.breathe_max_radius > anim_config.breathe_min_radius) {
                max_radius_ = anim_config.breathe_max_radius;
            }
            if (anim_config.breathe_audio_radius_influence >= 0.0f) {
                audio_radius_influence_ = anim_config.breathe_audio_radius_influence;
            }
            if (anim_config.breathe_smoothing_s >= 0.0f) {
                smoothing_time_s_ = anim_config.breathe_smoothing_s;
            }
            if (anim_config.breathe_noise_amount >= 0.0f) {
                noise_amount_ = anim_config.breathe_noise_amount;
            }
            if (anim_config.breathe_rotation_speed >= 0.0f) {
                rotation_speed_rad_s_ = anim_config.breathe_rotation_speed;
            }
            if (anim_config.breathe_vertical_scale > 0.0f) {
                vertical_scale_ = anim_config.breathe_vertical_scale;
            }
            if (anim_config.breathe_base_pulse_hz > 0.0f) {
                base_pulse_hz_ = anim_config.breathe_base_pulse_hz;
            }
            if (anim_config.breathe_audio_pulse_weight >= 0.0f) {
                audio_pulse_weight_ = anim_config.breathe_audio_pulse_weight;
            }
            if (anim_config.breathe_band_index >= -1) {
                audio_band_index_ = anim_config.breathe_band_index;
            }
            if (anim_config.breathe_rms_weight >= 0.0f) {
                rms_weight_ = anim_config.breathe_rms_weight;
            }
            if (anim_config.breathe_beat_weight >= 0.0f) {
                beat_weight_ = anim_config.breathe_beat_weight;
            }
            if (anim_config.breathe_band_weight >= 0.0f) {
                band_weight_ = anim_config.breathe_band_weight;
            }
            if (anim_config.display_duration_s > 0.0f) {
                persistence_duration_s_ = anim_config.display_duration_s;
            }
            if (anim_config.fade_duration_s > 0.0f) {
                fade_duration_s_ = anim_config.fade_duration_s;
            }
            break;
        }
    }
}

void BreatheAnimation::create_plane(notcurses* nc) {
    if (!nc) {
        return;
    }

    ncplane* stdplane = notcurses_stdplane(nc);
    unsigned int std_rows = 0u;
    unsigned int std_cols = 0u;
    ncplane_dim_yx(stdplane, &std_rows, &std_cols);

    unsigned int rows = plane_rows_;
    unsigned int cols = plane_cols_;

    if (rows == 0u || rows > std_rows) {
        rows = std_rows;
    }
    if (cols == 0u || cols > std_cols) {
        cols = std_cols;
    }

    plane_origin_y_ = std::clamp(plane_origin_y_, 0, static_cast<int>(std_rows));
    plane_origin_x_ = std::clamp(plane_origin_x_, 0, static_cast<int>(std_cols));

    ncplane_options opts{};
    opts.rows = rows;
    opts.cols = cols;
    opts.y = plane_origin_y_;
    opts.x = plane_origin_x_;

    plane_ = ncplane_create(stdplane, &opts);
    if (plane_) {
        plane_rows_ = rows;
        plane_cols_ = cols;
        ncplane_set_scrolling(plane_, true);
    }
}

void BreatheAnimation::refresh_dimensions() {
    if (!plane_) {
        return;
    }

    unsigned int rows = 0u;
    unsigned int cols = 0u;
    ncplane_dim_yx(plane_, &rows, &cols);
    if (rows != plane_rows_ || cols != plane_cols_) {
        plane_rows_ = rows;
        plane_cols_ = cols;
        reset_buffers();
    }
}

void BreatheAnimation::reset_buffers() {
    if (plane_rows_ == 0u || plane_cols_ == 0u) {
        cell_intensities_.clear();
        return;
    }

    cell_intensities_.assign(static_cast<std::size_t>(plane_rows_) * static_cast<std::size_t>(plane_cols_),
                             0.0f);
}

bool BreatheAnimation::load_glyphs_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    glyphs_ = parse_glyphs_or_default(content);
    return !glyphs_.empty();
}

void BreatheAnimation::ensure_glyphs_loaded() {
    if (glyphs_loaded_) {
        return;
    }

    if (!glyphs_file_path_.empty() && load_glyphs_from_file(glyphs_file_path_)) {
        glyphs_loaded_ = true;
        return;
    }

    if (glyphs_file_path_ != kDefaultGlyphFilePath) {
        if (load_glyphs_from_file(kDefaultGlyphFilePath)) {
            glyphs_loaded_ = true;
            return;
        }
    }

    glyphs_ = parse_glyphs_or_default(kDefaultGlyphs);
    glyphs_loaded_ = true;
}

void BreatheAnimation::update_noise_table() {
    if (points_ <= 0) {
        noise_phases_.clear();
        return;
    }

    if (static_cast<int>(noise_phases_.size()) == points_) {
        return;
    }

    std::uniform_real_distribution<float> dist(0.0f, kTwoPi);
    noise_phases_.resize(points_);
    for (float& phase : noise_phases_) {
        phase = dist(rng_);
    }
}

void BreatheAnimation::decay_intensities(float delta_time) {
    if (cell_intensities_.empty()) {
        return;
    }

    if (persistence_duration_s_ <= 0.0f) {
        std::fill(cell_intensities_.begin(), cell_intensities_.end(), 0.0f);
        return;
    }

    const float decay = delta_time / persistence_duration_s_;
    for (float& value : cell_intensities_) {
        value = std::max(0.0f, value - decay);
    }
}

void BreatheAnimation::draw_shape(float brightness) {
    if (!plane_ || plane_rows_ == 0u || plane_cols_ == 0u || points_ <= 1) {
        return;
    }

    const float center_y = static_cast<float>(plane_rows_) / 2.0f;
    const float center_x = static_cast<float>(plane_cols_) / 2.0f;

    std::vector<std::pair<int, int>> points;
    points.reserve(static_cast<std::size_t>(points_));

    for (int i = 0; i < points_; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(points_);
        const float angle = kTwoPi * t + rotation_angle_;
        float noise_value = 0.0f;
        if (i < static_cast<int>(noise_phases_.size())) {
            noise_value = std::sin(breathing_phase_ * 0.5f + noise_phases_[static_cast<std::size_t>(i)]);
        }
        const float radius = std::max(0.0f,
                                      (min_radius_ +
                                       (max_radius_ - min_radius_) * (0.5f + 0.5f * std::sin(breathing_phase_)) +
                                       audio_radius_influence_ * smoothed_energy_)) *
                             (1.0f + noise_amount_ * noise_value);
        const float px = center_x + radius * std::cos(angle);
        const float py = center_y + radius * std::sin(angle) * vertical_scale_;

        const int ix = static_cast<int>(std::round(px));
        const int iy = static_cast<int>(std::round(py));
        points.emplace_back(ix, iy);
    }

    if (points.empty()) {
        return;
    }

    auto previous = points.back();
    for (const auto& current : points) {
        draw_line(previous.first, previous.second, current.first, current.second, brightness);
        previous = current;
    }
}

void BreatheAnimation::draw_line(int x0, int y0, int x1, int y1, float intensity) {
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        stamp_cell(x0, y0, intensity);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void BreatheAnimation::stamp_cell(int x, int y, float intensity) {
    if (x < 0 || y < 0) {
        return;
    }
    if (plane_cols_ == 0u || plane_rows_ == 0u) {
        return;
    }
    if (x >= static_cast<int>(plane_cols_) || y >= static_cast<int>(plane_rows_)) {
        return;
    }

    const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(plane_cols_) +
                            static_cast<std::size_t>(x);
    float& cell = cell_intensities_[idx];
    cell = std::max(cell, clamp01(intensity));

    const float falloff = 0.6f * clamp01(intensity);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            int nx = x + dx;
            int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= static_cast<int>(plane_cols_) || ny >= static_cast<int>(plane_rows_)) {
                continue;
            }
            const std::size_t nidx = static_cast<std::size_t>(ny) * static_cast<std::size_t>(plane_cols_) +
                                     static_cast<std::size_t>(nx);
            float& neighbor = cell_intensities_[nidx];
            neighbor = std::max(neighbor, falloff);
        }
    }
}

float BreatheAnimation::compute_audio_energy(const AudioMetrics& metrics,
                                             const std::vector<float>& bands,
                                             float beat_strength) const {
    float energy = 0.0f;
    if (metrics.active) {
        energy += metrics.rms * rms_weight_;
    }
    energy += beat_strength * beat_weight_;
    if (audio_band_index_ >= 0 && audio_band_index_ < static_cast<int>(bands.size())) {
        energy += bands[static_cast<std::size_t>(audio_band_index_)] * band_weight_;
    }
    return std::max(0.0f, energy);
}

void BreatheAnimation::update(float delta_time,
                              const AudioMetrics& metrics,
                              const std::vector<float>& bands,
                              float beat_strength) {
    if (!is_active_ || !plane_) {
        return;
    }

    refresh_dimensions();
    ensure_glyphs_loaded();
    update_noise_table();

    decay_intensities(delta_time);

    const float audio_energy = compute_audio_energy(metrics, bands, beat_strength);
    const float smoothing = smoothing_time_s_ > 0.0f ? clamp01(delta_time / (smoothing_time_s_ + delta_time)) : 1.0f;
    smoothed_energy_ = lerp(smoothed_energy_, audio_energy, smoothing);

    const float pulse_rate = base_pulse_hz_ + audio_pulse_weight_ * smoothed_energy_;
    breathing_phase_ += kTwoPi * pulse_rate * delta_time;
    rotation_angle_ += rotation_speed_rad_s_ * delta_time;

    if (breathing_phase_ > kTwoPi) {
        breathing_phase_ = std::fmod(breathing_phase_, kTwoPi);
    }
    if (rotation_angle_ > kTwoPi) {
        rotation_angle_ = std::fmod(rotation_angle_, kTwoPi);
    }

    const float brightness = clamp01(0.35f + 0.65f * smoothed_energy_);
    draw_shape(brightness);
}

void BreatheAnimation::render(notcurses* /*nc*/) {
    if (!plane_) {
        return;
    }

    ensure_glyphs_loaded();

    ncplane_erase(plane_);

    if (glyphs_.empty()) {
        return;
    }

    const std::size_t glyph_count = glyphs_.size();

    for (unsigned int y = 0; y < plane_rows_; ++y) {
        for (unsigned int x = 0; x < plane_cols_; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(plane_cols_) +
                                    static_cast<std::size_t>(x);
            const float value = clamp01(cell_intensities_[idx]);
            if (value <= 0.0f) {
                continue;
            }
            const std::size_t glyph_index = static_cast<std::size_t>(std::floor(value * static_cast<float>(glyph_count - 1)));
            const std::string& glyph = glyphs_[glyph_index];
            ncplane_putstr_yx(plane_, static_cast<int>(y), static_cast<int>(x), glyph.c_str());
        }
    }
}

void BreatheAnimation::activate() {
    is_active_ = true;
    smoothed_energy_ = 0.0f;
}

void BreatheAnimation::deactivate() {
    is_active_ = false;
    if (plane_) {
        ncplane_erase(plane_);
    }
    std::fill(cell_intensities_.begin(), cell_intensities_.end(), 0.0f);
}

void BreatheAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    bind_standard_frame_updates(this, config, bus);
}

} // namespace animations
} // namespace why
