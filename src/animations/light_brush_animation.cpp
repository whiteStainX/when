#include "light_brush_animation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <utility>

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
constexpr float kSpeedScaleMin = 0.6f;
constexpr float kSpeedScaleMax = 1.8f;
constexpr float kTurbulenceBaseStrength = 0.45f;
constexpr std::size_t kMaxAttractors = 3;
constexpr float kAttractorRadius = 0.42f;
constexpr float kSeekingStrength = 1.25f;
constexpr float kAttractorEpsilon = 1.0e-3f;
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
    strokes_.clear();
    elapsed_time_ = 0.0f;

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

    elapsed_time_ += delta_time;

    const float clamped_total_energy = std::clamp(features.total_energy, 0.0f, 1.0f);
    // Map the smoothed total energy into a speed multiplier. Quiet passages nudge
    // the scale toward kSpeedScaleMin while intense sections approach
    // kSpeedScaleMax.
    const float speed_scale =
        kSpeedScaleMin + (kSpeedScaleMax - kSpeedScaleMin) * clamped_total_energy;

    const float clamped_flatness = std::clamp(features.spectral_flatness, 0.0f, 1.0f);
    // Higher spectral flatness values (noisier textures) yield more turbulence,
    // keeping tonal passages comparatively smooth.
    const float turbulence_strength = clamped_flatness * kTurbulenceBaseStrength * delta_time;
    std::uniform_real_distribution<float> turbulence_dist(-1.0f, 1.0f);

    for (auto& stroke : strokes_) {
        stroke.head.age += delta_time;
    }

    strokes_.erase(std::remove_if(strokes_.begin(),
                                  strokes_.end(),
                                  [](const BrushStroke& stroke) {
                                      return stroke.head.age >= stroke.head.lifespan;
                                  }),
                   strokes_.end());

    std::array<std::pair<float, float>, kMaxAttractors> attractor_positions{};
    std::array<float, kMaxAttractors> attractor_weights{};
    std::size_t attractor_count = 0;

    if (features.chroma_available) {
        std::array<std::pair<float, int>, 12> note_strengths{};
        for (int i = 0; i < 12; ++i) {
            note_strengths[i] = {features.chroma[i], i};
        }

        std::sort(note_strengths.begin(),
                  note_strengths.end(),
                  [](const auto& lhs, const auto& rhs) {
                      return lhs.first > rhs.first;
                  });

        const float strongest = note_strengths.front().first;
        if (strongest > 0.0f) {
            for (const auto& [strength, note_index] : note_strengths) {
                if (strength <= 0.0f) {
                    break;
                }

                const float angle =
                    (static_cast<float>(note_index) / 12.0f) * kTwoPi;
                const float strength_scale = strength / strongest;

                const float x = 0.5f + std::cos(angle) * kAttractorRadius;
                const float y = 0.5f + std::sin(angle) * kAttractorRadius;

                attractor_positions[attractor_count] =
                    {std::clamp(x, 0.0f, 1.0f), std::clamp(y, 0.0f, 1.0f)};
                attractor_weights[attractor_count] = strength_scale;
                ++attractor_count;

                if (attractor_count >= kMaxAttractors) {
                    break;
                }
            }
        }
    }

    for (auto& stroke : strokes_) {
        auto& particle = stroke.head;
        if (attractor_count > 0) {
            float nearest_distance_sq = std::numeric_limits<float>::max();
            std::pair<float, float> nearest_attractor{particle.x, particle.y};
            float nearest_weight = 1.0f;

            for (std::size_t i = 0; i < attractor_count; ++i) {
                const float dx = attractor_positions[i].first - particle.x;
                const float dy = attractor_positions[i].second - particle.y;
                const float distance_sq = dx * dx + dy * dy;

                if (distance_sq < nearest_distance_sq) {
                    nearest_distance_sq = distance_sq;
                    nearest_attractor = attractor_positions[i];
                    nearest_weight = std::max(attractor_weights[i], 0.1f);
                }
            }

            if (nearest_distance_sq > 0.0f) {
                const float dx = nearest_attractor.first - particle.x;
                const float dy = nearest_attractor.second - particle.y;
                const float distance =
                    std::sqrt(std::max(nearest_distance_sq, kAttractorEpsilon));
                const float scale =
                    (kSeekingStrength * nearest_weight * delta_time) /
                    (distance + kAttractorEpsilon);
                particle.vx += dx * scale;
                particle.vy += dy * scale;
            }
        }

        if (clamped_flatness > 0.0f) {
            particle.vx += turbulence_dist(rng_) * turbulence_strength;
            particle.vy += turbulence_dist(rng_) * turbulence_strength;
        }

        particle.x += particle.vx * delta_time * speed_scale;
        particle.y += particle.vy * delta_time * speed_scale;

        if (particle.x < 0.0f) {
            particle.x = -particle.x;
            particle.vx = std::abs(particle.vx);
        } else if (particle.x > 1.0f) {
            particle.x = 2.0f - particle.x;
            particle.vx = -std::abs(particle.vx);
        }

        if (particle.y < 0.0f) {
            particle.y = -particle.y;
            particle.vy = std::abs(particle.vy);
        } else if (particle.y > 1.0f) {
            particle.y = 2.0f - particle.y;
            particle.vy = -std::abs(particle.vy);
        }

        particle.x = std::clamp(particle.x, 0.0f, 1.0f);
        particle.y = std::clamp(particle.y, 0.0f, 1.0f);

        stroke.trail.push_front(TrailPoint{particle.x, particle.y, elapsed_time_});

        const float trail_lifespan = std::max(particle.lifespan, 0.0f);
        while (!stroke.trail.empty()) {
            const float trail_age = elapsed_time_ - stroke.trail.back().spawn_time;
            if (trail_age <= trail_lifespan) {
                break;
            }
            stroke.trail.pop_back();
        }
    }

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

    for (const auto& stroke : strokes_) {
        const float fade_duration = std::max(stroke.head.lifespan, 1.0e-3f);

        for (auto it = stroke.trail.rbegin(); it != stroke.trail.rend(); ++it) {
            const float age = std::max(0.0f, elapsed_time_ - it->spawn_time);
            const float normalized_age = std::clamp(age / fade_duration, 0.0f, 1.0f);
            const float brightness = 1.0f - normalized_age;
            if (brightness <= 0.0f) {
                continue;
            }

            const std::uint8_t intensity = static_cast<std::uint8_t>(
                std::round(brightness * static_cast<float>(kParticleForegroundColor)));

            if (intensity == 0u) {
                continue;
            }

            render_point(it->x,
                         it->y,
                         intensity,
                         frame_y,
                         frame_x,
                         interior_height,
                         interior_width);
        }

        render_point(stroke.head.x,
                     stroke.head.y,
                     kParticleForegroundColor,
                     frame_y,
                     frame_x,
                     interior_height,
                     interior_width);
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

void LightBrushAnimation::render_point(float normalized_x,
                                       float normalized_y,
                                       std::uint8_t intensity,
                                       int frame_y,
                                       int frame_x,
                                       int interior_height,
                                       int interior_width) {
    if (!plane_ || interior_height <= 0 || interior_width <= 0 || intensity == 0u) {
        return;
    }

    const float clamped_x = std::clamp(normalized_x, 0.0f, 1.0f);
    const float clamped_y = std::clamp(normalized_y, 0.0f, 1.0f);

    const int y = frame_y + 1 + static_cast<int>(std::round(clamped_y * std::max(0, interior_height - 1)));
    const int x = frame_x + 1 + static_cast<int>(std::round(clamped_x * std::max(0, interior_width - 1)));

    nccell cell = NCCELL_TRIVIAL_INITIALIZER;
    if (nccell_load_ucs32(plane_, &cell, 0x2588u) <= 0) { // Full block character
        return;
    }

    const int color = static_cast<int>(intensity);
    nccell_set_fg_rgb8(&cell, color, color, color);
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
        BrushStroke stroke;
        stroke.head.x = position_dist(rng_);
        stroke.head.y = position_dist(rng_);

        const float angle = angle_dist(rng_);
        const float speed = speed_dist(rng_);
        stroke.head.vx = std::cos(angle) * speed;
        stroke.head.vy = std::sin(angle) * speed;
        stroke.head.age = 0.0f;

        const float lifespan_min = heavy ? kHeavyLifespanMin : kLightLifespanMin;
        const float lifespan_max = heavy ? kHeavyLifespanMax : kLightLifespanMax;
        stroke.head.lifespan =
            lifespan_min + (lifespan_max - lifespan_min) * std::clamp(treble_envelope, 0.0f, 1.0f);

        stroke.trail.push_front(TrailPoint{stroke.head.x, stroke.head.y, elapsed_time_});

        const float trail_lifespan = std::max(stroke.head.lifespan, 0.0f);
        while (!stroke.trail.empty()) {
            const float trail_age = elapsed_time_ - stroke.trail.back().spawn_time;
            if (trail_age <= trail_lifespan) {
                break;
            }
            stroke.trail.pop_back();
        }

        strokes_.push_back(std::move(stroke));
    }
}

} // namespace animations
} // namespace when

