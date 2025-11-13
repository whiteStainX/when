#include "light_cycle_animation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

#include "animation_event_utils.h"

namespace when {
namespace animations {
namespace {
constexpr float kFrameFillRatio = 0.82f;
constexpr float kCellWidthToHeightRatio = 0.5f;
constexpr std::uint8_t kFrameForegroundColor = 240u;
constexpr std::uint8_t kFrameBackgroundColor = 18u;
constexpr std::uint8_t kCycleForegroundColor = 255u;
constexpr std::uint8_t kCycleBackgroundColor = 0u;
constexpr int kBrailleRowsPerCell = 4;
constexpr int kBrailleColsPerCell = 2;
constexpr float kThicknessRadiusScale = 1.35f;
constexpr std::size_t kMaxTrailSamples = 2048;
constexpr float kMinTurnSpacing = 0.12f;
}

LightCycleAnimation::LightCycleAnimation()
    : rng_(static_cast<std::mt19937::result_type>(
          std::chrono::steady_clock::now().time_since_epoch().count())) {}

LightCycleAnimation::~LightCycleAnimation() {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }
}

void LightCycleAnimation::init(notcurses* nc, const AppConfig& config) {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }

    is_active_ = true;
    z_index_ = 0;
    plane_rows_ = 0;
    plane_cols_ = 0;
    elapsed_time_ = 0.0f;
    time_since_last_turn_ = 0.0f;
    current_thickness_ = 1.0f;
    glow_intensity_ = 0.5f;
    trail_.clear();

    for (const auto& anim_config : config.animations) {
        if (anim_config.type == "LightCycle") {
            z_index_ = anim_config.z_index;
            is_active_ = anim_config.initially_active;
            base_speed_ = anim_config.light_cycle_base_speed;
            energy_speed_scale_ = anim_config.light_cycle_energy_speed_scale;
            tail_duration_s_ = std::max(0.1f, anim_config.light_cycle_tail_duration_s);
            tail_fade_power_ = std::max(0.1f, anim_config.light_cycle_tail_fade_power);
            turn_cooldown_s_ = std::max(0.01f, anim_config.light_cycle_turn_cooldown_s);
            beat_turn_threshold_ = std::clamp(anim_config.light_cycle_beat_turn_threshold, 0.0f, 1.0f);
            energy_turn_threshold_ = std::clamp(anim_config.light_cycle_energy_turn_threshold, 0.0f, 1.0f);
            thickness_min_ = std::max(0.05f, anim_config.light_cycle_thickness_min);
            thickness_max_ = std::max(thickness_min_, anim_config.light_cycle_thickness_max);
            thickness_smoothing_ = std::clamp(anim_config.light_cycle_thickness_smoothing, 0.0f, 1.0f);
            intensity_smoothing_ = std::clamp(anim_config.light_cycle_intensity_smoothing, 0.0f, 1.0f);
            break;
        }
    }

    create_or_resize_plane(nc);
    ensure_cycle_seeded();
}

void LightCycleAnimation::update(float delta_time,
                                 const AudioMetrics& /*metrics*/,
                                 const AudioFeatures& features) {
    if (!is_active_) {
        return;
    }

    elapsed_time_ += delta_time;
    time_since_last_turn_ += delta_time;
    last_features_ = features;

    const float clamped_energy = std::clamp(features.total_energy, 0.0f, 1.0f);
    const float clamped_instant = std::clamp(features.total_energy_instantaneous, 0.0f, 1.0f);
    const float clamped_bass = std::clamp(features.bass_envelope, 0.0f, 1.0f);

    const float target_thickness = thickness_min_ + (thickness_max_ - thickness_min_) * clamped_bass;
    current_thickness_ += (target_thickness - current_thickness_) * thickness_smoothing_;

    const float target_glow = 0.35f + clamped_energy * 0.65f;
    glow_intensity_ += (target_glow - glow_intensity_) * intensity_smoothing_;

    const float speed = base_speed_ + energy_speed_scale_ * clamped_energy;
    const float delta = speed * delta_time;

    if (orientation_ == Orientation::Horizontal) {
        head_x_ += static_cast<float>(direction_sign_) * delta;
        head_y_ = anchor_coordinate_;
    } else {
        head_y_ += static_cast<float>(direction_sign_) * delta;
        head_x_ = anchor_coordinate_;
    }

    clamp_head_to_bounds();

    append_trail_sample(current_thickness_, glow_intensity_);
    trim_trail();

    const bool beat_trigger = features.beat_detected && features.beat_strength >= beat_turn_threshold_;
    const bool energy_trigger = clamped_instant >= energy_turn_threshold_;

    if (beat_trigger || energy_trigger) {
        attempt_turn(features, false);
    }
}

