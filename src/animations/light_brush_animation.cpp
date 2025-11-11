#include "light_brush_animation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>

#include "animation_event_utils.h"

namespace when {
namespace animations {
namespace {
constexpr float kFrameFillRatio = 0.82f;
constexpr float kCellWidthToHeightRatio = 0.5f;
constexpr std::uint8_t kFrameForegroundColor = 240u;
constexpr std::uint8_t kFrameBackgroundColor = 18u;
constexpr std::uint8_t kParticleForegroundColor = 255u;
constexpr std::uint8_t kParticleBackgroundColor = 0u;
constexpr float kTwoPi = 6.28318530718f;
constexpr float kHeavyVelocityMin = 0.08f;
constexpr float kHeavyVelocityMax = 0.18f;
constexpr float kLightVelocityMin = 0.18f;
constexpr float kLightVelocityMax = 0.35f;
constexpr float kHeavySpawnStrengthScale = 3.0f;
constexpr float kLightSpawnStrengthScale = 4.0f;
constexpr float kHeavyLifespanMin = 1.1f;
constexpr float kHeavyLifespanMax = 3.0f;
constexpr float kLightLifespanMin = 0.6f;
constexpr float kLightLifespanMax = 2.0f;
}

LightBrushAnimation::LightBrushAnimation()
    : rng_(static_cast<std::mt19937::result_type>(
          std::chrono::steady_clock::now().time_since_epoch().count())) {}

LightBrushAnimation::~LightBrushAnimation() {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }
}

void LightBrushAnimation::init(notcurses* nc, const AppConfig& config) {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }

    is_active_ = true;
    z_index_ = 0;
    plane_rows_ = 0;
    plane_cols_ = 0;
    particles_.clear();

    for (const auto& anim_config : config.animations) {
        if (anim_config.type == "LightBrush") {
            z_index_ = anim_config.z_index;
            is_active_ = anim_config.initially_active;
            break;
        }
    }

    create_or_resize_plane(nc);
}

void LightBrushAnimation::update(float delta_time,
                                 const AudioMetrics& /*metrics*/,
                                 const AudioFeatures& features) {
    if (!is_active_) {
        return;
    }

    for (auto& particle : particles_) {
        particle.age += delta_time;
    }

    particles_.erase(std::remove_if(particles_.begin(),
                                    particles_.end(),
                                    [](const StrokeParticle& particle) {
                                        return particle.age >= particle.lifespan;
                                    }),
                     particles_.end());

    const float clamped_strength = std::clamp(features.beat_strength, 0.0f, 1.0f);
    const auto compute_spawn_count = [&](bool heavy) {
        const float scale = heavy ? kHeavySpawnStrengthScale : kLightSpawnStrengthScale;
        const int scaled = static_cast<int>(std::round(clamped_strength * scale));
        return std::max(1, scaled);
    };

    const float clamped_treble = std::clamp(features.treble_envelope, 0.0f, 1.0f);

    if (features.bass_beat) {
        spawn_particles(compute_spawn_count(true), true, clamped_treble);
    }

    if (features.mid_beat) {
        spawn_particles(compute_spawn_count(false), false, clamped_treble);
    }
}

void LightBrushAnimation::render(notcurses* /*nc*/) {
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

    for (const auto& particle : particles_) {
        render_particle(particle, frame_y, frame_x, interior_height, interior_width);
    }
}

void LightBrushAnimation::activate() {
    is_active_ = true;
}

void LightBrushAnimation::deactivate() {
    is_active_ = false;
    if (plane_) {
        ncplane_erase(plane_);
    }
}

bool LightBrushAnimation::is_active() const {
    return is_active_;
}

int LightBrushAnimation::get_z_index() const {
    return z_index_;
}

ncplane* LightBrushAnimation::get_plane() const {
    return plane_;
}

void LightBrushAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    bind_standard_frame_updates(this, config, bus);
}

void LightBrushAnimation::create_or_resize_plane(notcurses* nc) {
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

void LightBrushAnimation::draw_frame(int frame_y, int frame_x, int frame_height, int frame_width) {
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

void LightBrushAnimation::render_particle(const StrokeParticle& particle,
                                          int frame_y,
                                          int frame_x,
                                          int interior_height,
                                          int interior_width) {
    if (!plane_ || interior_height <= 0 || interior_width <= 0) {
        return;
    }

    const float clamped_x = std::clamp(particle.x, 0.0f, 1.0f);
    const float clamped_y = std::clamp(particle.y, 0.0f, 1.0f);

    const int y = frame_y + 1 + static_cast<int>(std::round(clamped_y * std::max(0, interior_height - 1)));
    const int x = frame_x + 1 + static_cast<int>(std::round(clamped_x * std::max(0, interior_width - 1)));

    nccell cell = NCCELL_TRIVIAL_INITIALIZER;
    if (nccell_load_ucs32(plane_, &cell, 0x2588u) <= 0) { // Full block character
        return;
    }

    nccell_set_fg_rgb8(&cell,
                       static_cast<int>(kParticleForegroundColor),
                       static_cast<int>(kParticleForegroundColor),
                       static_cast<int>(kParticleForegroundColor));
    nccell_set_bg_rgb8(&cell,
                       static_cast<int>(kParticleBackgroundColor),
                       static_cast<int>(kParticleBackgroundColor),
                       static_cast<int>(kParticleBackgroundColor));

    ncplane_putc_yx(plane_, y, x, &cell);
    nccell_release(plane_, &cell);
}

void LightBrushAnimation::spawn_particles(int count, bool heavy, float treble_envelope) {
    if (count <= 0) {
        return;
    }

    std::uniform_real_distribution<float> position_dist(0.0f, 1.0f);
    std::uniform_real_distribution<float> angle_dist(0.0f, kTwoPi);
    const float min_speed = heavy ? kHeavyVelocityMin : kLightVelocityMin;
    const float max_speed = heavy ? kHeavyVelocityMax : kLightVelocityMax;
    std::uniform_real_distribution<float> speed_dist(min_speed, max_speed);

    for (int i = 0; i < count; ++i) {
        StrokeParticle particle;
        particle.x = position_dist(rng_);
        particle.y = position_dist(rng_);

        const float angle = angle_dist(rng_);
        const float speed = speed_dist(rng_);
        particle.vx = std::cos(angle) * speed;
        particle.vy = std::sin(angle) * speed;
        particle.age = 0.0f;

        const float lifespan_min = heavy ? kHeavyLifespanMin : kLightLifespanMin;
        const float lifespan_max = heavy ? kHeavyLifespanMax : kLightLifespanMax;
        particle.lifespan =
            lifespan_min + (lifespan_max - lifespan_min) * std::clamp(treble_envelope, 0.0f, 1.0f);

        particles_.push_back(particle);
    }
}

} // namespace animations
} // namespace when

