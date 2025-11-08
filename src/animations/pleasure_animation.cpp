#include "pleasure_animation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <random>
#include <vector>

#include "animation_event_utils.h"

namespace when {
namespace animations {

namespace {
constexpr unsigned int kDefaultPlaneRows = 24;
constexpr unsigned int kDefaultPlaneCols = 48;
constexpr int kBrailleRowsPerCell = 4;
constexpr int kBrailleColsPerCell = 2;
constexpr float kTwoPi = 6.28318530717958647692f;
} // namespace

PleasureAnimation::PleasureAnimation()
    : rng_(std::random_device{}()) {}

PleasureAnimation::~PleasureAnimation() {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }
}

void PleasureAnimation::init(notcurses* nc, const AppConfig& config) {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }

    z_index_ = 0;
    is_active_ = true;
    params_ = PleasureParameters{};

    unsigned int desired_rows = kDefaultPlaneRows;
    unsigned int desired_cols = kDefaultPlaneCols;
    int desired_y = 0;
    int desired_x = 0;
    bool custom_origin_y = false;
    bool custom_origin_x = false;

    for (const auto& anim_config : config.animations) {
        if (anim_config.type == "Pleasure") {
            z_index_ = anim_config.z_index;
            is_active_ = anim_config.initially_active;
            load_parameters_from_config(anim_config);

            if (anim_config.plane_rows) {
                desired_rows = std::max(1, *anim_config.plane_rows);
            }
            if (anim_config.plane_cols) {
                desired_cols = std::max(1, *anim_config.plane_cols);
            }
            if (anim_config.plane_y) {
                desired_y = *anim_config.plane_y;
                custom_origin_y = true;
            }
            if (anim_config.plane_x) {
                desired_x = *anim_config.plane_x;
                custom_origin_x = true;
            }
            break;
        }
    }

    desired_rows = std::max<unsigned int>(1, desired_rows);
    desired_cols = std::max<unsigned int>(1, desired_cols);

    ncplane* stdplane = notcurses_stdplane(nc);
    if (!stdplane) {
        return;
    }

    unsigned int std_rows = 0;
    unsigned int std_cols = 0;
    ncplane_dim_yx(stdplane, &std_rows, &std_cols);

    plane_rows_ = std::min(desired_rows, std_rows > 0u ? std_rows : desired_rows);
    plane_cols_ = std::min(desired_cols, std_cols > 0u ? std_cols : desired_cols);

    if (std_rows > 0u) {
        int max_origin_y = std::max(0, static_cast<int>(std_rows) - static_cast<int>(plane_rows_));
        plane_origin_y_ = custom_origin_y ? std::clamp(desired_y, 0, max_origin_y) : max_origin_y / 2;
    } else {
        plane_origin_y_ = custom_origin_y ? desired_y : 0;
    }

    if (std_cols > 0u) {
        int max_origin_x = std::max(0, static_cast<int>(std_cols) - static_cast<int>(plane_cols_));
        plane_origin_x_ = custom_origin_x ? std::clamp(desired_x, 0, max_origin_x) : max_origin_x / 2;
    } else {
        plane_origin_x_ = custom_origin_x ? desired_x : 0;
    }

    create_or_resize_plane(nc);

    configure_history_capacity();
    last_magnitude_ = 0.0f;
    global_magnitude_ = 0.0f;
    beat_pulse_ = 0.0f;
    downbeat_flash_ = 0.0f;
}