void LightCycleAnimation::render(notcurses* /*nc*/) {
    if (!plane_ || !is_active_) {
        return;
    }

    ncplane_dim_yx(plane_, &plane_rows_, &plane_cols_);
    if (plane_rows_ == 0 || plane_cols_ == 0) {
        return;
    }

    ncplane_erase(plane_);

    const float plane_physical_height = static_cast<float>(plane_rows_);
    const float plane_physical_width = static_cast<float>(plane_cols_) * kCellWidthToHeightRatio;
    const float max_physical_extent = std::min(plane_physical_height, plane_physical_width);
    const float target_physical_extent = std::max(1.0f, max_physical_extent * kFrameFillRatio);

    const int max_width_for_height =
        std::max(2, static_cast<int>(std::floor(static_cast<float>(plane_rows_) / kCellWidthToHeightRatio)));

    int frame_width = static_cast<int>(std::round(target_physical_extent / kCellWidthToHeightRatio));
    frame_width = std::clamp(frame_width, 2, std::min(static_cast<int>(plane_cols_), max_width_for_height));

    int frame_height = static_cast<int>(std::round(frame_width * kCellWidthToHeightRatio));
    frame_height = std::clamp(frame_height, 2, static_cast<int>(plane_rows_));

    frame_width =
        std::clamp(static_cast<int>(std::round(static_cast<float>(frame_height) / kCellWidthToHeightRatio)),
                   2,
                   static_cast<int>(plane_cols_));
    frame_height =
        std::clamp(static_cast<int>(std::round(static_cast<float>(frame_width) * kCellWidthToHeightRatio)),
                   2,
                   static_cast<int>(plane_rows_));

    const int max_frame_y = static_cast<int>(plane_rows_) - frame_height;
    const int max_frame_x = static_cast<int>(plane_cols_) - frame_width;
    const int frame_y = std::max(0, max_frame_y / 2);
    const int frame_x = std::max(0, max_frame_x / 2);

    draw_frame(frame_y, frame_x, frame_height, frame_width);

    const int interior_height = std::max(0, frame_height - 2);
    const int interior_width = std::max(0, frame_width - 2);
    if (interior_height <= 0 || interior_width <= 0) {
        return;
    }

    const std::size_t cell_count =
        static_cast<std::size_t>(interior_height) * static_cast<std::size_t>(interior_width);
    braille_masks_.assign(cell_count, 0u);
    accumulation_buffer_.assign(cell_count, LightCycleColor{});

    bool any_samples = false;
    for (const auto& point : trail_) {
        const float age = std::max(0.0f, elapsed_time_ - point.spawn_time);
        if (age > tail_duration_s_) {
            continue;
        }

        const float brightness = compute_trail_brightness(age) * point.intensity;
        if (brightness <= std::numeric_limits<float>::epsilon()) {
            continue;
        }

        any_samples |= render_point(point.x,
                                    point.y,
                                    brightness,
                                    point.thickness,
                                    trail_color_,
                                    frame_y,
                                    frame_x,
                                    interior_height,
                                    interior_width);
    }

    const float head_brightness = std::clamp(glow_intensity_, 0.0f, 1.0f);
    any_samples |= render_point(head_x_,
                                head_y_,
                                head_brightness,
                                std::max(current_thickness_, thickness_min_),
                                head_color_,
                                frame_y,
                                frame_x,
                                interior_height,
                                interior_width);

    if (!any_samples) {
        const float clamped_x = std::clamp(head_x_, 0.0f, 1.0f);
        const float clamped_y = std::clamp(head_y_, 0.0f, 1.0f);
        const int y = frame_y + 1 +
                      static_cast<int>(std::round(clamped_y * std::max(0, interior_height - 1)));
        const int x = frame_x + 1 +
                      static_cast<int>(std::round(clamped_x * std::max(0, interior_width - 1)));

        nccell cell = NCCELL_TRIVIAL_INITIALIZER;
        if (nccell_load_ucs32(plane_, &cell, 0x2588u) > 0) {
            const float scaled_r = std::clamp(head_color_.r * head_brightness, 0.0f, 1.0f);
            const float scaled_g = std::clamp(head_color_.g * head_brightness, 0.0f, 1.0f);
            const float scaled_b = std::clamp(head_color_.b * head_brightness, 0.0f, 1.0f);
            nccell_set_fg_rgb8(&cell,
                               static_cast<int>(std::round(scaled_r * static_cast<float>(kCycleForegroundColor))),
                               static_cast<int>(std::round(scaled_g * static_cast<float>(kCycleForegroundColor))),
                               static_cast<int>(std::round(scaled_b * static_cast<float>(kCycleForegroundColor))));
            nccell_set_bg_rgb8(&cell,
                               static_cast<int>(kCycleBackgroundColor),
                               static_cast<int>(kCycleBackgroundColor),
                               static_cast<int>(kCycleBackgroundColor));
            ncplane_putc_yx(plane_, y, x, &cell);
            nccell_release(plane_, &cell);
        }
        return;
    }

    for (int row = 0; row < interior_height; ++row) {
        for (int col = 0; col < interior_width; ++col) {
            const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(interior_width) +
                                      static_cast<std::size_t>(col);
            if (index >= braille_masks_.size() || index >= accumulation_buffer_.size()) {
                continue;
            }

            const std::uint8_t mask = braille_masks_[index];
            if (mask == 0u) {
                continue;
            }

            const LightCycleColor& color = accumulation_buffer_[index];
            const float max_component = std::max({color.r, color.g, color.b});
            if (max_component <= 0.0f) {
                continue;
            }

            nccell cell = NCCELL_TRIVIAL_INITIALIZER;
            const uint32_t glyph = 0x2800u + static_cast<uint32_t>(mask);
            if (nccell_load_ucs32(plane_, &cell, glyph) <= 0) {
                continue;
            }

            const float clamped_r = std::clamp(color.r, 0.0f, 1.0f);
            const float clamped_g = std::clamp(color.g, 0.0f, 1.0f);
            const float clamped_b = std::clamp(color.b, 0.0f, 1.0f);
            nccell_set_fg_rgb8(&cell,
                               static_cast<int>(std::round(clamped_r * static_cast<float>(kCycleForegroundColor))),
                               static_cast<int>(std::round(clamped_g * static_cast<float>(kCycleForegroundColor))),
                               static_cast<int>(std::round(clamped_b * static_cast<float>(kCycleForegroundColor))));
            nccell_set_bg_rgb8(&cell,
                               static_cast<int>(kCycleBackgroundColor),
                               static_cast<int>(kCycleBackgroundColor),
                               static_cast<int>(kCycleBackgroundColor));

            ncplane_putc_yx(plane_, frame_y + 1 + row, frame_x + 1 + col, &cell);
            nccell_release(plane_, &cell);
        }
    }
}

