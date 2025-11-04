#include "config.h"

#include <algorithm>
#include <sstream>

#include "config/animation_config_parser.h"
#include "config/raw_config.h"
#include "config/value_parsers.h"

namespace why {
namespace {

using config::detail::RawConfig;

template <typename T, typename Parser>
void assign_scalar(const RawConfig& raw,
                   const std::string& key,
                   T& target,
                   Parser parser,
                   std::vector<std::string>& warnings) {
    const auto it = raw.scalars.find(key);
    if (it == raw.scalars.end()) {
        return;
    }
    T parsed_value{};
    if (parser(it->second.value, parsed_value)) {
        target = parsed_value;
    } else {
        std::ostringstream oss;
        oss << "Invalid value for '" << key << "' on line " << it->second.line;
        warnings.push_back(oss.str());
    }
}

void assign_string(const RawConfig& raw, const std::string& key, std::string& target) {
    const auto it = raw.scalars.find(key);
    if (it == raw.scalars.end()) {
        return;
    }
    std::string value = it->second.value;
    if (!value.empty() && value.front() == '\"' && value.back() == '\"' && value.size() >= 2) {
        value = value.substr(1, value.size() - 2);
    }
    if (!value.empty() && value.front() == '\'' && value.back() == '\'' && value.size() >= 2) {
        value = value.substr(1, value.size() - 2);
    }
    target = value;
}

void populate_audio_config(const RawConfig& raw,
                           AudioConfig& audio,
                           std::vector<std::string>& warnings) {
    using config::detail::parse_bool;
    using config::detail::parse_float32;
    using config::detail::parse_size;
    using config::detail::parse_uint32;

    assign_scalar(raw,
                  "audio.capture.enabled",
                  audio.capture.enabled,
                  parse_bool,
                  warnings);
    assign_scalar(raw,
                  "audio.capture.sample_rate",
                  audio.capture.sample_rate,
                  parse_uint32,
                  warnings);
    assign_scalar(raw,
                  "audio.capture.channels",
                  audio.capture.channels,
                  parse_uint32,
                  warnings);
    assign_scalar(raw,
                  "audio.capture.ring_frames",
                  audio.capture.ring_frames,
                  parse_size,
                  warnings);
    assign_string(raw, "audio.capture.device", audio.capture.device);
    assign_scalar(raw,
                  "audio.capture.input_gain",
                  audio.capture.input_gain,
                  parse_float32,
                  warnings);
    assign_scalar(raw,
                  "audio.capture.system",
                  audio.capture.system,
                  parse_bool,
                  warnings);

    assign_scalar(raw,
                  "audio.file.enabled",
                  audio.file.enabled,
                  parse_bool,
                  warnings);
    assign_string(raw, "audio.file.path", audio.file.path);
    assign_scalar(raw,
                  "audio.file.channels",
                  audio.file.channels,
                  parse_uint32,
                  warnings);
    assign_scalar(raw,
                  "audio.file.gain",
                  audio.file.gain,
                  parse_float32,
                  warnings);

    assign_scalar(raw,
                  "audio.prefer_file",
                  audio.prefer_file,
                  parse_bool,
                  warnings);
}

void populate_dsp_config(const RawConfig& raw,
                         DspConfig& dsp,
                         std::vector<std::string>& warnings) {
    using config::detail::parse_bool;
    using config::detail::parse_float32;
    using config::detail::parse_size;

    assign_scalar(raw, "dsp.fft_size", dsp.fft_size, parse_size, warnings);
    assign_scalar(raw, "dsp.hop_size", dsp.hop_size, parse_size, warnings);
    assign_scalar(raw, "dsp.bands", dsp.bands, parse_size, warnings);
    assign_string(raw, "dsp.window", dsp.window);
    assign_scalar(raw,
                  "dsp.smoothing_attack",
                  dsp.smoothing_attack,
                  parse_float32,
                  warnings);
    assign_scalar(raw,
                  "dsp.smoothing_release",
                  dsp.smoothing_release,
                  parse_float32,
                  warnings);
    assign_scalar(raw,
                  "dsp.beat_sensitivity",
                  dsp.beat_sensitivity,
                  parse_float32,
                  warnings);
    assign_scalar(raw, "dsp.enable_flux", dsp.enable_flux, parse_bool, warnings);
}

void populate_visual_config(const RawConfig& raw,
                            VisualConfig& visual,
                            std::vector<std::string>& warnings) {
    using config::detail::parse_double;
    assign_scalar(raw, "visual.target_fps", visual.target_fps, parse_double, warnings);
}

void populate_runtime_config(const RawConfig& raw,
                             RuntimeConfig& runtime,
                             std::vector<std::string>& warnings) {
    using config::detail::parse_bool;
    assign_scalar(raw, "runtime.show_metrics", runtime.show_metrics, parse_bool, warnings);
    assign_scalar(raw, "runtime.allow_resize", runtime.allow_resize, parse_bool, warnings);
    assign_scalar(raw, "runtime.beat_flash", runtime.beat_flash, parse_bool, warnings);
    assign_scalar(raw,
                  "runtime.show_overlay_metrics",
                  runtime.show_overlay_metrics,
                  parse_bool,
                  warnings);
}

void populate_plugin_config(const RawConfig& raw,
                            PluginConfig& plugins,
                            std::vector<std::string>& warnings) {
    using config::detail::parse_bool;
    (void)warnings;
    assign_string(raw, "plugins.directory", plugins.directory);
    const auto array_it = raw.arrays.find("plugins.autoload");
    if (array_it != raw.arrays.end()) {
        plugins.autoload = array_it->second.values;
    }
    assign_scalar(raw, "plugins.safe_mode", plugins.safe_mode, parse_bool, warnings);
}

void populate_animation_configs(const RawConfig& raw,
                                std::vector<AnimationConfig>& animations,
                                std::vector<std::string>& warnings) {
    for (const auto& raw_anim_config : raw.animation_configs) {
        auto parsed = config::detail::parse_animation_config(raw_anim_config, warnings);
        if (parsed.has_value()) {
            animations.push_back(*parsed);
        }
    }
}

void apply_sanity_defaults(AppConfig& config) {
    if (config.audio.capture.sample_rate == 0) {
        config.audio.capture.sample_rate = 48000;
    }
    if (config.audio.capture.channels == 0) {
        config.audio.capture.channels = 2;
    }
    if (config.audio.capture.ring_frames == 0) {
        config.audio.capture.ring_frames = 8192;
    }
    if (config.audio.file.channels == 0) {
        config.audio.file.channels = 1;
    }
    if (config.audio.file.gain <= 0.0f) {
        config.audio.file.gain = 1.0f;
    }
    if (config.dsp.hop_size == 0) {
        config.dsp.hop_size = std::max<std::size_t>(1, config.dsp.fft_size / 4);
    }
    if (config.visual.target_fps <= 0.0) {
        config.visual.target_fps = 60.0;
    }
    if (config.plugins.autoload.empty()) {
        config.plugins.autoload.push_back("beat-flash-debug");
    }
}

} // namespace

ConfigLoadResult load_app_config(const std::string& path) {
    ConfigLoadResult result;
    RawConfig raw = config::detail::parse_raw_config(path, result.warnings, result.loaded_file);

    assign_string(raw, "log.level", result.config.log_level);

    populate_audio_config(raw, result.config.audio, result.warnings);
    populate_dsp_config(raw, result.config.dsp, result.warnings);
    populate_visual_config(raw, result.config.visual, result.warnings);
    populate_runtime_config(raw, result.config.runtime, result.warnings);
    populate_plugin_config(raw, result.config.plugins, result.warnings);
    populate_animation_configs(raw, result.config.animations, result.warnings);

    apply_sanity_defaults(result.config);

    return result;
}

} // namespace why