void PleasureAnimation::update(float delta_time,
                               const AudioMetrics& /*metrics*/,
                               const AudioFeatures& features) {
    if (history_capacity_ < 2u || lines_.empty()) {
        return;
    }

    const float knee = std::clamp(params_.soft_clip_knee, 0.01f, 0.99f);
    const float global_headroom = std::max(params_.global_headroom, 1.0f);
    const float ridge_headroom = std::max(params_.ridge_headroom, 1.0f);
    const float profile_headroom = std::max(params_.profile_headroom, 1.0f);
    const auto soft_clip = [knee](float value, float headroom) {
        const float safe_headroom = std::max(headroom, 1e-3f);
        const float safe_value = std::max(value, 0.0f);
        float normalized = safe_value / safe_headroom;
        if (normalized <= 0.0f) {
            return 0.0f;
        }
        if (normalized <= knee) {
            return std::clamp(normalized, 0.0f, 1.0f);
        }
        const float tail = std::max(1.0f - knee, 1e-5f);
        const float excess = normalized - knee;
        const float compressed = knee + tail * (1.0f - std::exp(-excess / tail));
        return std::clamp(compressed, 0.0f, 1.0f);
    };

    const float weighted_energy = features.bass_energy * 0.5f +
                                  features.mid_energy * 0.35f +
                                  features.treble_energy * 0.15f;
    const float reference = std::max({features.total_energy, features.bass_energy, features.mid_energy, features.treble_energy, 1e-5f});
    float magnitude = std::clamp(weighted_energy / reference, 0.0f, 1.0f);

    const float beat_strength = std::clamp(features.beat_strength, 0.0f, 1.0f);
    const float responsive_history =
        std::clamp(params_.history_smoothing * (1.0f + beat_strength * params_.history_beat_boost), 0.0f, 1.0f);
    const float smoothed = (1.0f - responsive_history) * last_magnitude_ + responsive_history * magnitude;
    last_magnitude_ = smoothed;

    const float beat_phase = std::clamp(features.beat_phase, 0.0f, 1.0f);
    const float bar_phase = std::clamp(features.bar_phase, 0.0f, 1.0f);
    const float beat_wave = 0.5f * (1.0f - std::cos(kTwoPi * beat_phase));
    const float shaped_pulse = std::pow(std::clamp(beat_wave, 0.0f, 1.0f), std::max(params_.beat_phase_power, 1e-3f));
    const float target_pulse = beat_strength * shaped_pulse;
    const float pulse_attack = 1.0f - std::exp(-params_.beat_pulse_attack * delta_time);
    const float pulse_release = 1.0f - std::exp(-params_.beat_pulse_release * delta_time);
    const float pulse_smoothing = (target_pulse >= beat_pulse_) ? pulse_attack : pulse_release;
    beat_pulse_ += (target_pulse - beat_pulse_) * pulse_smoothing;
    beat_pulse_ = std::clamp(beat_pulse_, 0.0f, 1.5f);

    if (features.downbeat) {
        downbeat_flash_ = params_.downbeat_flash_strength;
    }
    const float downbeat_decay = std::exp(-params_.downbeat_flash_decay * delta_time);
    downbeat_flash_ = std::max(0.0f, downbeat_flash_ * downbeat_decay);

    const float normalized_magnitude = std::clamp(smoothed * params_.magnitude_scale, 0.0f, 1.0f);
    const float tempo_locked =
        std::clamp(normalized_magnitude * (1.0f + params_.beat_phase_depth * beat_pulse_), 0.0f, global_headroom);
    const float raw_global =
        std::clamp(tempo_locked + beat_strength * params_.beat_response + downbeat_flash_, 0.0f, global_headroom);
    const float target_global = soft_clip(raw_global, global_headroom);
    const float envelope_smoothing = (target_global >= global_magnitude_)
                                         ? std::clamp(params_.global_envelope_smoothing *
                                                          (1.0f + beat_strength * params_.beat_attack_boost),
                                                      0.0f,
                                                      1.0f)
                                         : params_.global_envelope_smoothing;
    global_magnitude_ += (target_global - global_magnitude_) * envelope_smoothing;

    const float center_band_start = 0.5f - params_.center_band_width * 0.5f;
    const float center_band_end = 0.5f + params_.center_band_width * 0.5f;

    const float two_sigma_sq = 2.0f * params_.ridge_sigma * params_.ridge_sigma;
    const float highlight_sigma = std::max(params_.highlight_width, 1e-4f);
    const float two_highlight_sigma_sq = 2.0f * highlight_sigma * highlight_sigma;

    float highlight_pos_target = 0.5f;
    float highlight_strength_target = 0.0f;

    if (!features.band_flux.empty()) {
        float flux_sum = 0.0f;
        float max_flux = 0.0f;
        std::size_t max_index = 0u;
        for (std::size_t i = 0; i < features.band_flux.size(); ++i) {
            const float flux = std::max(features.band_flux[i], 0.0f);
            flux_sum += flux;
            if (flux > max_flux) {
                max_flux = flux;
                max_index = i;
            }
        }

        if (max_flux > 0.0f) {
            const float normalized_flux = max_flux / std::max(flux_sum, 1e-6f);
            if (normalized_flux >= params_.highlight_flux_threshold) {
                highlight_strength_target =
                    (normalized_flux - params_.highlight_flux_threshold) /
                    std::max(1.0f - params_.highlight_flux_threshold, 1e-3f);
                highlight_strength_target = std::clamp(highlight_strength_target, 0.0f, 1.0f);
                highlight_pos_target = (static_cast<float>(max_index) + 0.5f) /
                                       static_cast<float>(features.band_flux.size());
            }
        }
    }

    if (highlight_strength_target > 0.0f && features.chroma_available &&
        features.spectral_flatness <= params_.highlight_flatness_threshold) {
        float max_chroma = 0.0f;
        std::size_t chroma_index = 0u;
        for (std::size_t i = 0; i < features.chroma.size(); ++i) {
            if (features.chroma[i] > max_chroma) {
                max_chroma = features.chroma[i];
                chroma_index = i;
            }
        }

        if (max_chroma > 0.0f) {
            const float chroma_pos = (static_cast<float>(chroma_index) + 0.5f) /
                                     static_cast<float>(features.chroma.size());
            const float mix = std::clamp(params_.highlight_tonal_bias, 0.0f, 1.0f);
            highlight_pos_target =
                std::clamp(highlight_pos_target + (chroma_pos - highlight_pos_target) * mix, 0.0f, 1.0f);
            const float tonal_weight = 1.0f + (1.0f - features.spectral_flatness) * params_.highlight_tonal_bias;
            highlight_strength_target = std::clamp(highlight_strength_target * tonal_weight, 0.0f, 1.0f);
        }
    }

    highlight_strength_target *= params_.highlight_gain;
    highlight_strength_target = std::clamp(highlight_strength_target, 0.0f, 1.0f);

    const bool band_triggers[3] = {features.bass_beat, features.mid_beat, features.treble_beat};

    for (std::size_t line_index = 0; line_index < lines_.size(); ++line_index) {
        auto& line = lines_[line_index];

        if (line.ridges.empty()) {
            initialize_line(line);
        }

        if (line.line_profile.size() != history_capacity_) {
            line.line_profile.assign(history_capacity_, 0.5f);
            line.highlight_pos = 0.5f;
            line.highlight_strength = 0.0f;
        }

        const float depth = (lines_.size() > 1u)
                                ? static_cast<float>(line_index) /
                                      static_cast<float>(lines_.size() - 1u)
                                : 0.0f;
        const float depth_scale = 1.0f - depth * 0.45f;

        const float highlight_target = highlight_strength_target * (0.65f + 0.35f * depth_scale);
        const float highlight_attack = 1.0f - std::exp(-params_.highlight_attack * delta_time);
        const float highlight_release = 1.0f - std::exp(-params_.highlight_release * delta_time);
        const float highlight_smoothing =
            (highlight_target >= line.highlight_strength) ? highlight_attack : highlight_release;
        line.highlight_strength += (highlight_target - line.highlight_strength) * highlight_smoothing;
        line.highlight_pos += (highlight_pos_target - line.highlight_pos) * params_.highlight_position_smoothing;
        line.highlight_pos = std::clamp(line.highlight_pos, 0.0f, 1.0f);

        for (auto& ridge : line.ridges) {
            const float noise_speed = 1.0f + beat_strength * params_.ridge_noise_acceleration;
            ridge.noise_timer += delta_time * noise_speed;
            const int band_index = std::clamp(ridge.band_index, 0, 2);
            if (band_triggers[band_index]) {
                ridge.beat_emphasis += params_.band_beat_gain;
                ridge.base_pos = std::clamp(ridge.base_pos +
                                                random_between(-params_.band_reseed_jitter, params_.band_reseed_jitter),
                                            center_band_start,
                                            center_band_end);
                ridge.phase_offset = std::fmod(ridge.phase_offset + random_between(-0.35f, 0.35f) + 2.0f, 1.0f);
                ridge.noise_timer = 0.0f;
                ridge.noise_interval =
                    random_between(params_.ridge_interval_min * 0.65f, params_.ridge_interval_max * 0.85f);
            }

            if (ridge.noise_timer >= ridge.noise_interval) {
                ridge.base_pos = std::clamp(ridge.base_pos +
                                                random_between(-params_.ridge_position_jitter, params_.ridge_position_jitter),
                                            center_band_start,
                                            center_band_end);
                ridge.noise_timer = 0.0f;
                ridge.noise_interval =
                    random_between(params_.ridge_interval_min, params_.ridge_interval_max);
            }

            ridge.beat_emphasis =
                std::max(0.0f, ridge.beat_emphasis * std::exp(-params_.band_beat_decay * delta_time));

            const float beat_sway = params_.beat_phase_sway *
                                    std::sin(kTwoPi * (beat_phase + ridge.phase_offset + bar_phase * 0.25f));
            ridge.target_pos = std::clamp(ridge.base_pos + beat_sway, center_band_start, center_band_end);

            const float highlight_factor = std::exp(-((ridge.target_pos - line.highlight_pos) *
                                                      (ridge.target_pos - line.highlight_pos)) /
                                                     std::max(two_highlight_sigma_sq, 1e-6f));
            const float beat_boost = 1.0f + ridge.beat_emphasis;
            const float highlight_boost = 1.0f + line.highlight_strength * highlight_factor;
            const float raw_ridge = global_magnitude_ * depth_scale * beat_boost * highlight_boost;
            ridge.target_magnitude = soft_clip(raw_ridge, ridge_headroom);

            ridge.current_pos += (ridge.target_pos - ridge.current_pos) * params_.ridge_position_smoothing;
            ridge.current_magnitude += (ridge.target_magnitude - ridge.current_magnitude) *
                                       params_.ridge_magnitude_smoothing;
        }

        const float base_level =
            global_magnitude_ * 0.08f * (0.6f + 0.4f * depth_scale) + downbeat_flash_ * 0.12f * depth_scale;

        for (std::size_t i = 0; i < history_capacity_; ++i) {
            const float x_norm = (history_capacity_ > 1u)
                                     ? static_cast<float>(i) / static_cast<float>(history_capacity_ - 1u)
                                     : 0.0f;

            float ridge_sum = 0.0f;
            for (const auto& ridge : line.ridges) {
                const float dx = x_norm - ridge.current_pos;
                const float gaussian = std::exp(-(dx * dx) / std::max(two_sigma_sq, 1e-6f));
                ridge_sum += ridge.current_magnitude * gaussian;
            }

            const float highlight_gaussian = std::exp(-((x_norm - line.highlight_pos) * (x_norm - line.highlight_pos)) /
                                                      std::max(two_highlight_sigma_sq, 1e-6f));

            float profile_noise = 0.0f;
            if (params_.profile_noise_amount > 0.0f) {
                profile_noise = random_between(-params_.profile_noise_amount, params_.profile_noise_amount) *
                                (0.4f + 0.6f * (global_magnitude_ + beat_strength * 0.5f));
            }

            const float raw_value = base_level + (ridge_sum * depth_scale) +
                                    (line.highlight_strength * highlight_gaussian * depth_scale) + profile_noise;
            const float target_value = soft_clip(raw_value, profile_headroom);
            line.line_profile[i] += (target_value - line.line_profile[i]) * params_.profile_smoothing;
        }
    }
}