void LightCycleAnimation::activate() {
    is_active_ = true;
}

void LightCycleAnimation::deactivate() {
    is_active_ = false;
    if (plane_) {
        ncplane_erase(plane_);
    }
}

bool LightCycleAnimation::is_active() const {
    return is_active_;
}

int LightCycleAnimation::get_z_index() const {
    return z_index_;
}

ncplane* LightCycleAnimation::get_plane() const {
    return plane_;
}

void LightCycleAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    bind_standard_frame_updates(this, config, bus);
}

void LightCycleAnimation::create_or_resize_plane(notcurses* nc) {
    if (!nc) {
        plane_ = nullptr;
        plane_rows_ = 0;
        plane_cols_ = 0;
        return;
    }

    ncplane* stdplane = notcurses_stdplane(nc);
    unsigned int std_rows = 0;
    unsigned int std_cols = 0;
    ncplane_dim_yx(stdplane, &std_rows, &std_cols);

    ncplane_options opts{};
    opts.rows = std_rows;
    opts.cols = std_cols;
    opts.y = 0;
    opts.x = 0;

    plane_ = ncplane_create(stdplane, &opts);
    if (plane_) {
        ncplane_dim_yx(plane_, &plane_rows_, &plane_cols_);
    } else {
        plane_rows_ = 0;
        plane_cols_ = 0;
    }
}

