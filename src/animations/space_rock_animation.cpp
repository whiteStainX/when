#include "space_rock_animation.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <limits>
#include <random>

#include "animation_event_utils.h"

namespace when {
namespace animations {
namespace {
constexpr float kFrameFillRatio = 0.8f;
constexpr float kMillisecondsToSeconds = 0.001f;
// Approximate width-to-height ratio of a terminal cell so rendered geometry can remain
// visually square even though cells are taller than they are wide.
constexpr float kCellWidthToHeightRatio = 0.5f;
constexpr std::uint8_t kDefaultSquareColor = 200u;
constexpr std::uint8_t kFrameForegroundColor = 255u;
constexpr std::uint8_t kFrameBackgroundColor = 20u;

int compute_spawn_count(int base_count, float strength_scale, float beat_strength) {
    const int clamped_base = std::max(base_count, 0);
    const float clamped_strength = std::max(beat_strength, 0.0f);
    const int scaled = static_cast<int>(std::round(clamped_strength * strength_scale));
    return clamped_base + std::max(scaled, 0);
}

int compute_max_squares(int floor, float scale, float bass_envelope) {
    const int clamped_floor = std::max(floor, 0);
    const float clamped_envelope = std::max(bass_envelope, 0.0f);
    const float scaled = clamped_envelope * std::max(scale, 0.0f);
    const int dynamic = static_cast<int>(std::round(scaled));
    return clamped_floor + std::max(dynamic, 0);
}

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * clamp01(t);
}

float compute_size_from_normalized(float normalized_value, float min_size, float max_size) {
    const float clamped_min = std::max(0.0f, min_size);
    const float clamped_max = std::max(clamped_min, max_size);
    return clamped_min + (clamped_max - clamped_min) * clamp01(normalized_value);
}

float compute_low_frequency_bias(const AudioFeatures& features) {
    const float centroid = clamp01(features.spectral_centroid);
    const float bass_component = clamp01(features.bass_envelope);
    const float treble_component = clamp01(features.treble_envelope);
    const float bass_weight = 0.5f * (bass_component + (1.0f - centroid));
    const float treble_weight = 0.5f * (treble_component + centroid);
    const float total = bass_weight + treble_weight;
    if (total <= std::numeric_limits<float>::epsilon()) {
        return 0.5f;
    }
    return clamp01(bass_weight / total);
}
} // namespace

SpaceRockAnimation::SpaceRockAnimation()
    : rng_(static_cast<std::mt19937::result_type>(
          std::chrono::steady_clock::now().time_since_epoch().count())) {}

SpaceRockAnimation::~SpaceRockAnimation() {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }
}

void SpaceRockAnimation::init(notcurses* nc, const AppConfig& config) {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }

    z_index_ = 0;
    is_active_ = true;
    params_ = Parameters{};

    load_parameters_from_config(config);

    plane_rows_ = 0;
    plane_cols_ = 0;
    squares_.clear();
    was_beat_detected_ = false;

    create_or_resize_plane(nc, config);
}

