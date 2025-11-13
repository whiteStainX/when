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
constexpr float kTwoPi = 6.28318530718f;
constexpr std::size_t kMaxAttractors = 3;
constexpr float kAttractorEpsilon = 1.0e-3f;
constexpr int kBrailleRowsPerCell = 4;
constexpr int kBrailleColsPerCell = 2;

int clamp_color_value(int value) {
    return std::clamp(value, 0, 255);
}
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
    parameters_ = LightBrushParameters{};

    for (const auto& anim_config : config.animations) {
        if (anim_config.type == "LightBrush") {
            z_index_ = anim_config.z_index;
            is_active_ = anim_config.initially_active;
            apply_animation_config(anim_config);
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
    // the scale toward the configured minimum while intense sections approach
    // the configured maximum.
    const float speed_scale = parameters_.speed_scale_min +
                               (parameters_.speed_scale_max - parameters_.speed_scale_min) *
                                   clamped_total_energy;

    const float clamped_flatness = std::clamp(features.spectral_flatness, 0.0f, 1.0f);
    // Higher spectral flatness values (noisier textures) yield more turbulence,
    // keeping tonal passages comparatively smooth.
    const float turbulence_strength =
        clamped_flatness * parameters_.turbulence_base_strength * delta_time;
    const float clamped_beat_strength = std::clamp(features.beat_strength, 0.0f, 1.0f);
    const float tonal_weight = parameters_.tonal_weight_base +
                               (1.0f - clamped_flatness) * parameters_.tonal_weight_scale;
    const float beat_weight =
        parameters_.beat_weight_base + clamped_beat_strength * parameters_.beat_weight_scale;
    std::uniform_real_distribution<float> turbulence_dist(-1.0f, 1.0f);

    for (auto& stroke : strokes_) {
        stroke.head.age += delta_time;
    }

    strokes_.erase(std::remove_if(strokes_.begin(),
                                  strokes_.end(),
                                  [&](const BrushStroke& stroke) {
                                      return compute_brightness(stroke.head.age,
                                                                stroke.head.lifespan) <=
                                             0.0f;
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

                const float x = 0.5f + std::cos(angle) * parameters_.attractor_radius;
                const float y = 0.5f + std::sin(angle) * parameters_.attractor_radius;

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
        const float thickness_target = std::clamp(stroke.base_thickness * beat_weight * tonal_weight,
                                                  parameters_.thickness_min,
                                                  parameters_.thickness_max);
        stroke.thickness +=
            (thickness_target - stroke.thickness) * parameters_.thickness_smoothing;
        stroke.thickness =
            std::clamp(stroke.thickness, parameters_.thickness_min, parameters_.thickness_max);
        particle.thickness = stroke.thickness;
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
                const float scale = (parameters_.seeking_strength * nearest_weight * delta_time) /
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

        stroke.trail.push_front(TrailPoint{particle.x, particle.y, elapsed_time_, stroke.thickness});

        const float trail_lifespan = std::max(particle.lifespan, 0.0f);
        while (!stroke.trail.empty()) {
            const float trail_age = std::max(0.0f, elapsed_time_ - stroke.trail.back().spawn_time);
            const float trail_brightness = compute_brightness(trail_age, trail_lifespan);
            if (trail_brightness > 0.0f) {
                break;
            }
            stroke.trail.pop_back();
        }
    }

    const float clamped_treble = std::clamp(features.treble_envelope, 0.0f, 1.0f);

    if (features.bass_beat) {
        spawn_particles(1, true, clamped_treble, clamped_beat_strength, clamped_flatness);
    } else if (features.mid_beat) {
        spawn_particles(1, false, clamped_treble, clamped_beat_strength, clamped_flatness);
    } else if (features.treble_beat) {
        spawn_particles(1, false, clamped_treble, clamped_beat_strength, clamped_flatness);
    } else if (features.beat_detected) {
        const bool heavy_bias = features.bass_energy >= features.mid_energy;
        spawn_particles(1, heavy_bias, clamped_treble, clamped_beat_strength, clamped_flatness);
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
    const float cell_ratio = std::max(parameters_.cell_width_to_height_ratio, 1.0e-3f);
    const float frame_fill_ratio = std::clamp(parameters_.frame_fill_ratio, 0.0f, 1.0f);
    const float plane_physical_width = static_cast<float>(plane_cols_) * cell_ratio;
    const float max_physical_extent = std::min(plane_physical_height, plane_physical_width);
    const float target_physical_extent = std::max(1.0f, max_physical_extent * frame_fill_ratio);

    const int max_width_for_height =
        std::max(2, static_cast<int>(std::floor(static_cast<float>(plane_rows_) / cell_ratio)));

    int frame_width = static_cast<int>(std::round(target_physical_extent / cell_ratio));
    frame_width = std::clamp(frame_width, 2, std::min(static_cast<int>(plane_cols_), max_width_for_height));

    int frame_height = static_cast<int>(std::round(static_cast<float>(frame_width) * cell_ratio));
    frame_height = std::clamp(frame_height, 2, static_cast<int>(plane_rows_));

    frame_width =
        std::clamp(static_cast<int>(std::round(static_cast<float>(frame_height) / cell_ratio)),
                   2,
                   static_cast<int>(plane_cols_));
    frame_height =
        std::clamp(static_cast<int>(std::round(static_cast<float>(frame_width) * cell_ratio)),
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
    accumulation_buffer_.assign(cell_count, Color{});

    bool any_braille_samples = false;
    struct FallbackSample {
        float x = 0.5f;
        float y = 0.5f;
        float intensity = 0.0f;
    };
    FallbackSample strongest_sample;

    for (const auto& stroke : strokes_) {
        const float fade_duration = std::max(stroke.head.lifespan, 1.0e-3f);
        const float stroke_brightness = compute_brightness(stroke.head.age, fade_duration);
        if (stroke_brightness <= 0.0f) {
            continue;
        }

        for (auto it = stroke.trail.rbegin(); it != stroke.trail.rend(); ++it) {
            const float age = std::max(0.0f, elapsed_time_ - it->spawn_time);
            const float point_fade = compute_brightness(age, fade_duration);
            const float brightness = stroke_brightness * point_fade;
            if (brightness <= 0.0f) {
                continue;
            }

            const float point_thickness = std::max(it->thickness * brightness, 0.0f);
            if (point_thickness <= 0.0f) {
                continue;
            }

            any_braille_samples |= render_point(it->x,
                                                it->y,
                                                brightness,
                                                point_thickness,
                                                frame_y,
                                                frame_x,
                                                interior_height,
                                                interior_width);

            if (brightness > strongest_sample.intensity) {
                strongest_sample = {it->x, it->y, brightness};
            }
        }

        const float head_thickness = std::max(stroke.thickness * stroke_brightness, 0.0f);
        if (head_thickness <= 0.0f) {
            continue;
        }

        any_braille_samples |= render_point(stroke.head.x,
                                            stroke.head.y,
                                            stroke_brightness,
                                            head_thickness,
                                            frame_y,
                                            frame_x,
                                            interior_height,
                                            interior_width);

        if (stroke_brightness > strongest_sample.intensity) {
            strongest_sample = {stroke.head.x, stroke.head.y, stroke_brightness};
        }
    }

    if (!any_braille_samples && strongest_sample.intensity > 0.0f) {
        const float clamped_x = std::clamp(strongest_sample.x, 0.0f, 1.0f);
        const float clamped_y = std::clamp(strongest_sample.y, 0.0f, 1.0f);
        const int y = frame_y + 1 +
                      static_cast<int>(std::round(clamped_y * std::max(0, interior_height - 1)));
        const int x = frame_x + 1 +
                      static_cast<int>(std::round(clamped_x * std::max(0, interior_width - 1)));

        nccell cell = NCCELL_TRIVIAL_INITIALIZER;
        if (nccell_load_ucs32(plane_, &cell, 0x2588u) > 0) {
            const int fg_color = clamp_color_value(parameters_.particle_foreground_color);
            const int bg_color = clamp_color_value(parameters_.particle_background_color);
            const int color = static_cast<int>(
                std::round(std::clamp(strongest_sample.intensity, 0.0f, 1.0f) *
                           static_cast<float>(fg_color)));
            nccell_set_fg_rgb8(&cell, color, color, color);
            nccell_set_bg_rgb8(&cell, bg_color, bg_color, bg_color);
            ncplane_putc_yx(plane_, y, x, &cell);
            nccell_release(plane_, &cell);
        }
    }

    for (int row = 0; row < interior_height; ++row) {
        for (int col = 0; col < interior_width; ++col) {
            const std::size_t index =
                static_cast<std::size_t>(row) * static_cast<std::size_t>(interior_width) +
                static_cast<std::size_t>(col);
            if (index >= braille_masks_.size() || index >= accumulation_buffer_.size()) {
                continue;
            }

            const std::uint8_t mask = braille_masks_[index];
            if (mask == 0u) {
                continue;
            }

            const Color& color = accumulation_buffer_[index];
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
            const int fg_color = clamp_color_value(parameters_.particle_foreground_color);
            const int bg_color = clamp_color_value(parameters_.particle_background_color);
            nccell_set_fg_rgb8(&cell,
                               static_cast<int>(std::round(clamped_r * static_cast<float>(fg_color))),
                               static_cast<int>(std::round(clamped_g * static_cast<float>(fg_color))),
                               static_cast<int>(std::round(clamped_b * static_cast<float>(fg_color))));
            nccell_set_bg_rgb8(&cell, bg_color, bg_color, bg_color);

            ncplane_putc_yx(plane_, frame_y + 1 + row, frame_x + 1 + col, &cell);
            nccell_release(plane_, &cell);
        }
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

void LightBrushAnimation::apply_animation_config(const AnimationConfig& config) {
    const LightBrushParameters defaults;

    parameters_.frame_fill_ratio =
        std::clamp(config.light_brush_frame_fill_ratio, 0.0f, 1.0f);
    if (!std::isfinite(parameters_.frame_fill_ratio)) {
        parameters_.frame_fill_ratio = defaults.frame_fill_ratio;
    }

    parameters_.cell_width_to_height_ratio = config.light_brush_cell_aspect_ratio;
    if (!std::isfinite(parameters_.cell_width_to_height_ratio) ||
        parameters_.cell_width_to_height_ratio <= 0.0f) {
        parameters_.cell_width_to_height_ratio = defaults.cell_width_to_height_ratio;
    }

    parameters_.frame_foreground_color =
        clamp_color_value(config.light_brush_frame_foreground_color);
    parameters_.frame_background_color =
        clamp_color_value(config.light_brush_frame_background_color);
    parameters_.particle_foreground_color =
        clamp_color_value(config.light_brush_particle_foreground_color);
    parameters_.particle_background_color =
        clamp_color_value(config.light_brush_particle_background_color);

    parameters_.heavy_velocity_min = std::max(0.0f, config.light_brush_heavy_velocity_min);
    parameters_.heavy_velocity_max =
        std::max(parameters_.heavy_velocity_min, std::max(0.0f, config.light_brush_heavy_velocity_max));

    parameters_.light_velocity_min = std::max(0.0f, config.light_brush_light_velocity_min);
    parameters_.light_velocity_max =
        std::max(parameters_.light_velocity_min, std::max(0.0f, config.light_brush_light_velocity_max));

    parameters_.heavy_lifespan_min = std::max(0.0f, config.light_brush_heavy_lifespan_min);
    parameters_.heavy_lifespan_max =
        std::max(parameters_.heavy_lifespan_min, std::max(0.0f, config.light_brush_heavy_lifespan_max));

    parameters_.light_lifespan_min = std::max(0.0f, config.light_brush_light_lifespan_min);
    parameters_.light_lifespan_max =
        std::max(parameters_.light_lifespan_min, std::max(0.0f, config.light_brush_light_lifespan_max));

    parameters_.speed_scale_min = std::max(0.0f, config.light_brush_speed_scale_min);
    parameters_.speed_scale_max =
        std::max(parameters_.speed_scale_min, std::max(0.0f, config.light_brush_speed_scale_max));

    parameters_.turbulence_base_strength =
        std::max(0.0f, config.light_brush_turbulence_base_strength);

    parameters_.attractor_radius = std::clamp(config.light_brush_attractor_radius, 0.0f, 1.0f);
    if (!std::isfinite(parameters_.attractor_radius)) {
        parameters_.attractor_radius = defaults.attractor_radius;
    }

    parameters_.seeking_strength = std::max(0.0f, config.light_brush_seeking_strength);

    parameters_.thickness_min = std::max(0.0f, config.light_brush_thickness_min);
    parameters_.thickness_max =
        std::max(parameters_.thickness_min, std::max(0.0f, config.light_brush_thickness_max));

    parameters_.thickness_smoothing =
        std::clamp(config.light_brush_thickness_smoothing, 0.0f, 1.0f);
    if (!std::isfinite(parameters_.thickness_smoothing)) {
        parameters_.thickness_smoothing = defaults.thickness_smoothing;
    }

    parameters_.thickness_radius_scale =
        std::max(0.0f, config.light_brush_thickness_radius_scale);
    if (!std::isfinite(parameters_.thickness_radius_scale) ||
        parameters_.thickness_radius_scale <= 0.0f) {
        parameters_.thickness_radius_scale = defaults.thickness_radius_scale;
    }

    parameters_.beat_weight_base = config.light_brush_beat_weight_base;
    parameters_.beat_weight_scale = std::max(0.0f, config.light_brush_beat_weight_scale);
    parameters_.tonal_weight_base = config.light_brush_tonal_weight_base;
    parameters_.tonal_weight_scale = std::max(0.0f, config.light_brush_tonal_weight_scale);

    parameters_.heavy_thickness_bias =
        std::max(0.0f, config.light_brush_heavy_thickness_bias);
    parameters_.light_thickness_bias =
        std::max(0.0f, config.light_brush_light_thickness_bias);

    parameters_.base_thickness_base = config.light_brush_base_thickness_base;
    parameters_.base_thickness_beat_scale =
        std::max(0.0f, config.light_brush_base_thickness_beat_scale);
    parameters_.base_thickness_tonal_base = config.light_brush_base_thickness_tonal_base;
    parameters_.base_thickness_tonal_scale =
        std::max(0.0f, config.light_brush_base_thickness_tonal_scale);
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
        const int frame_fg = clamp_color_value(parameters_.frame_foreground_color);
        const int frame_bg = clamp_color_value(parameters_.frame_background_color);
        nccell_set_fg_rgb8(cell,
                           frame_fg,
                           frame_fg,
                           frame_fg);
        nccell_set_bg_rgb8(cell, frame_bg, frame_bg, frame_bg);
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

bool LightBrushAnimation::render_point(float normalized_x,
                                       float normalized_y,
                                       float brightness,
                                       float thickness,
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
    const float radius = std::max(thickness * parameters_.thickness_radius_scale, 0.1f);

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
            Color& color = accumulation_buffer_[index];
            color.r += sample_intensity;
            color.g += sample_intensity;
            color.b += sample_intensity;
            wrote_sample = true;
        }
    }
    return wrote_sample;
}

float LightBrushAnimation::compute_brightness(float age, float lifespan) const {
    if (lifespan <= 1.0e-6f) {
        return 0.0f;
    }

    const float normalized_age = std::clamp(age / lifespan, 0.0f, 1.0f);
    const float eased = 1.0f - normalized_age;
    return eased * eased;
}

void LightBrushAnimation::spawn_particles(int count,
                                          bool heavy,
                                          float treble_envelope,
                                          float beat_strength,
                                          float spectral_flatness) {
    if (count <= 0) {
        return;
    }

    count = std::min(count, 1);

    std::uniform_real_distribution<float> position_dist(0.0f, 1.0f);
    std::uniform_real_distribution<float> angle_dist(0.0f, kTwoPi);
    const float raw_min_speed = heavy ? parameters_.heavy_velocity_min : parameters_.light_velocity_min;
    const float raw_max_speed = heavy ? parameters_.heavy_velocity_max : parameters_.light_velocity_max;
    const float min_speed = std::min(raw_min_speed, raw_max_speed);
    const float max_speed = std::max(raw_min_speed, raw_max_speed);
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

        const float raw_lifespan_min =
            heavy ? parameters_.heavy_lifespan_min : parameters_.light_lifespan_min;
        const float raw_lifespan_max =
            heavy ? parameters_.heavy_lifespan_max : parameters_.light_lifespan_max;
        const float lifespan_min = std::min(raw_lifespan_min, raw_lifespan_max);
        const float lifespan_max = std::max(raw_lifespan_min, raw_lifespan_max);
        stroke.head.lifespan =
            lifespan_min + (lifespan_max - lifespan_min) * std::clamp(treble_envelope, 0.0f, 1.0f);

        const float clamped_beat = std::clamp(beat_strength, 0.0f, 1.0f);
        const float tonal_presence = 1.0f - std::clamp(spectral_flatness, 0.0f, 1.0f);
        const float heavy_bias = heavy ? parameters_.heavy_thickness_bias : parameters_.light_thickness_bias;
        float base_thickness =
            heavy_bias *
            (parameters_.base_thickness_base + clamped_beat * parameters_.base_thickness_beat_scale) *
            (parameters_.base_thickness_tonal_base + tonal_presence * parameters_.base_thickness_tonal_scale);
        base_thickness =
            std::clamp(base_thickness, parameters_.thickness_min, parameters_.thickness_max);
        stroke.base_thickness = base_thickness;
        stroke.thickness = base_thickness;
        stroke.head.thickness = base_thickness;

        stroke.trail.push_front(TrailPoint{stroke.head.x, stroke.head.y, elapsed_time_, base_thickness});

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