void PleasureAnimation::render(notcurses* /*nc*/) {
    if (!plane_ || !is_active_) {
        return;
    }

    ncplane_erase(plane_);

    unsigned int rows = 0;
    unsigned int cols = 0;
    ncplane_dim_yx(plane_, &rows, &cols);
    if (rows == 0u || cols == 0u) {
        return;
    }

    const int pixel_rows = static_cast<int>(rows) * kBrailleRowsPerCell;
    const int pixel_cols = static_cast<int>(cols) * kBrailleColsPerCell;

    if (pixel_rows <= 0 || pixel_cols <= 0) {
        return;
    }

    if (lines_.empty()) {
        return;
    }

    std::vector<uint8_t> braille_cells(rows * cols, 0);
    std::vector<int> skyline_buffer(static_cast<std::size_t>(pixel_cols), pixel_rows);

    const float max_x = static_cast<float>(pixel_cols - 1);

    for (std::size_t line_index = 0; line_index < lines_.size(); ++line_index) {
        const auto& line = lines_[line_index];
        const std::size_t profile_size = line.line_profile.size();
        if (profile_size < 2u) {
            continue;
        }

        const float max_index = static_cast<float>(profile_size - 1u);
        const int base_y = pixel_rows - 1 - static_cast<int>(line_index) * params_.line_spacing -
                           params_.baseline_margin;
        if (base_y < 0) {
            break;
        }

        const int upward_range = std::max(1, std::min(params_.max_upward_excursion, std::max(0, base_y)));
        const int downward_range =
            std::min(params_.max_downward_excursion, std::max(0, pixel_rows - 1 - base_y));

        for (std::size_t j = 0; j + 1 < profile_size; ++j) {
            const float sample_a = std::clamp(line.line_profile[j], 0.0f, 1.0f);
            const float sample_b = std::clamp(line.line_profile[j + 1], 0.0f, 1.0f);

            const float centered_a = sample_a * 2.0f - 1.0f;
            const float centered_b = sample_b * 2.0f - 1.0f;

            const auto map_to_y = [&](float centered_value) {
                if (centered_value >= 0.0f) {
                    const int offset = static_cast<int>(std::lround(centered_value * upward_range));
                    return base_y - offset;
                }
                const int offset = static_cast<int>(std::lround(-centered_value * downward_range));
                return base_y + offset;
            };

            int y1 = std::clamp(map_to_y(centered_a), 0, pixel_rows - 1);
            int y2 = std::clamp(map_to_y(centered_b), 0, pixel_rows - 1);

            const float x_norm_a = static_cast<float>(j) / std::max(1.0f, max_index);
            const float x_norm_b = static_cast<float>(j + 1u) / std::max(1.0f, max_index);

            int x1 = static_cast<int>(std::lround(x_norm_a * max_x));
            int x2 = static_cast<int>(std::lround(x_norm_b * max_x));

            x1 = std::clamp(x1, 0, pixel_cols - 1);
            x2 = std::clamp(x2, 0, pixel_cols - 1);

            draw_occluded_line(braille_cells, rows, cols, y1, x1, y2, x2, skyline_buffer);
        }
    }

    blit_braille_cells(plane_, braille_cells, rows, cols);
}