void SpaceRockAnimation::update(float delta_time,
                                const AudioMetrics& /*metrics*/,
                                const AudioFeatures& features) {
    if (!plane_) {
        return;
    }

    ncplane_dim_yx(plane_, &plane_rows_, &plane_cols_);

    const float dt = std::max(delta_time, 0.0f);
    const float target_size = compute_target_size_from_envelope(features.mid_envelope);
    const float jitter_magnitude =
        std::max(features.treble_energy, 0.0f) * std::max(params_.max_jitter, 0.0f);
    const bool beat_triggered = features.beat_detected && !was_beat_detected_;
    was_beat_detected_ = features.beat_detected;

    if (!squares_.empty()) {
        const float interpolation_rate = std::max(params_.size_interp_rate, 0.0f);
        const float interpolation_step = std::clamp(interpolation_rate * dt, 0.0f, 1.0f);
        const float beat_phase = clamp01(features.beat_phase);
        const float position_rate = std::max(params_.position_interp_rate, 0.0f);
        const float position_step_base = std::clamp(position_rate * dt, 0.0f, 1.0f);
        const float position_step = std::clamp(std::max(position_step_base, beat_phase), 0.0f, 1.0f);

        if (beat_triggered) {
            std::uniform_real_distribution<float> zero_to_one(0.0f, 1.0f);
            std::uniform_real_distribution<float> jitter_distribution(-jitter_magnitude, jitter_magnitude);
            const float low_frequency_bias = compute_low_frequency_bias(features);
            const float low_span = std::max(0.0f, params_.low_band_max_y - params_.low_band_min_y);
            const float high_span = std::max(0.0f, params_.high_band_max_y - params_.high_band_min_y);

            auto sample_low_band = [&]() {
                if (low_span <= 0.0f) {
                    return clamp01(params_.low_band_min_y);
                }
                return clamp01(params_.low_band_min_y + zero_to_one(rng_) * low_span);
            };
            auto sample_high_band = [&]() {
                if (high_span <= 0.0f) {
                    return clamp01(params_.high_band_min_y);
                }
                return clamp01(params_.high_band_min_y + zero_to_one(rng_) * high_span);
            };

            for (auto& square : squares_) {
                const float random_x = zero_to_one(rng_);
                const float low_sample = sample_low_band();
                const float high_sample = sample_high_band();
                const float biased_y = clamp01(lerp(high_sample, low_sample, low_frequency_bias));

                const float jitter_offset_x = jitter_magnitude > 0.0f ? jitter_distribution(rng_) : 0.0f;
                const float jitter_offset_y = jitter_magnitude > 0.0f ? jitter_distribution(rng_) : 0.0f;

                square.target_x = clamp01(random_x + jitter_offset_x);
                square.target_y = clamp01(biased_y + jitter_offset_y);
            }
        }

        for (auto& square : squares_) {
            square.age += dt * params_.square_decay_rate;
            square.target_size = std::clamp(target_size * square.size_multiplier,
                                            params_.min_size,
                                            params_.max_size);
            if (interpolation_rate <= 0.0f) {
                square.size = square.target_size;
            } else {
                square.size += (square.target_size - square.size) * interpolation_step;
            }
            square.size = std::clamp(square.size, params_.min_size, params_.max_size);

            if (position_step >= 1.0f) {
                square.x = square.target_x;
                square.y = square.target_y;
            } else if (position_step > 0.0f) {
                square.x += (square.target_x - square.x) * position_step;
                square.y += (square.target_y - square.y) * position_step;
            }

            square.x = clamp01(square.x);
            square.y = clamp01(square.y);
        }

        squares_.erase(std::remove_if(squares_.begin(),
                                      squares_.end(),
                                      [](const Square& square) {
                                          if (square.lifespan <= 0.0f) {
                                              return true;
                                          }
                                          return square.age >= square.lifespan;
                                      }),
                       squares_.end());
    }

    const int max_squares = compute_max_squares(params_.max_squares_floor,
                                                params_.max_squares_scale,
                                                features.bass_envelope);

    auto enforce_max_squares = [&](int target_max) {
        const int clamped_max = std::max(target_max, 0);
        if (static_cast<int>(squares_.size()) <= clamped_max) {
            return;
        }

        std::stable_sort(squares_.begin(), squares_.end(), [](const Square& a, const Square& b) {
            return a.age < b.age;
        });

        const std::size_t keep_count = static_cast<std::size_t>(clamped_max);
        if (keep_count < squares_.size()) {
            squares_.erase(squares_.begin() + keep_count, squares_.end());
        }
    };

    enforce_max_squares(max_squares);

    if (features.bass_beat) {
        const int spawn_count =
            compute_spawn_count(params_.spawn_base_count,
                                params_.spawn_strength_scale,
                                features.beat_strength);
        if (spawn_count > 0) {
            spawn_squares(spawn_count, features);
        }
    }

    enforce_max_squares(max_squares);
}

