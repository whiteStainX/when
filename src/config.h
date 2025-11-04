#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>



namespace why {

struct AudioCaptureConfig {
    bool enabled = true;
    std::uint32_t sample_rate = 48000;
    std::uint32_t channels = 2;
    std::size_t ring_frames = 8192;
    std::string device;
    float input_gain = 1.0f;
    bool system = false;
};

struct AudioFileConfig {
    bool enabled = true;
    std::string path;
    std::uint32_t channels = 1;
    float gain = 1.0f;
};

struct AudioConfig {
    AudioCaptureConfig capture;
    AudioFileConfig file;
    bool prefer_file = false;
};

struct DspConfig {
    std::size_t fft_size = 1024;
    std::size_t hop_size = 256;
    std::size_t bands = 32;
    std::string window = "hann";
    float smoothing_attack = 0.2f;
    float smoothing_release = 0.05f;
    float beat_sensitivity = 1.0f;
    bool enable_flux = true;
};

struct VisualConfig {

    double target_fps = 60.0;

};

struct RuntimeConfig {
    bool show_metrics = true;
    bool allow_resize = true;
    bool beat_flash = true;
    bool show_overlay_metrics = false; // New config option, default to false
};

struct PluginConfig {
    std::string directory = "plugins";
    std::vector<std::string> autoload;
    bool safe_mode = false;
};

struct AnimationConfig {
    std::string type;
    int z_index = 0;
    bool initially_active = true; // New: whether the animation starts active
    // Trigger conditions
    int trigger_band_index = -1; // -1 means no band-specific trigger
    float trigger_threshold = 0.0f; // Threshold for band energy or beat strength
    float trigger_beat_min = 0.0f; // Minimum beat strength to activate
    float trigger_beat_max = 1.0f; // Maximum beat strength to activate
    std::string text_file_path; // New: Path to text file for animations like RandomText
    float type_speed_words_per_s = 4.0f; // Typing speed for word-by-word reveal
    float display_duration_s = 3.0f;     // How long a fully revealed line remains
    float fade_duration_s = 1.0f;        // Fade-out duration once display time elapses
    float trigger_cooldown_s = 0.75f;    // Minimum time between spawning new lines
    int max_active_lines = 4;            // Maximum number of simultaneous lines
    float random_text_min_y_ratio = 0.0f; // Minimum vertical spawn ratio for random text lines
    float random_text_max_y_ratio = 1.0f; // Maximum vertical spawn ratio for random text lines
    std::optional<int> plane_y;          // Optional plane origin Y for visuals that use dedicated planes
    std::optional<int> plane_x;          // Optional plane origin X for visuals that use dedicated planes
    std::optional<int> plane_rows;       // Optional plane height override
    std::optional<int> plane_cols;       // Optional plane width override
    std::optional<int> matrix_rows;      // Optional number of rows for matrix-style animations
    std::optional<int> matrix_cols;      // Optional number of columns for matrix-style animations
    bool matrix_show_border = true;      // Whether to render a border around the matrix animation
    std::string glyphs_file_path;        // Glyph file override for glyph-based animations
    float matrix_beat_boost = 1.5f;      // Beat multiplier for matrix animations
    float matrix_beat_threshold = 0.6f;  // Beat threshold for matrix animations
    float rain_angle_degrees = 0.0f;     // Angle for cyber rain drops (degrees, relative to vertical)
    float wave_speed_cols_per_s = 40.0f; // Sweep speed for lightning wave animations
    int wave_front_width_cols = 2;       // Width of the solid lightning front in columns
    int wave_tail_length_cols = 6;       // Number of trailing columns for fading tail
    bool wave_alternate_direction = true; // Alternate sweep direction on each activation
    bool wave_direction_right = true;     // Initial sweep direction when not alternating
    float lightning_novelty_threshold = 0.35f;      // Cosine-distance novelty threshold for triggering the wave
    float lightning_energy_floor = 0.015f;          // Minimum summed band energy required to evaluate novelty
    float lightning_detection_cooldown_s = 0.65f;   // Cooldown between lightning triggers
    float lightning_novelty_smoothing_s = 0.18f;    // Smoothing horizon for novelty accumulation
    float lightning_background_smoothing_s = 1.0f;  // Running-average horizon for the background spectrum
    float lightning_activation_decay_s = 0.8f;      // Time for lightning intensity to decay back to zero
    int breathe_points = 64;              // Number of vertices for the breathing shape
    float breathe_min_radius = 6.0f;      // Minimum radius for the breathing circle
    float breathe_max_radius = 14.0f;     // Maximum radius for the breathing circle
    float breathe_audio_radius_influence = 10.0f; // Radius increase based on audio energy
    float breathe_smoothing_s = 0.18f;    // Audio smoothing constant for breathing animation
    float breathe_noise_amount = 0.3f;    // Amount of irregular jitter to apply to the outline
    float breathe_rotation_speed = 0.35f; // Rotation speed (radians per second)
    float breathe_vertical_scale = 0.55f; // Vertical scale correction for terminal aspect ratio
    float breathe_base_pulse_hz = 0.35f;  // Base breathing frequency in Hz
    float breathe_audio_pulse_weight = 0.65f; // How much audio energy speeds up the pulse
    int breathe_band_index = -1;          // Optional FFT band to prioritise
    float breathe_rms_weight = 1.0f;      // Weight applied to RMS audio energy
    float breathe_beat_weight = 0.6f;     // Weight applied to beat strength
    float breathe_band_weight = 0.5f;     // Weight applied to the selected FFT band
    float log_line_interval_s = 0.4f;     // Interval between log entries when streaming
    bool log_loop_messages = true;        // Whether to loop messages when the end is reached
    bool log_show_border = true;          // Display a frame border around the log window
    int log_padding_y = 1;                // Vertical padding between border and log text
    int log_padding_x = 2;                // Horizontal padding between border and log text
    std::string log_title;                // Optional title displayed on the top border
    // Add more generic parameters as needed, e.g., std::map<std::string, std::string> params;
};

struct AppConfig {
    std::string log_level = "info";
    AudioConfig audio;
    DspConfig dsp;
    VisualConfig visual;
    RuntimeConfig runtime;
    PluginConfig plugins;
    std::vector<AnimationConfig> animations;
};

struct ConfigLoadResult {
    AppConfig config;
    std::vector<std::string> warnings;
    bool loaded_file = false;
};

ConfigLoadResult load_app_config(const std::string& path);



} // namespace why