void LightCycleAnimation::draw_frame(int frame_y, int frame_x, int frame_height, int frame_width) {
    if (!plane_ || frame_height <= 0 || frame_width <= 0) {
        return;
    }

    nccell ul = NCCELL_TRIVIAL_INITIALIZER;
    nccell ur = NCCELL_TRIVIAL_INITIALIZER;
    nccell ll = NCCELL_TRIVIAL_INITIALIZER;
    nccell lr = NCCELL_TRIVIAL_INITIALIZER;
    nccell hl = NCCELL_TRIVIAL_INITIALIZER;
    nccell vl = NCCELL_TRIVIAL_INITIALIZER;

    auto cleanup_cells = [&]() {
        nccell_release(plane_, &ul);
        nccell_release(plane_, &ur);
        nccell_release(plane_, &ll);
        nccell_release(plane_, &lr);
        nccell_release(plane_, &hl);
        nccell_release(plane_, &vl);
    };

    auto load_cell = [&](nccell* cell, uint32_t glyph) -> bool {
        if (nccell_load_ucs32(plane_, cell, glyph) <= 0) {
            return false;
        }
        nccell_set_fg_rgb8(cell,
                           static_cast<int>(kFrameForegroundColor),
                           static_cast<int>(kFrameForegroundColor),
                           static_cast<int>(kFrameForegroundColor));
        nccell_set_bg_rgb8(cell,
                           static_cast<int>(kFrameBackgroundColor),
                           static_cast<int>(kFrameBackgroundColor),
                           static_cast<int>(kFrameBackgroundColor));
        return true;
    };

    const bool loaded = load_cell(&ul, 0x250Cu) &&  // ┌
                        load_cell(&ur, 0x2510u) &&  // ┐
                        load_cell(&ll, 0x2514u) &&  // └
                        load_cell(&lr, 0x2518u) &&  // ┘
                        load_cell(&hl, 0x2500u) &&  // ─
                        load_cell(&vl, 0x2502u);    // │
    if (!loaded) {
        cleanup_cells();
        return;
    }

    ncplane_cursor_move_yx(plane_, frame_y, frame_x);
    ncplane_box_sized(plane_, &ul, &ur, &ll, &lr, &hl, &vl, frame_height, frame_width, 0);

    cleanup_cells();
}