void SpaceRockAnimation::render(notcurses* /*nc*/) {
    if (!plane_ || !is_active_) {
        return;
    }

    ncplane_dim_yx(plane_, &plane_rows_, &plane_cols_);
    ncplane_erase(plane_);

    if (plane_rows_ == 0u || plane_cols_ == 0u) {
        return;
    }

    if (plane_rows_ < 2u || plane_cols_ < 2u) {
        return;
    }

    const float plane_physical_height = static_cast<float>(plane_rows_);
    const float plane_physical_width = static_cast<float>(plane_cols_) * kCellWidthToHeightRatio;
    const float max_physical_extent = std::min(plane_physical_height, plane_physical_width);
    const float target_physical_extent =
        std::max(1.0f, max_physical_extent * kFrameFillRatio);

    const int max_width_for_height =
        std::max(2, static_cast<int>(std::floor(static_cast<float>(plane_rows_) / kCellWidthToHeightRatio)));

    int frame_width = static_cast<int>(std::round(target_physical_extent / kCellWidthToHeightRatio));
    frame_width = std::clamp(frame_width, 2, std::min(static_cast<int>(plane_cols_), max_width_for_height));

    int frame_height = static_cast<int>(std::round(frame_width * kCellWidthToHeightRatio));
    frame_height = std::clamp(frame_height, 2, static_cast<int>(plane_rows_));

    frame_width = std::clamp(static_cast<int>(std::round(frame_height / kCellWidthToHeightRatio)),
                             2,
                             static_cast<int>(plane_cols_));
    frame_height = std::clamp(static_cast<int>(std::round(frame_width * kCellWidthToHeightRatio)),
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

    for (const auto& square : squares_) {
        render_square(square,
                      frame_y + 1,
                      frame_x + 1,
                      interior_height,
                      interior_width);
    }
}

void SpaceRockAnimation::activate() {
    is_active_ = true;
    was_beat_detected_ = false;
}

void SpaceRockAnimation::deactivate() {
    is_active_ = false;
    was_beat_detected_ = false;
    if (plane_) {
        ncplane_erase(plane_);
    }
}

void SpaceRockAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    bind_standard_frame_updates(this, config, bus);
}

void SpaceRockAnimation::load_parameters_from_config(const AppConfig& config) {
    for (const auto& anim_config : config.animations) {
        if (anim_config.type != "SpaceRock") {
            continue;
        }

        z_index_ = anim_config.z_index;
        is_active_ = anim_config.initially_active;
        params_.spawn_base_count = anim_config.space_rock_spawn_base_count;
        params_.spawn_strength_scale = anim_config.space_rock_spawn_strength_scale;
        params_.square_lifespan_s =
            std::max(0.0f, anim_config.space_rock_square_lifespan_ms * kMillisecondsToSeconds);
        params_.square_decay_rate = std::max(0.0f, anim_config.space_rock_square_decay_rate);
        params_.max_squares_floor = std::max(0, anim_config.space_rock_max_squares_floor);
        params_.max_squares_scale = std::max(0.0f, anim_config.space_rock_max_squares_scale);
        params_.min_size = std::max(0.0f, anim_config.space_rock_min_size);
        params_.max_size = std::max(params_.min_size, anim_config.space_rock_max_size);
        params_.mid_beat_size_multiplier =
            std::max(0.0f, anim_config.space_rock_mid_beat_size_multiplier);
        params_.bass_size_scale = std::max(0.0f, anim_config.space_rock_bass_size_scale);
        params_.treble_size_scale = std::max(0.0f, anim_config.space_rock_treble_size_scale);
        params_.low_band_min_y = clamp01(anim_config.space_rock_low_band_min_y);
        params_.low_band_max_y = clamp01(anim_config.space_rock_low_band_max_y);
        if (params_.low_band_max_y < params_.low_band_min_y) {
            std::swap(params_.low_band_min_y, params_.low_band_max_y);
        }
        params_.high_band_min_y = clamp01(anim_config.space_rock_high_band_min_y);
        params_.high_band_max_y = clamp01(anim_config.space_rock_high_band_max_y);
        if (params_.high_band_max_y < params_.high_band_min_y) {
            std::swap(params_.high_band_min_y, params_.high_band_max_y);
        }
        params_.size_interp_rate = std::max(0.0f, anim_config.space_rock_size_interp_rate);
        params_.max_jitter = std::max(0.0f, anim_config.space_rock_max_jitter);
        params_.position_interp_rate =
            std::max(0.0f, anim_config.space_rock_position_interp_rate);
        break;
    }
}

