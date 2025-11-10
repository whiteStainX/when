#include "space_rock_animation.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <random>
#include <string>

#include "animation_event_utils.h"

namespace when {
namespace animations {
namespace {
constexpr float kFrameFillRatio = 0.8f;
constexpr float kDefaultSquareSize = 0.18f;
constexpr float kMillisecondsToSeconds = 0.001f;

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
    if (!squares_.empty()) {
        for (auto& square : squares_) {
            square.age += dt * params_.square_decay_rate;
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
            spawn_squares(spawn_count);
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

    const int frame_height = std::max(2, static_cast<int>(std::round(plane_rows_ * kFrameFillRatio)));
    const int frame_width = std::max(2, static_cast<int>(std::round(plane_cols_ * kFrameFillRatio)));

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
}

void SpaceRockAnimation::deactivate() {
    is_active_ = false;
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

    nccell_load_char(plane_, &ul, '+');
    nccell_load_char(plane_, &ur, '+');
    nccell_load_char(plane_, &ll, '+');
    nccell_load_char(plane_, &lr, '+');
    nccell_load_char(plane_, &hl, '-');
    nccell_load_char(plane_, &vl, '|');

    ncplane_cursor_move_yx(plane_, frame_y, frame_x);
    ncplane_box_sized(plane_, &ul, &ur, &ll, &lr, &hl, &vl, frame_height, frame_width, 0);

    nccell_release(plane_, &ul);
    nccell_release(plane_, &ur);
    nccell_release(plane_, &ll);
    nccell_release(plane_, &lr);
    nccell_release(plane_, &hl);
    nccell_release(plane_, &vl);
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

    const int base_extent = std::max(1, static_cast<int>(std::round(clamped_size * std::min(interior_height, interior_width))));
    const int square_height = base_extent;
    const int square_width = base_extent;

    const int center_y = interior_y + static_cast<int>(std::round(clamped_y * (interior_height - 1)));
    const int center_x = interior_x + static_cast<int>(std::round(clamped_x * (interior_width - 1)));

    const int max_top = interior_y + interior_height - square_height;
    const int max_left = interior_x + interior_width - square_width;

    const int top = std::clamp(center_y - square_height / 2, interior_y, std::max(interior_y, max_top));
    const int left = std::clamp(center_x - square_width / 2, interior_x, std::max(interior_x, max_left));

    std::string row_pattern;
    row_pattern.reserve(static_cast<size_t>(square_width) * 3u);
    for (int i = 0; i < square_width; ++i) {
        row_pattern.append("█");
    }

    for (int row = 0; row < square_height; ++row) {
        const int draw_y = top + row;
        if (draw_y < interior_y || draw_y >= interior_y + interior_height) {
            continue;
        }
        ncplane_putstr_yx(plane_, draw_y, left, row_pattern.c_str());
    }
}

void SpaceRockAnimation::spawn_squares(int count) {
    if (count <= 0) {
        return;
    }

    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    for (int i = 0; i < count; ++i) {
        Square square{};
        square.x = distribution(rng_);
        square.y = distribution(rng_);
        square.size = kDefaultSquareSize;
        square.age = 0.0f;
        square.lifespan = params_.square_lifespan_s;
        squares_.push_back(square);
    }
}

} // namespace animations
} // namespace when