bool LightCycleAnimation::render_point(float normalized_x,
                                       float normalized_y,
                                       float brightness,
                                       float thickness,
                                       const LightCycleColor& color_scale,
                                       int frame_y,
                                       int frame_x,
                                       int interior_height,
                                       int interior_width) {
    (void)frame_y;
    (void)frame_x;
    if (!plane_ || interior_height <= 0 || interior_width <= 0 || brightness <= 0.0f || thickness <= 0.0f) {
        return false;
    }

    const float clamped_x = std::clamp(normalized_x, 0.0f, 1.0f);
    const float clamped_y = std::clamp(normalized_y, 0.0f, 1.0f);

    const int subcols = interior_width * kBrailleColsPerCell;
    const int subrows = interior_height * kBrailleRowsPerCell;
    if (subcols <= 0 || subrows <= 0) {
        return false;
    }

    const float center_subx = clamped_x * (static_cast<float>(subcols) - 1.0f);
    const float center_suby = clamped_y * (static_cast<float>(subrows) - 1.0f);
    const float radius = std::max(thickness * kThicknessRadiusScale, 0.1f);

    const int min_subx = std::max(0, static_cast<int>(std::floor(center_subx - radius)));
    const int max_subx = std::min(subcols - 1, static_cast<int>(std::ceil(center_subx + radius)));
    const int min_suby = std::max(0, static_cast<int>(std::floor(center_suby - radius)));
    const int max_suby = std::min(subrows - 1, static_cast<int>(std::ceil(center_suby + radius)));

    if (braille_masks_.empty() || accumulation_buffer_.empty()) {
        return false;
    }

    static constexpr std::uint8_t kBrailleMask[kBrailleRowsPerCell][kBrailleColsPerCell] = {
        {0x01u, 0x08u},
        {0x02u, 0x10u},
        {0x04u, 0x20u},
        {0x40u, 0x80u},
    };

    bool wrote_sample = false;
    for (int suby = min_suby; suby <= max_suby; ++suby) {
        const float dy = static_cast<float>(suby) - center_suby;
        for (int subx = min_subx; subx <= max_subx; ++subx) {
            const float dx = static_cast<float>(subx) - center_subx;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > radius) {
                continue;
            }

            const float normalized = 1.0f - std::clamp(distance / radius, 0.0f, 1.0f);
            const float sample_intensity = brightness * normalized * normalized;
            if (sample_intensity <= 0.0f) {
                continue;
            }

            const int cell_row = std::clamp(suby / kBrailleRowsPerCell, 0, interior_height - 1);
            const int cell_col = std::clamp(subx / kBrailleColsPerCell, 0, interior_width - 1);
            const int dot_row = suby % kBrailleRowsPerCell;
            const int dot_col = subx % kBrailleColsPerCell;
            const std::size_t index =
                static_cast<std::size_t>(cell_row) * static_cast<std::size_t>(interior_width) +
                static_cast<std::size_t>(cell_col);
            if (index >= braille_masks_.size() || index >= accumulation_buffer_.size()) {
                continue;
            }

            braille_masks_[index] |= kBrailleMask[dot_row][dot_col];
            LightCycleColor& color = accumulation_buffer_[index];
            color.r += sample_intensity * color_scale.r;
            color.g += sample_intensity * color_scale.g;
            color.b += sample_intensity * color_scale.b;
            wrote_sample = true;
        }
    }
    return wrote_sample;
}

float LightCycleAnimation::compute_trail_brightness(float age) const {
    if (tail_duration_s_ <= std::numeric_limits<float>::epsilon()) {
        return 0.0f;
    }

    const float normalized_age = std::clamp(age / tail_duration_s_, 0.0f, 1.0f);
    const float eased = 1.0f - normalized_age;
    return std::pow(eased, tail_fade_power_);
}

void LightCycleAnimation::ensure_cycle_seeded() {
    std::uniform_real_distribution<float> position_dist(0.1f, 0.9f);
    head_x_ = position_dist(rng_);
    head_y_ = position_dist(rng_);

    std::uniform_int_distribution<int> orientation_dist(0, 1);
    orientation_ = orientation_dist(rng_) == 0 ? Orientation::Horizontal : Orientation::Vertical;
    anchor_coordinate_ = orientation_ == Orientation::Horizontal ? head_y_ : head_x_;

    std::uniform_int_distribution<int> direction_dist(0, 1);
    direction_sign_ = direction_dist(rng_) == 0 ? -1 : 1;

    trail_.clear();
    append_trail_sample(current_thickness_, glow_intensity_);
}