void PleasureAnimation::activate() {
    is_active_ = true;
}

void PleasureAnimation::deactivate() {
    is_active_ = false;
    if (plane_) {
        ncplane_erase(plane_);
    }
}

void PleasureAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    bind_standard_frame_updates(this, config, bus);
}

void PleasureAnimation::initialize_line_states() {
    lines_.clear();

    if (!plane_ || history_capacity_ < 2u) {
        return;
    }

    const int pixel_rows = static_cast<int>(plane_rows_) * kBrailleRowsPerCell;
    if (pixel_rows <= 0) {
        return;
    }

    const int available_height = pixel_rows - 1 - params_.baseline_margin;
    if (available_height < 0) {
        return;
    }

    const int max_lines = (available_height / params_.line_spacing) + 1;
    const int desired_lines = std::max(1, std::min(params_.max_lines, max_lines));

    lines_.resize(static_cast<std::size_t>(desired_lines));
    for (auto& line : lines_) {
        line.line_profile.assign(history_capacity_, 0.5f);
        line.ridges.clear();
        line.highlight_pos = 0.5f;
        line.highlight_strength = 0.0f;
        initialize_line(line);
    }
}

void PleasureAnimation::initialize_line(LineState& line_state) {
    line_state.ridges.clear();

    if (history_capacity_ < 2u) {
        return;
    }

    const float center_band_start = 0.5f - params_.center_band_width * 0.5f;
    const float center_band_end = 0.5f + params_.center_band_width * 0.5f;

    std::uniform_int_distribution<int> ridge_count_dist(params_.min_ridges, params_.max_ridges);
    const int ridge_count = ridge_count_dist(rng_);

    for (int i = 0; i < ridge_count; ++i) {
        const float pos = random_between(center_band_start, center_band_end);
        RidgeState ridge{};
        ridge.current_pos = pos;
        ridge.target_pos = pos;
        ridge.current_magnitude = 0.0f;
        ridge.target_magnitude = 0.0f;
        ridge.noise_timer = random_between(0.0f, params_.ridge_interval_min);
        ridge.noise_interval = random_between(params_.ridge_interval_min, params_.ridge_interval_max);
        ridge.base_pos = pos;
        ridge.phase_offset = random_between(0.0f, 1.0f);
        ridge.beat_emphasis = 0.0f;
        const float width = std::max(params_.center_band_width, 1e-4f);
        const float normalized = std::clamp((pos - center_band_start) / width, 0.0f, 0.999f);
        ridge.band_index = static_cast<int>(std::floor(normalized * 3.0f));
        line_state.ridges.push_back(ridge);
    }
}