void SpaceRockAnimation::create_or_resize_plane(notcurses* nc, const AppConfig& config) {
    (void)config;
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
    }
}

void SpaceRockAnimation::draw_frame(int frame_y, int frame_x, int frame_height, int frame_width) {
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

void SpaceRockAnimation::render_square(const Square& square,
                                       int interior_y,
                                       int interior_x,
                                       int interior_height,
                                       int interior_width) {
    if (!plane_ || interior_height <= 0 || interior_width <= 0) {
        return;
    }

    const float clamped_x = std::clamp(square.x, 0.0f, 1.0f);
    const float clamped_y = std::clamp(square.y, 0.0f, 1.0f);
    const float clamped_size = std::clamp(square.size, 0.0f, 1.0f);

    const float interior_physical_height = static_cast<float>(interior_height);
    const float interior_physical_width = static_cast<float>(interior_width) * kCellWidthToHeightRatio;
    const float max_physical_extent = std::min(interior_physical_height, interior_physical_width);
    const float physical_extent = std::max(1.0f, clamped_size * max_physical_extent);

    int square_width = std::max(1, static_cast<int>(std::round(physical_extent / kCellWidthToHeightRatio)));
    square_width = std::min(square_width, interior_width);
    int square_height = std::max(1, static_cast<int>(std::round(square_width * kCellWidthToHeightRatio)));
    square_height = std::min(square_height, interior_height);

    // Adjust width again after clamping height to keep the perceived aspect ratio.
    square_width = std::min(
        std::max(1, static_cast<int>(std::round(static_cast<float>(square_height) / kCellWidthToHeightRatio))),
        interior_width);
    square_height = std::min(
        interior_height,
        std::max(1, static_cast<int>(std::round(static_cast<float>(square_width) * kCellWidthToHeightRatio))));

    const float horizontal_range_physical =
        std::max(0.0f, (static_cast<float>(interior_width) - 1.0f) * kCellWidthToHeightRatio);
    const int center_y = interior_y +
                         static_cast<int>(std::round(clamped_y * std::max(0, interior_height - 1)));
    const int center_x = interior_x + static_cast<int>(std::round(
                                          horizontal_range_physical > 0.0f
                                              ? (clamped_x * horizontal_range_physical /
                                                 kCellWidthToHeightRatio)
                                              : 0.0f));

    const int clamped_center_x = std::clamp(center_x, interior_x, interior_x + interior_width - 1);
    const int clamped_center_y = std::clamp(center_y, interior_y, interior_y + interior_height - 1);

    const int max_top = interior_y + interior_height - square_height;
    const int max_left = interior_x + interior_width - square_width;

    const int top = std::clamp(clamped_center_y - square_height / 2,
                               interior_y,
                               std::max(interior_y, max_top));
    const int left = std::clamp(clamped_center_x - square_width / 2,
                                interior_x,
                                std::max(interior_x, max_left));

    nccell fill = NCCELL_TRIVIAL_INITIALIZER;
    static constexpr uint32_t kFullBlock = 0x2588;  // Unicode full block character.
    if (nccell_load_ucs32(plane_, &fill, kFullBlock) <= 0) {
        return;
    }
    nccell_set_fg_rgb8(&fill,
                       static_cast<int>(square.color_r),
                       static_cast<int>(square.color_g),
                       static_cast<int>(square.color_b));
    nccell_set_bg_rgb8(&fill,
                       static_cast<int>(square.color_r),
                       static_cast<int>(square.color_g),
                       static_cast<int>(square.color_b));
    for (int row = 0; row < square_height; ++row) {
        const int draw_y = top + row;
        if (draw_y < interior_y || draw_y >= interior_y + interior_height) {
            continue;
        }
        for (int col = 0; col < square_width; ++col) {
            ncplane_putc_yx(plane_, draw_y, left + col, &fill);
        }
    }
    nccell_release(plane_, &fill);
}

float SpaceRockAnimation::compute_spawn_size(const AudioFeatures& features) const {
    const float base_size = compute_size_from_normalized(features.mid_energy_instantaneous,
                                                         params_.min_size,
                                                         params_.max_size);
    const float centroid = clamp01(features.spectral_centroid);
    const float bass_component = clamp01(features.bass_envelope);
    const float treble_component = clamp01(features.treble_envelope);
    const float bass_emphasis = clamp01(0.5f * (bass_component + (1.0f - centroid)));
    const float treble_emphasis = clamp01(0.5f * (treble_component + centroid));
    const float bass_scale =
        lerp(1.0f, std::max(params_.bass_size_scale, 0.0f), bass_emphasis);
    const float treble_scale =
        lerp(1.0f, std::max(params_.treble_size_scale, 0.0f), treble_emphasis);
    const float size_multiplier = features.mid_beat ? params_.mid_beat_size_multiplier : 1.0f;
    const float scaled_size =
        base_size * bass_scale * treble_scale * std::max(size_multiplier, 0.0f);
    return std::clamp(scaled_size, params_.min_size, params_.max_size);
}

float SpaceRockAnimation::compute_target_size_from_envelope(float mid_envelope) const {
    return compute_size_from_normalized(mid_envelope, params_.min_size, params_.max_size);
}

void SpaceRockAnimation::spawn_squares(int count, const AudioFeatures& features) {
    if (count <= 0) {
        return;
    }

    const float spawn_size = compute_spawn_size(features);
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    std::uniform_real_distribution<float> size_jitter_distribution(0.55f, 1.6f);
    const float low_frequency_bias = compute_low_frequency_bias(features);
    const float low_span = std::max(0.0f, params_.low_band_max_y - params_.low_band_min_y);
    const float high_span = std::max(0.0f, params_.high_band_max_y - params_.high_band_min_y);

    auto sample_low_band = [&]() {
        if (low_span <= 0.0f) {
            return clamp01(params_.low_band_min_y);
        }
        return clamp01(params_.low_band_min_y + distribution(rng_) * low_span);
    };
    auto sample_high_band = [&]() {
        if (high_span <= 0.0f) {
            return clamp01(params_.high_band_min_y);
        }
        return clamp01(params_.high_band_min_y + distribution(rng_) * high_span);
    };

    for (int i = 0; i < count; ++i) {
        Square square{};
        square.x = distribution(rng_);
        const float low_sample = sample_low_band();
        const float high_sample = sample_high_band();
        square.y = clamp01(lerp(high_sample, low_sample, low_frequency_bias));
        square.target_x = square.x;
        square.target_y = square.y;
        square.size_multiplier = size_jitter_distribution(rng_);
        const float initial_size =
            std::clamp(spawn_size * square.size_multiplier, params_.min_size, params_.max_size);
        square.size = initial_size;
        square.target_size = initial_size;
        square.age = 0.0f;
        square.lifespan = params_.square_lifespan_s;
        square.color_r = kDefaultSquareColor;
        square.color_g = kDefaultSquareColor;
        square.color_b = kDefaultSquareColor;
        squares_.push_back(square);
    }
}

} // namespace animations
} // namespace when