void LightCycleAnimation::append_trail_sample(float thickness, float intensity) {
    if (trail_.size() >= kMaxTrailSamples) {
        trail_.pop_front();
    }
    trail_.push_back(LightCycleTrailPoint{head_x_, head_y_, elapsed_time_, thickness, intensity});
}

void LightCycleAnimation::trim_trail() {
    while (!trail_.empty()) {
        const float age = elapsed_time_ - trail_.front().spawn_time;
        if (age <= tail_duration_s_) {
            break;
        }
        trail_.pop_front();
    }
}

void LightCycleAnimation::attempt_turn(const AudioFeatures& features, bool forced) {
    if (!forced && time_since_last_turn_ < std::max(kMinTurnSpacing, turn_cooldown_s_)) {
        return;
    }

    const Orientation next_orientation =
        orientation_ == Orientation::Horizontal ? Orientation::Vertical : Orientation::Horizontal;
    orientation_ = next_orientation;
    anchor_coordinate_ = next_orientation == Orientation::Horizontal ? head_y_ : head_x_;
    direction_sign_ = choose_direction(next_orientation, features);
    if (direction_sign_ == 0) {
        direction_sign_ = 1;
    }
    time_since_last_turn_ = 0.0f;
}

int LightCycleAnimation::choose_direction(Orientation orientation, const AudioFeatures& features) {
    std::uniform_int_distribution<int> coin_flip(0, 1);

    int direction = 0;
    if (orientation == Orientation::Horizontal) {
        const float tonal_bias = std::clamp(features.treble_energy - features.bass_energy, -1.0f, 1.0f);
        direction = tonal_bias >= 0.0f ? 1 : -1;
        if (std::abs(tonal_bias) < 0.1f) {
            direction = coin_flip(rng_) == 0 ? -1 : 1;
        }

        if (head_x_ <= 0.1f) {
            direction = 1;
        } else if (head_x_ >= 0.9f) {
            direction = -1;
        }

        if (direction > 0 && head_x_ >= 0.92f) {
            direction = -1;
        } else if (direction < 0 && head_x_ <= 0.08f) {
            direction = 1;
        }

        if (direction == 0) {
            direction = coin_flip(rng_) == 0 ? -1 : 1;
        }
    } else {
        const float bass_bias = std::clamp(features.bass_energy - features.mid_energy, -1.0f, 1.0f);
        direction = bass_bias >= 0.0f ? 1 : -1;
        if (std::abs(bass_bias) < 0.1f) {
            direction = coin_flip(rng_) == 0 ? -1 : 1;
        }

        if (head_y_ <= 0.1f) {
            direction = 1;
        } else if (head_y_ >= 0.9f) {
            direction = -1;
        }

        if (direction > 0 && head_y_ >= 0.92f) {
            direction = -1;
        } else if (direction < 0 && head_y_ <= 0.08f) {
            direction = 1;
        }

        if (direction == 0) {
            direction = coin_flip(rng_) == 0 ? -1 : 1;
        }
    }

    return direction;
}

void LightCycleAnimation::clamp_head_to_bounds() {
    const float min_pos = 0.0f;
    const float max_pos = 1.0f;

    if (orientation_ == Orientation::Horizontal) {
        if (head_x_ <= min_pos) {
            head_x_ = min_pos;
            attempt_turn(last_features_, true);
        } else if (head_x_ >= max_pos) {
            head_x_ = max_pos;
            attempt_turn(last_features_, true);
        }
    } else {
        if (head_y_ <= min_pos) {
            head_y_ = min_pos;
            attempt_turn(last_features_, true);
        } else if (head_y_ >= max_pos) {
            head_y_ = max_pos;
            attempt_turn(last_features_, true);
        }
    }
}

} // namespace animations
} // namespace when

