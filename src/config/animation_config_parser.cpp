#include "animation_config_parser.h"

#include <sstream>

#include "value_parsers.h"

namespace when::config::detail {

std::optional<AnimationConfig> parse_animation_config(
    const std::unordered_map<std::string, RawScalar>& raw_anim_config,
    std::vector<std::string>& warnings) {
    AnimationConfig anim_config;

    const auto type_it = raw_anim_config.find("type");
    if (type_it != raw_anim_config.end()) {
        anim_config.type = sanitize_string_value(type_it->second.value);
    } else {
        std::ostringstream oss;
        oss << "Animation configuration missing 'type' for an entry.";
        warnings.push_back(oss.str());
        return std::nullopt;
    }

    const auto z_index_it = raw_anim_config.find("z_index");
    if (z_index_it != raw_anim_config.end()) {
        parse_int32(z_index_it->second.value, anim_config.z_index);
    }

    const auto initially_active_it = raw_anim_config.find("initially_active");
    if (initially_active_it != raw_anim_config.end()) {
        parse_bool(initially_active_it->second.value, anim_config.initially_active);
    }

    const auto trigger_band_index_it = raw_anim_config.find("trigger_band_index");
    if (trigger_band_index_it != raw_anim_config.end()) {
        parse_int32(trigger_band_index_it->second.value, anim_config.trigger_band_index);
    }

    const auto trigger_threshold_it = raw_anim_config.find("trigger_threshold");
    if (trigger_threshold_it != raw_anim_config.end()) {
        parse_float32(trigger_threshold_it->second.value, anim_config.trigger_threshold);
    }

    const auto trigger_beat_min_it = raw_anim_config.find("trigger_beat_min");
    if (trigger_beat_min_it != raw_anim_config.end()) {
        parse_float32(trigger_beat_min_it->second.value, anim_config.trigger_beat_min);
    }

    const auto trigger_beat_max_it = raw_anim_config.find("trigger_beat_max");
    if (trigger_beat_max_it != raw_anim_config.end()) {
        parse_float32(trigger_beat_max_it->second.value, anim_config.trigger_beat_max);
    }

    const auto text_file_path_it = raw_anim_config.find("text_file_path");
    if (text_file_path_it != raw_anim_config.end()) {
        anim_config.text_file_path = sanitize_string_value(text_file_path_it->second.value);
    }

    const auto type_speed_it = raw_anim_config.find("type_speed_words_per_s");
    if (type_speed_it != raw_anim_config.end()) {
        parse_float32(type_speed_it->second.value, anim_config.type_speed_words_per_s);
    }

    const auto display_duration_it = raw_anim_config.find("display_duration_s");
    if (display_duration_it != raw_anim_config.end()) {
        parse_float32(display_duration_it->second.value, anim_config.display_duration_s);
    }

    const auto fade_duration_it = raw_anim_config.find("fade_duration_s");
    if (fade_duration_it != raw_anim_config.end()) {
        parse_float32(fade_duration_it->second.value, anim_config.fade_duration_s);
    }

    const auto trigger_cooldown_it = raw_anim_config.find("trigger_cooldown_s");
    if (trigger_cooldown_it != raw_anim_config.end()) {
        parse_float32(trigger_cooldown_it->second.value, anim_config.trigger_cooldown_s);
    }

    const auto max_lines_it = raw_anim_config.find("max_active_lines");
    if (max_lines_it != raw_anim_config.end()) {
        parse_int32(max_lines_it->second.value, anim_config.max_active_lines);
    }

    const auto min_y_ratio_it = raw_anim_config.find("random_text_min_y_ratio");
    if (min_y_ratio_it != raw_anim_config.end()) {
        parse_float32(min_y_ratio_it->second.value, anim_config.random_text_min_y_ratio);
    }

    const auto max_y_ratio_it = raw_anim_config.find("random_text_max_y_ratio");
    if (max_y_ratio_it != raw_anim_config.end()) {
        parse_float32(max_y_ratio_it->second.value, anim_config.random_text_max_y_ratio);
    }

    const auto log_interval_it = raw_anim_config.find("log_line_interval_s");
    if (log_interval_it != raw_anim_config.end()) {
        parse_float32(log_interval_it->second.value, anim_config.log_line_interval_s);
    }

    const auto log_loop_it = raw_anim_config.find("log_loop_messages");
    if (log_loop_it != raw_anim_config.end()) {
        parse_bool(log_loop_it->second.value, anim_config.log_loop_messages);
    }

    const auto log_border_it = raw_anim_config.find("log_show_border");
    if (log_border_it != raw_anim_config.end()) {
        parse_bool(log_border_it->second.value, anim_config.log_show_border);
    }

    const auto log_padding_y_it = raw_anim_config.find("log_padding_y");
    if (log_padding_y_it != raw_anim_config.end()) {
        parse_int32(log_padding_y_it->second.value, anim_config.log_padding_y);
    }

    const auto log_padding_x_it = raw_anim_config.find("log_padding_x");
    if (log_padding_x_it != raw_anim_config.end()) {
        parse_int32(log_padding_x_it->second.value, anim_config.log_padding_x);
    }

    const auto log_title_it = raw_anim_config.find("log_title");
    if (log_title_it != raw_anim_config.end()) {
        anim_config.log_title = sanitize_string_value(log_title_it->second.value);
    }

    const auto plane_y_it = raw_anim_config.find("plane_y");
    if (plane_y_it != raw_anim_config.end()) {
        int value = 0;
        if (parse_int32(plane_y_it->second.value, value)) {
            anim_config.plane_y = value;
        }
    }

    const auto plane_x_it = raw_anim_config.find("plane_x");
    if (plane_x_it != raw_anim_config.end()) {
        int value = 0;
        if (parse_int32(plane_x_it->second.value, value)) {
            anim_config.plane_x = value;
        }
    }

    const auto plane_rows_it = raw_anim_config.find("plane_rows");
    if (plane_rows_it != raw_anim_config.end()) {
        int value = 0;
        if (parse_int32(plane_rows_it->second.value, value)) {
            anim_config.plane_rows = value;
        }
    }

    const auto plane_cols_it = raw_anim_config.find("plane_cols");
    if (plane_cols_it != raw_anim_config.end()) {
        int value = 0;
        if (parse_int32(plane_cols_it->second.value, value)) {
            anim_config.plane_cols = value;
        }
    }

    const auto matrix_rows_it = raw_anim_config.find("matrix_rows");
    if (matrix_rows_it != raw_anim_config.end()) {
        int value = 0;
        if (parse_int32(matrix_rows_it->second.value, value)) {
            anim_config.matrix_rows = value;
        }
    }

    const auto matrix_cols_it = raw_anim_config.find("matrix_cols");
    if (matrix_cols_it != raw_anim_config.end()) {
        int value = 0;
        if (parse_int32(matrix_cols_it->second.value, value)) {
            anim_config.matrix_cols = value;
        }
    }

    const auto matrix_border_it = raw_anim_config.find("matrix_show_border");
    if (matrix_border_it != raw_anim_config.end()) {
        parse_bool(matrix_border_it->second.value, anim_config.matrix_show_border);
    }

    const auto glyphs_file_it = raw_anim_config.find("glyphs_file_path");
    if (glyphs_file_it != raw_anim_config.end()) {
        anim_config.glyphs_file_path = sanitize_string_value(glyphs_file_it->second.value);
    }

    const auto beat_boost_it = raw_anim_config.find("matrix_beat_boost");
    if (beat_boost_it != raw_anim_config.end()) {
        parse_float32(beat_boost_it->second.value, anim_config.matrix_beat_boost);
    }

    const auto beat_threshold_it = raw_anim_config.find("matrix_beat_threshold");
    if (beat_threshold_it != raw_anim_config.end()) {
        parse_float32(beat_threshold_it->second.value, anim_config.matrix_beat_threshold);
    }

    const auto space_rock_spawn_base_count_it =
        raw_anim_config.find("space_rock_spawn_base_count");
    if (space_rock_spawn_base_count_it != raw_anim_config.end()) {
        parse_int32(space_rock_spawn_base_count_it->second.value,
                    anim_config.space_rock_spawn_base_count);
    }

    const auto space_rock_spawn_strength_scale_it =
        raw_anim_config.find("space_rock_spawn_strength_scale");
    if (space_rock_spawn_strength_scale_it != raw_anim_config.end()) {
        parse_float32(space_rock_spawn_strength_scale_it->second.value,
                      anim_config.space_rock_spawn_strength_scale);
    }

    const auto space_rock_square_lifespan_ms_it =
        raw_anim_config.find("space_rock_square_lifespan_ms");
    if (space_rock_square_lifespan_ms_it != raw_anim_config.end()) {
        parse_float32(space_rock_square_lifespan_ms_it->second.value,
                      anim_config.space_rock_square_lifespan_ms);
    }

    const auto space_rock_square_decay_rate_it =
        raw_anim_config.find("space_rock_square_decay_rate");
    if (space_rock_square_decay_rate_it != raw_anim_config.end()) {
        parse_float32(space_rock_square_decay_rate_it->second.value,
                      anim_config.space_rock_square_decay_rate);
    }

    const auto space_rock_max_squares_floor_it =
        raw_anim_config.find("space_rock_max_squares_floor");
    if (space_rock_max_squares_floor_it != raw_anim_config.end()) {
        parse_int32(space_rock_max_squares_floor_it->second.value,
                    anim_config.space_rock_max_squares_floor);
    }

    const auto space_rock_max_squares_scale_it =
        raw_anim_config.find("space_rock_max_squares_scale");
    if (space_rock_max_squares_scale_it != raw_anim_config.end()) {
        parse_float32(space_rock_max_squares_scale_it->second.value,
                      anim_config.space_rock_max_squares_scale);
    }

    const auto space_rock_min_size_it = raw_anim_config.find("space_rock_min_size");
    if (space_rock_min_size_it != raw_anim_config.end()) {
        parse_float32(space_rock_min_size_it->second.value, anim_config.space_rock_min_size);
    }

    const auto space_rock_max_size_it = raw_anim_config.find("space_rock_max_size");
    if (space_rock_max_size_it != raw_anim_config.end()) {
        parse_float32(space_rock_max_size_it->second.value, anim_config.space_rock_max_size);
    }

    const auto space_rock_mid_beat_multiplier_it =
        raw_anim_config.find("space_rock_mid_beat_size_multiplier");
    if (space_rock_mid_beat_multiplier_it != raw_anim_config.end()) {
        parse_float32(space_rock_mid_beat_multiplier_it->second.value,
                      anim_config.space_rock_mid_beat_size_multiplier);
    }

    const auto space_rock_bass_size_scale_it =
        raw_anim_config.find("space_rock_bass_size_scale");
    if (space_rock_bass_size_scale_it != raw_anim_config.end()) {
        parse_float32(space_rock_bass_size_scale_it->second.value,
                      anim_config.space_rock_bass_size_scale);
    }

    const auto space_rock_treble_size_scale_it =
        raw_anim_config.find("space_rock_treble_size_scale");
    if (space_rock_treble_size_scale_it != raw_anim_config.end()) {
        parse_float32(space_rock_treble_size_scale_it->second.value,
                      anim_config.space_rock_treble_size_scale);
    }

    const auto space_rock_treble_spawn_threshold_it =
        raw_anim_config.find("space_rock_treble_spawn_threshold");
    if (space_rock_treble_spawn_threshold_it != raw_anim_config.end()) {
        parse_float32(space_rock_treble_spawn_threshold_it->second.value,
                      anim_config.space_rock_treble_spawn_threshold);
    }

    const auto space_rock_low_band_min_y_it =
        raw_anim_config.find("space_rock_low_band_min_y");
    if (space_rock_low_band_min_y_it != raw_anim_config.end()) {
        parse_float32(space_rock_low_band_min_y_it->second.value,
                      anim_config.space_rock_low_band_min_y);
    }

    const auto space_rock_low_band_max_y_it =
        raw_anim_config.find("space_rock_low_band_max_y");
    if (space_rock_low_band_max_y_it != raw_anim_config.end()) {
        parse_float32(space_rock_low_band_max_y_it->second.value,
                      anim_config.space_rock_low_band_max_y);
    }

    const auto space_rock_high_band_min_y_it =
        raw_anim_config.find("space_rock_high_band_min_y");
    if (space_rock_high_band_min_y_it != raw_anim_config.end()) {
        parse_float32(space_rock_high_band_min_y_it->second.value,
                      anim_config.space_rock_high_band_min_y);
    }

    const auto space_rock_high_band_max_y_it =
        raw_anim_config.find("space_rock_high_band_max_y");
    if (space_rock_high_band_max_y_it != raw_anim_config.end()) {
        parse_float32(space_rock_high_band_max_y_it->second.value,
                      anim_config.space_rock_high_band_max_y);
    }

    const auto space_rock_size_interp_rate_it =
        raw_anim_config.find("space_rock_size_interp_rate");
    if (space_rock_size_interp_rate_it != raw_anim_config.end()) {
        parse_float32(space_rock_size_interp_rate_it->second.value,
                      anim_config.space_rock_size_interp_rate);
    }

    const auto space_rock_max_jitter_it = raw_anim_config.find("space_rock_max_jitter");
    if (space_rock_max_jitter_it != raw_anim_config.end()) {
        parse_float32(space_rock_max_jitter_it->second.value, anim_config.space_rock_max_jitter);
    }

    const auto space_rock_position_interp_rate_it =
        raw_anim_config.find("space_rock_position_interp_rate");
    if (space_rock_position_interp_rate_it != raw_anim_config.end()) {
        parse_float32(space_rock_position_interp_rate_it->second.value,
                      anim_config.space_rock_position_interp_rate);
    }

    const auto rain_angle_it = raw_anim_config.find("rain_angle_degrees");
    if (rain_angle_it != raw_anim_config.end()) {
        parse_float32(rain_angle_it->second.value, anim_config.rain_angle_degrees);
    }

    const auto wave_speed_it = raw_anim_config.find("wave_speed_cols_per_s");
    if (wave_speed_it != raw_anim_config.end()) {
        parse_float32(wave_speed_it->second.value, anim_config.wave_speed_cols_per_s);
    }

    const auto wave_front_it = raw_anim_config.find("wave_front_width_cols");
    if (wave_front_it != raw_anim_config.end()) {
        parse_int32(wave_front_it->second.value, anim_config.wave_front_width_cols);
    }

    const auto wave_tail_it = raw_anim_config.find("wave_tail_length_cols");
    if (wave_tail_it != raw_anim_config.end()) {
        parse_int32(wave_tail_it->second.value, anim_config.wave_tail_length_cols);
    }

    const auto wave_alternate_it = raw_anim_config.find("wave_alternate_direction");
    if (wave_alternate_it != raw_anim_config.end()) {
        parse_bool(wave_alternate_it->second.value, anim_config.wave_alternate_direction);
    }

    const auto wave_direction_it = raw_anim_config.find("wave_direction_right");
    if (wave_direction_it != raw_anim_config.end()) {
        parse_bool(wave_direction_it->second.value, anim_config.wave_direction_right);
    }

    const auto lightning_novelty_threshold_it =
        raw_anim_config.find("lightning_novelty_threshold");
    if (lightning_novelty_threshold_it != raw_anim_config.end()) {
        parse_float32(lightning_novelty_threshold_it->second.value,
                      anim_config.lightning_novelty_threshold);
    }

    const auto lightning_energy_floor_it = raw_anim_config.find("lightning_energy_floor");
    if (lightning_energy_floor_it != raw_anim_config.end()) {
        parse_float32(lightning_energy_floor_it->second.value, anim_config.lightning_energy_floor);
    }

    const auto lightning_detection_cooldown_it =
        raw_anim_config.find("lightning_detection_cooldown_s");
    if (lightning_detection_cooldown_it != raw_anim_config.end()) {
        parse_float32(lightning_detection_cooldown_it->second.value,
                      anim_config.lightning_detection_cooldown_s);
    }

    const auto lightning_novelty_smoothing_it =
        raw_anim_config.find("lightning_novelty_smoothing_s");
    if (lightning_novelty_smoothing_it != raw_anim_config.end()) {
        parse_float32(lightning_novelty_smoothing_it->second.value,
                      anim_config.lightning_novelty_smoothing_s);
    }

    const auto lightning_background_smoothing_it =
        raw_anim_config.find("lightning_background_smoothing_s");
    if (lightning_background_smoothing_it != raw_anim_config.end()) {
        parse_float32(lightning_background_smoothing_it->second.value,
                      anim_config.lightning_background_smoothing_s);
    }

    const auto lightning_activation_decay_it =
        raw_anim_config.find("lightning_activation_decay_s");
    if (lightning_activation_decay_it != raw_anim_config.end()) {
        parse_float32(lightning_activation_decay_it->second.value,
                      anim_config.lightning_activation_decay_s);
    }

    const auto breathe_points_it = raw_anim_config.find("breathe_points");
    if (breathe_points_it != raw_anim_config.end()) {
        parse_int32(breathe_points_it->second.value, anim_config.breathe_points);
    }

    const auto breathe_min_radius_it = raw_anim_config.find("breathe_min_radius");
    if (breathe_min_radius_it != raw_anim_config.end()) {
        parse_float32(breathe_min_radius_it->second.value, anim_config.breathe_min_radius);
    }

    const auto breathe_max_radius_it = raw_anim_config.find("breathe_max_radius");
    if (breathe_max_radius_it != raw_anim_config.end()) {
        parse_float32(breathe_max_radius_it->second.value, anim_config.breathe_max_radius);
    }

    const auto breathe_radius_influence_it =
        raw_anim_config.find("breathe_audio_radius_influence");
    if (breathe_radius_influence_it != raw_anim_config.end()) {
        parse_float32(breathe_radius_influence_it->second.value,
                      anim_config.breathe_audio_radius_influence);
    }

    const auto breathe_smoothing_it = raw_anim_config.find("breathe_smoothing_s");
    if (breathe_smoothing_it != raw_anim_config.end()) {
        parse_float32(breathe_smoothing_it->second.value, anim_config.breathe_smoothing_s);
    }

    const auto breathe_noise_it = raw_anim_config.find("breathe_noise_amount");
    if (breathe_noise_it != raw_anim_config.end()) {
        parse_float32(breathe_noise_it->second.value, anim_config.breathe_noise_amount);
    }

    const auto breathe_rotation_it = raw_anim_config.find("breathe_rotation_speed");
    if (breathe_rotation_it != raw_anim_config.end()) {
        parse_float32(breathe_rotation_it->second.value, anim_config.breathe_rotation_speed);
    }

    const auto breathe_vertical_it = raw_anim_config.find("breathe_vertical_scale");
    if (breathe_vertical_it != raw_anim_config.end()) {
        parse_float32(breathe_vertical_it->second.value, anim_config.breathe_vertical_scale);
    }

    const auto breathe_base_pulse_it = raw_anim_config.find("breathe_base_pulse_hz");
    if (breathe_base_pulse_it != raw_anim_config.end()) {
        parse_float32(breathe_base_pulse_it->second.value, anim_config.breathe_base_pulse_hz);
    }

    const auto breathe_pulse_weight_it = raw_anim_config.find("breathe_audio_pulse_weight");
    if (breathe_pulse_weight_it != raw_anim_config.end()) {
        parse_float32(breathe_pulse_weight_it->second.value, anim_config.breathe_audio_pulse_weight);
    }

    const auto breathe_band_index_it = raw_anim_config.find("breathe_band_index");
    if (breathe_band_index_it != raw_anim_config.end()) {
        parse_int32(breathe_band_index_it->second.value, anim_config.breathe_band_index);
    }

    const auto breathe_rms_weight_it = raw_anim_config.find("breathe_rms_weight");
    if (breathe_rms_weight_it != raw_anim_config.end()) {
        parse_float32(breathe_rms_weight_it->second.value, anim_config.breathe_rms_weight);
    }

    const auto breathe_beat_weight_it = raw_anim_config.find("breathe_beat_weight");
    if (breathe_beat_weight_it != raw_anim_config.end()) {
        parse_float32(breathe_beat_weight_it->second.value, anim_config.breathe_beat_weight);
    }

    const auto breathe_band_weight_it = raw_anim_config.find("breathe_band_weight");
    if (breathe_band_weight_it != raw_anim_config.end()) {
        parse_float32(breathe_band_weight_it->second.value, anim_config.breathe_band_weight);
    }

    const auto pleasure_magnitude_scale_it = raw_anim_config.find("pleasure_magnitude_scale");
    if (pleasure_magnitude_scale_it != raw_anim_config.end()) {
        parse_float32(pleasure_magnitude_scale_it->second.value, anim_config.pleasure_magnitude_scale);
    }

    const auto pleasure_history_smoothing_it = raw_anim_config.find("pleasure_history_smoothing");
    if (pleasure_history_smoothing_it != raw_anim_config.end()) {
        parse_float32(pleasure_history_smoothing_it->second.value, anim_config.pleasure_history_smoothing);
    }

    const auto pleasure_global_envelope_smoothing_it =
        raw_anim_config.find("pleasure_global_envelope_smoothing");
    if (pleasure_global_envelope_smoothing_it != raw_anim_config.end()) {
        parse_float32(pleasure_global_envelope_smoothing_it->second.value,
                      anim_config.pleasure_global_envelope_smoothing);
    }

    const auto pleasure_profile_smoothing_it = raw_anim_config.find("pleasure_profile_smoothing");
    if (pleasure_profile_smoothing_it != raw_anim_config.end()) {
        parse_float32(pleasure_profile_smoothing_it->second.value, anim_config.pleasure_profile_smoothing);
    }

    const auto pleasure_ridge_magnitude_smoothing_it =
        raw_anim_config.find("pleasure_ridge_magnitude_smoothing");
    if (pleasure_ridge_magnitude_smoothing_it != raw_anim_config.end()) {
        parse_float32(pleasure_ridge_magnitude_smoothing_it->second.value,
                      anim_config.pleasure_ridge_magnitude_smoothing);
    }

    const auto pleasure_ridge_position_smoothing_it =
        raw_anim_config.find("pleasure_ridge_position_smoothing");
    if (pleasure_ridge_position_smoothing_it != raw_anim_config.end()) {
        parse_float32(pleasure_ridge_position_smoothing_it->second.value,
                      anim_config.pleasure_ridge_position_smoothing);
    }

    const auto pleasure_center_band_width_it = raw_anim_config.find("pleasure_center_band_width");
    if (pleasure_center_band_width_it != raw_anim_config.end()) {
        parse_float32(pleasure_center_band_width_it->second.value, anim_config.pleasure_center_band_width);
    }

    const auto pleasure_ridge_sigma_it = raw_anim_config.find("pleasure_ridge_sigma");
    if (pleasure_ridge_sigma_it != raw_anim_config.end()) {
        parse_float32(pleasure_ridge_sigma_it->second.value, anim_config.pleasure_ridge_sigma);
    }

    const auto pleasure_ridge_position_jitter_it =
        raw_anim_config.find("pleasure_ridge_position_jitter");
    if (pleasure_ridge_position_jitter_it != raw_anim_config.end()) {
        parse_float32(pleasure_ridge_position_jitter_it->second.value,
                      anim_config.pleasure_ridge_position_jitter);
    }

    const auto pleasure_ridge_magnitude_jitter_it =
        raw_anim_config.find("pleasure_ridge_magnitude_jitter");
    if (pleasure_ridge_magnitude_jitter_it != raw_anim_config.end()) {
        parse_float32(pleasure_ridge_magnitude_jitter_it->second.value,
                      anim_config.pleasure_ridge_magnitude_jitter);
    }

    const auto pleasure_ridge_interval_min_it = raw_anim_config.find("pleasure_ridge_interval_min");
    if (pleasure_ridge_interval_min_it != raw_anim_config.end()) {
        parse_float32(pleasure_ridge_interval_min_it->second.value,
                      anim_config.pleasure_ridge_interval_min);
    }

    const auto pleasure_ridge_interval_max_it = raw_anim_config.find("pleasure_ridge_interval_max");
    if (pleasure_ridge_interval_max_it != raw_anim_config.end()) {
        parse_float32(pleasure_ridge_interval_max_it->second.value,
                      anim_config.pleasure_ridge_interval_max);
    }
    const auto pleasure_history_beat_boost_it = raw_anim_config.find("pleasure_history_beat_boost");
    if (pleasure_history_beat_boost_it != raw_anim_config.end()) {
        parse_float32(pleasure_history_beat_boost_it->second.value,
                      anim_config.pleasure_history_beat_boost);
    }
    const auto pleasure_beat_response_it = raw_anim_config.find("pleasure_beat_response");
    if (pleasure_beat_response_it != raw_anim_config.end()) {
        parse_float32(pleasure_beat_response_it->second.value, anim_config.pleasure_beat_response);
    }
    const auto pleasure_beat_attack_boost_it = raw_anim_config.find("pleasure_beat_attack_boost");
    if (pleasure_beat_attack_boost_it != raw_anim_config.end()) {
        parse_float32(pleasure_beat_attack_boost_it->second.value,
                      anim_config.pleasure_beat_attack_boost);
    }
    const auto pleasure_ridge_noise_acceleration_it =
        raw_anim_config.find("pleasure_ridge_noise_acceleration");
    if (pleasure_ridge_noise_acceleration_it != raw_anim_config.end()) {
        parse_float32(pleasure_ridge_noise_acceleration_it->second.value,
                      anim_config.pleasure_ridge_noise_acceleration);
    }
    const auto pleasure_profile_noise_amount_it = raw_anim_config.find("pleasure_profile_noise_amount");
    if (pleasure_profile_noise_amount_it != raw_anim_config.end()) {
        parse_float32(pleasure_profile_noise_amount_it->second.value,
                      anim_config.pleasure_profile_noise_amount);
    }
    const auto pleasure_beat_phase_depth_it = raw_anim_config.find("pleasure_beat_phase_depth");
    if (pleasure_beat_phase_depth_it != raw_anim_config.end()) {
        parse_float32(pleasure_beat_phase_depth_it->second.value, anim_config.pleasure_beat_phase_depth);
    }
    const auto pleasure_beat_phase_power_it = raw_anim_config.find("pleasure_beat_phase_power");
    if (pleasure_beat_phase_power_it != raw_anim_config.end()) {
        parse_float32(pleasure_beat_phase_power_it->second.value, anim_config.pleasure_beat_phase_power);
    }
    const auto pleasure_beat_pulse_attack_it = raw_anim_config.find("pleasure_beat_pulse_attack");
    if (pleasure_beat_pulse_attack_it != raw_anim_config.end()) {
        parse_float32(pleasure_beat_pulse_attack_it->second.value, anim_config.pleasure_beat_pulse_attack);
    }
    const auto pleasure_beat_pulse_release_it = raw_anim_config.find("pleasure_beat_pulse_release");
    if (pleasure_beat_pulse_release_it != raw_anim_config.end()) {
        parse_float32(pleasure_beat_pulse_release_it->second.value, anim_config.pleasure_beat_pulse_release);
    }
    const auto pleasure_beat_phase_sway_it = raw_anim_config.find("pleasure_beat_phase_sway");
    if (pleasure_beat_phase_sway_it != raw_anim_config.end()) {
        parse_float32(pleasure_beat_phase_sway_it->second.value, anim_config.pleasure_beat_phase_sway);
    }
    const auto pleasure_downbeat_flash_strength_it = raw_anim_config.find("pleasure_downbeat_flash_strength");
    if (pleasure_downbeat_flash_strength_it != raw_anim_config.end()) {
        parse_float32(pleasure_downbeat_flash_strength_it->second.value,
                      anim_config.pleasure_downbeat_flash_strength);
    }
    const auto pleasure_downbeat_flash_decay_it = raw_anim_config.find("pleasure_downbeat_flash_decay");
    if (pleasure_downbeat_flash_decay_it != raw_anim_config.end()) {
        parse_float32(pleasure_downbeat_flash_decay_it->second.value, anim_config.pleasure_downbeat_flash_decay);
    }
    const auto pleasure_global_headroom_it = raw_anim_config.find("pleasure_global_headroom");
    if (pleasure_global_headroom_it != raw_anim_config.end()) {
        parse_float32(pleasure_global_headroom_it->second.value, anim_config.pleasure_global_headroom);
    }
    const auto pleasure_ridge_headroom_it = raw_anim_config.find("pleasure_ridge_headroom");
    if (pleasure_ridge_headroom_it != raw_anim_config.end()) {
        parse_float32(pleasure_ridge_headroom_it->second.value, anim_config.pleasure_ridge_headroom);
    }
    const auto pleasure_profile_headroom_it = raw_anim_config.find("pleasure_profile_headroom");
    if (pleasure_profile_headroom_it != raw_anim_config.end()) {
        parse_float32(pleasure_profile_headroom_it->second.value, anim_config.pleasure_profile_headroom);
    }
    const auto pleasure_soft_clip_knee_it = raw_anim_config.find("pleasure_soft_clip_knee");
    if (pleasure_soft_clip_knee_it != raw_anim_config.end()) {
        parse_float32(pleasure_soft_clip_knee_it->second.value, anim_config.pleasure_soft_clip_knee);
    }
    const auto pleasure_band_beat_gain_it = raw_anim_config.find("pleasure_band_beat_gain");
    if (pleasure_band_beat_gain_it != raw_anim_config.end()) {
        parse_float32(pleasure_band_beat_gain_it->second.value, anim_config.pleasure_band_beat_gain);
    }
    const auto pleasure_band_beat_decay_it = raw_anim_config.find("pleasure_band_beat_decay");
    if (pleasure_band_beat_decay_it != raw_anim_config.end()) {
        parse_float32(pleasure_band_beat_decay_it->second.value, anim_config.pleasure_band_beat_decay);
    }
    const auto pleasure_band_reseed_jitter_it = raw_anim_config.find("pleasure_band_reseed_jitter");
    if (pleasure_band_reseed_jitter_it != raw_anim_config.end()) {
        parse_float32(pleasure_band_reseed_jitter_it->second.value, anim_config.pleasure_band_reseed_jitter);
    }
    const auto pleasure_highlight_flux_threshold_it = raw_anim_config.find("pleasure_highlight_flux_threshold");
    if (pleasure_highlight_flux_threshold_it != raw_anim_config.end()) {
        parse_float32(pleasure_highlight_flux_threshold_it->second.value,
                      anim_config.pleasure_highlight_flux_threshold);
    }
    const auto pleasure_highlight_attack_it = raw_anim_config.find("pleasure_highlight_attack");
    if (pleasure_highlight_attack_it != raw_anim_config.end()) {
        parse_float32(pleasure_highlight_attack_it->second.value, anim_config.pleasure_highlight_attack);
    }
    const auto pleasure_highlight_release_it = raw_anim_config.find("pleasure_highlight_release");
    if (pleasure_highlight_release_it != raw_anim_config.end()) {
        parse_float32(pleasure_highlight_release_it->second.value, anim_config.pleasure_highlight_release);
    }
    const auto pleasure_highlight_width_it = raw_anim_config.find("pleasure_highlight_width");
    if (pleasure_highlight_width_it != raw_anim_config.end()) {
        parse_float32(pleasure_highlight_width_it->second.value, anim_config.pleasure_highlight_width);
    }
    const auto pleasure_highlight_gain_it = raw_anim_config.find("pleasure_highlight_gain");
    if (pleasure_highlight_gain_it != raw_anim_config.end()) {
        parse_float32(pleasure_highlight_gain_it->second.value, anim_config.pleasure_highlight_gain);
    }
    const auto pleasure_highlight_position_smoothing_it =
        raw_anim_config.find("pleasure_highlight_position_smoothing");
    if (pleasure_highlight_position_smoothing_it != raw_anim_config.end()) {
        parse_float32(pleasure_highlight_position_smoothing_it->second.value,
                      anim_config.pleasure_highlight_position_smoothing);
    }
    const auto pleasure_highlight_flatness_threshold_it =
        raw_anim_config.find("pleasure_highlight_flatness_threshold");
    if (pleasure_highlight_flatness_threshold_it != raw_anim_config.end()) {
        parse_float32(pleasure_highlight_flatness_threshold_it->second.value,
                      anim_config.pleasure_highlight_flatness_threshold);
    }
    const auto pleasure_highlight_tonal_bias_it = raw_anim_config.find("pleasure_highlight_tonal_bias");
    if (pleasure_highlight_tonal_bias_it != raw_anim_config.end()) {
        parse_float32(pleasure_highlight_tonal_bias_it->second.value, anim_config.pleasure_highlight_tonal_bias);
    }

    const auto pleasure_min_ridges_it = raw_anim_config.find("pleasure_min_ridges");
    if (pleasure_min_ridges_it != raw_anim_config.end()) {
        parse_int32(pleasure_min_ridges_it->second.value, anim_config.pleasure_min_ridges);
    }

    const auto pleasure_max_ridges_it = raw_anim_config.find("pleasure_max_ridges");
    if (pleasure_max_ridges_it != raw_anim_config.end()) {
        parse_int32(pleasure_max_ridges_it->second.value, anim_config.pleasure_max_ridges);
    }

    const auto pleasure_line_spacing_it = raw_anim_config.find("pleasure_line_spacing");
    if (pleasure_line_spacing_it != raw_anim_config.end()) {
        parse_int32(pleasure_line_spacing_it->second.value, anim_config.pleasure_line_spacing);
    }

    const auto pleasure_max_lines_it = raw_anim_config.find("pleasure_max_lines");
    if (pleasure_max_lines_it != raw_anim_config.end()) {
        parse_int32(pleasure_max_lines_it->second.value, anim_config.pleasure_max_lines);
    }

    const auto pleasure_baseline_margin_it = raw_anim_config.find("pleasure_baseline_margin");
    if (pleasure_baseline_margin_it != raw_anim_config.end()) {
        parse_int32(pleasure_baseline_margin_it->second.value, anim_config.pleasure_baseline_margin);
    }

    const auto pleasure_max_upward_excursion_it =
        raw_anim_config.find("pleasure_max_upward_excursion");
    if (pleasure_max_upward_excursion_it != raw_anim_config.end()) {
        parse_int32(pleasure_max_upward_excursion_it->second.value,
                    anim_config.pleasure_max_upward_excursion);
    }

    const auto pleasure_max_downward_excursion_it =
        raw_anim_config.find("pleasure_max_downward_excursion");
    if (pleasure_max_downward_excursion_it != raw_anim_config.end()) {
        parse_int32(pleasure_max_downward_excursion_it->second.value,
                    anim_config.pleasure_max_downward_excursion);
    }

    return anim_config;
}

} // namespace when::config::detail