float PleasureAnimation::random_between(float min_value, float max_value) {
    if (min_value > max_value) {
        std::swap(min_value, max_value);
    }
    std::uniform_real_distribution<float> dist(min_value, max_value);
    return dist(rng_);
}

void PleasureAnimation::load_parameters_from_config(const AnimationConfig& config_entry) {
    auto clamp_unit = [](float value) { return std::clamp(value, 0.0f, 1.0f); };

    params_.magnitude_scale = std::max(0.0f, config_entry.pleasure_magnitude_scale);
    params_.history_smoothing = clamp_unit(config_entry.pleasure_history_smoothing);
    params_.global_envelope_smoothing = clamp_unit(config_entry.pleasure_global_envelope_smoothing);
    params_.profile_smoothing = clamp_unit(config_entry.pleasure_profile_smoothing);
    params_.ridge_magnitude_smoothing = clamp_unit(config_entry.pleasure_ridge_magnitude_smoothing);
    params_.ridge_position_smoothing = clamp_unit(config_entry.pleasure_ridge_position_smoothing);
    params_.center_band_width = std::clamp(config_entry.pleasure_center_band_width, 0.0f, 1.0f);
    params_.ridge_sigma = std::max(config_entry.pleasure_ridge_sigma, 1e-4f);
    params_.ridge_position_jitter = std::max(0.0f, config_entry.pleasure_ridge_position_jitter);
    params_.ridge_magnitude_jitter = std::max(0.0f, config_entry.pleasure_ridge_magnitude_jitter);
    params_.ridge_interval_min = std::max(config_entry.pleasure_ridge_interval_min, 1e-3f);
    params_.ridge_interval_max =
        std::max(params_.ridge_interval_min, config_entry.pleasure_ridge_interval_max);
    params_.history_beat_boost = std::max(0.0f, config_entry.pleasure_history_beat_boost);
    params_.beat_response = std::max(0.0f, config_entry.pleasure_beat_response);
    params_.beat_attack_boost = std::max(0.0f, config_entry.pleasure_beat_attack_boost);
    params_.ridge_noise_acceleration = std::max(0.0f, config_entry.pleasure_ridge_noise_acceleration);
    params_.profile_noise_amount = std::max(0.0f, config_entry.pleasure_profile_noise_amount);
    params_.beat_phase_depth = std::max(0.0f, config_entry.pleasure_beat_phase_depth);
    params_.beat_phase_power = std::max(config_entry.pleasure_beat_phase_power, 1e-3f);
    params_.beat_pulse_attack = std::max(0.0f, config_entry.pleasure_beat_pulse_attack);
    params_.beat_pulse_release = std::max(0.0f, config_entry.pleasure_beat_pulse_release);
    params_.beat_phase_sway = config_entry.pleasure_beat_phase_sway;
    params_.downbeat_flash_strength = std::max(0.0f, config_entry.pleasure_downbeat_flash_strength);
    params_.downbeat_flash_decay = std::max(0.0f, config_entry.pleasure_downbeat_flash_decay);
    params_.global_headroom = std::max(config_entry.pleasure_global_headroom, 1.0f);
    params_.ridge_headroom = std::max(config_entry.pleasure_ridge_headroom, 1.0f);
    params_.profile_headroom = std::max(config_entry.pleasure_profile_headroom, 1.0f);
    params_.soft_clip_knee = std::clamp(config_entry.pleasure_soft_clip_knee, 0.01f, 0.99f);
    params_.band_beat_gain = std::max(0.0f, config_entry.pleasure_band_beat_gain);
    params_.band_beat_decay = std::max(0.0f, config_entry.pleasure_band_beat_decay);
    params_.band_reseed_jitter = std::max(0.0f, config_entry.pleasure_band_reseed_jitter);
    params_.highlight_flux_threshold = std::clamp(config_entry.pleasure_highlight_flux_threshold, 0.0f, 1.0f);
    params_.highlight_attack = std::max(0.0f, config_entry.pleasure_highlight_attack);
    params_.highlight_release = std::max(0.0f, config_entry.pleasure_highlight_release);
    params_.highlight_width = std::max(config_entry.pleasure_highlight_width, 1e-4f);
    params_.highlight_gain = std::max(0.0f, config_entry.pleasure_highlight_gain);
    params_.highlight_position_smoothing = std::max(0.0f, config_entry.pleasure_highlight_position_smoothing);
    params_.highlight_flatness_threshold = std::clamp(config_entry.pleasure_highlight_flatness_threshold, 0.0f, 1.0f);
    params_.highlight_tonal_bias = std::clamp(config_entry.pleasure_highlight_tonal_bias, 0.0f, 1.0f);

    const int min_ridges = std::max(1, config_entry.pleasure_min_ridges);
    const int max_ridges = std::max(min_ridges, config_entry.pleasure_max_ridges);
    params_.min_ridges = min_ridges;
    params_.max_ridges = max_ridges;

    params_.line_spacing = std::clamp(config_entry.pleasure_line_spacing, 1, 256);
    params_.max_lines = std::clamp(config_entry.pleasure_max_lines, 1, 512);
    params_.baseline_margin = std::max(0, config_entry.pleasure_baseline_margin);
    params_.max_upward_excursion = std::max(1, config_entry.pleasure_max_upward_excursion);
    params_.max_downward_excursion = std::max(0, config_entry.pleasure_max_downward_excursion);
}

void PleasureAnimation::draw_occluded_line(std::vector<uint8_t>& cells,
                                           unsigned int cell_rows,
                                           unsigned int cell_cols,
                                           int y1,
                                           int x1,
                                           int y2,
                                           int x2,
                                           std::vector<int>& skyline_buffer) {
    if (cells.empty() || cell_rows == 0u || cell_cols == 0u) {
        return;
    }

    const int pixel_rows = static_cast<int>(cell_rows) * kBrailleRowsPerCell;
    const int pixel_cols = static_cast<int>(cell_cols) * kBrailleColsPerCell;
    if (pixel_rows <= 0 || pixel_cols <= 0) {
        return;
    }

    if (skyline_buffer.size() != static_cast<std::size_t>(pixel_cols)) {
        skyline_buffer.assign(static_cast<std::size_t>(pixel_cols), pixel_rows);
    }

    auto clamp_coord = [](int value, int max_value) {
        if (max_value <= 0) {
            return 0;
        }
        if (value < 0) {
            return 0;
        }
        if (value >= max_value) {
            return max_value - 1;
        }
        return value;
    };

    x1 = clamp_coord(x1, pixel_cols);
    x2 = clamp_coord(x2, pixel_cols);
    y1 = clamp_coord(y1, pixel_rows);
    y2 = clamp_coord(y2, pixel_rows);

    int x = x1;
    int y = y1;
    const int dx = std::abs(x2 - x1);
    const int sx = (x1 < x2) ? 1 : ((x1 > x2) ? -1 : 0);
    const int dy = -std::abs(y2 - y1);
    const int sy = (y1 < y2) ? 1 : ((y1 > y2) ? -1 : 0);
    int err = dx + dy;

    while (true) {
        if (x >= 0 && x < pixel_cols && y >= 0 && y < pixel_rows) {
            const std::size_t skyline_index = static_cast<std::size_t>(x);
            if (skyline_index < skyline_buffer.size() && y < skyline_buffer[skyline_index]) {
                skyline_buffer[skyline_index] = y;
                set_braille_pixel(cells, cell_cols, y, x);
            }
        }

        if (x == x2 && y == y2) {
            break;
        }

        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

void PleasureAnimation::set_braille_pixel(std::vector<uint8_t>& cells,
                                          unsigned int cell_cols,
                                          int y,
                                          int x) const {
    if (cells.empty() || cell_cols == 0u || y < 0 || x < 0) {
        return;
    }

    const unsigned int cell_y = static_cast<unsigned int>(y / kBrailleRowsPerCell);
    const unsigned int cell_x = static_cast<unsigned int>(x / kBrailleColsPerCell);
    const unsigned int cell_rows = static_cast<unsigned int>(cells.size() / cell_cols);
    if (cell_y >= cell_rows || cell_x >= cell_cols) {
        return;
    }

    static constexpr uint8_t kDotMasks[kBrailleRowsPerCell][kBrailleColsPerCell] = {
        {0x01, 0x08},
        {0x02, 0x10},
        {0x04, 0x20},
        {0x40, 0x80},
    };

    const int sub_y = y % kBrailleRowsPerCell;
    const int sub_x = x % kBrailleColsPerCell;
    const unsigned int index = cell_y * cell_cols + cell_x;
    cells[index] |= kDotMasks[sub_y][sub_x];
}

void PleasureAnimation::blit_braille_cells(ncplane* plane,
                                           const std::vector<uint8_t>& cells,
                                           unsigned int cell_rows,
                                           unsigned int cell_cols) const {
    if (!plane || cells.empty() || cell_rows == 0u || cell_cols == 0u) {
        return;
    }

    const unsigned int expected_size = cell_rows * cell_cols;
    if (cells.size() < expected_size) {
        return;
    }

    for (unsigned int row = 0; row < cell_rows; ++row) {
        for (unsigned int col = 0; col < cell_cols; ++col) {
            const uint8_t mask = cells[row * cell_cols + col];
            if (mask == 0u) {
                continue;
            }

            const wchar_t ch = static_cast<wchar_t>(0x2800u + mask);
            ncplane_putwc_yx(plane, row, col, ch);
        }
    }
}

void PleasureAnimation::create_or_resize_plane(notcurses* nc) {
    if (!nc) {
        return;
    }

    ncplane* stdplane = notcurses_stdplane(nc);
    if (!stdplane) {
        return;
    }

    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }

    if (plane_rows_ == 0u || plane_cols_ == 0u) {
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
    }
}

void PleasureAnimation::configure_history_capacity() {
    history_capacity_ = 0u;

    if (plane_) {
        const std::size_t pixel_cols = static_cast<std::size_t>(plane_cols_) *
                                       static_cast<std::size_t>(kBrailleColsPerCell);
        history_capacity_ = std::max<std::size_t>(2u, pixel_cols);
    }

    initialize_line_states();
}

} // namespace animations
} // namespace when

