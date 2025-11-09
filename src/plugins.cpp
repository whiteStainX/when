#include "plugins.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>
#include <vector>

#include "animations/band/feature_taps.h"

namespace when {
namespace {

class BeatFlashDebugPlugin final : public Plugin {
public:
    std::string id() const override { return "beat-flash-debug"; }

    void on_load(const AppConfig& config) override {
        enabled_ = config.runtime.beat_flash;
        threshold_ = std::max(0.35f, static_cast<float>(config.dsp.beat_sensitivity));
        last_log_time_ = -10.0;
        log_interval_ = 1.0;
        if (!enabled_) {
            std::clog << "[plugin] beat-flash-debug disabled via runtime.beat_flash" << std::endl;
            return;
        }
        open_log(config.plugins.directory);
        if (!log_) {
            enabled_ = false;
            std::clog << "[plugin] beat-flash-debug logging unavailable; disabling plugin" << std::endl;
            return;
        }
        std::clog << "[plugin] beat-flash-debug armed (threshold=" << threshold_ << ", log='" << log_path_
                  << "')" << std::endl;
        log_header();
    }

    void on_frame(const AudioMetrics&, const AudioFeatures& features, double time_s) override {
        if (!enabled_) {
            return;
        }
        if (features.beat_strength < threshold_) {
            return;
        }
        if (time_s - last_log_time_ < log_interval_) {
            return;
        }
        last_log_time_ = time_s;
        write_log(features.beat_strength, time_s);
    }

private:
    void open_log(const std::string& directory) {
        log_.close();
        log_path_.clear();
        std::filesystem::path base_path;
        if (!directory.empty()) {
            base_path = std::filesystem::path(directory);
            std::error_code ec;
            std::filesystem::create_directories(base_path, ec);
            if (ec) {
                std::clog << "[plugin] beat-flash-debug failed to create directory '" << base_path.string()
                          << "' (" << ec.message() << ")" << std::endl;
                base_path.clear();
            }
        }
        const std::filesystem::path log_path = base_path.empty() ? std::filesystem::path("beat-flash-debug.log")
                                                                 : base_path / "beat-flash-debug.log";
        log_path_ = log_path.string();
        log_.open(log_path_, std::ios::out | std::ios::app);
    }

    void log_header() {
        if (!log_) {
            return;
        }
        log_ << "\n=== beat-flash-debug session started ===\n";
        log_.flush();
    }

    void write_log(float beat_strength, double time_s) {
        if (!log_) {
            return;
        }
        std::ostringstream line;
        line << std::fixed << std::setprecision(3) << time_s << "s beat_strength=" << beat_strength;
        log_ << line.str() << '\n';
        log_.flush();
    }

    bool enabled_ = true;
    float threshold_ = 0.75f;
    double last_log_time_ = 0.0;
    double log_interval_ = 1.0;
    std::ofstream log_;
    std::string log_path_;
};

} // namespace

namespace {

class BandFeatureTapLogger final : public Plugin {
public:
    std::string id() const override { return "band-feature-tap-logger"; }

    void configure_feature_extractor(const when::FeatureExtractor::Config& feature_config) override {
        tap_config_ = when::animations::band::feature_tap_config_from(feature_config);
    }

    void on_load(const AppConfig& config) override {
        enabled_ = config.runtime.band_feature_logging;
        duration_limit_s_ = std::max(0.0, config.runtime.band_feature_logging_duration_s);

        if (!enabled_) {
            std::clog << "[plugin] band-feature-tap-logger disabled" << std::endl;
            return;
        }

        open_log(config);
        if (!log_) {
            enabled_ = false;
            std::clog << "[plugin] band-feature-tap-logger failed to open log file" << std::endl;
            return;
        }

        std::clog << "[plugin] band-feature-tap-logger capturing";
        if (duration_limit_s_ > 0.0) {
            std::clog << " for up to " << duration_limit_s_ << "s";
        }
        std::clog << " -> '" << log_path_ << "'" << std::endl;
    }

    void on_frame(const AudioMetrics&, const AudioFeatures& features, double time_s) override {
        if (!enabled_) {
            return;
        }

        if (!started_) {
            started_ = true;
            start_time_s_ = time_s;
            write_header();
        }

        if (duration_limit_s_ > 0.0 && time_s - start_time_s_ > duration_limit_s_) {
            if (!notified_stop_) {
                std::clog << "[plugin] band-feature-tap-logger reached capture limit" << std::endl;
                notified_stop_ = true;
            }
            enabled_ = false;
            return;
        }

        const auto view = when::animations::band::build_feature_view(features, tap_config_);
        write_view(time_s, view);
    }

private:
    void open_log(const AppConfig& config) {
        std::filesystem::path base_path;
        if (!config.plugins.directory.empty()) {
            base_path = std::filesystem::path(config.plugins.directory);
            std::error_code ec;
            std::filesystem::create_directories(base_path, ec);
            if (ec) {
                std::clog << "[plugin] band-feature-tap-logger failed to create directory '"
                          << base_path.string() << "' (" << ec.message() << ")" << std::endl;
                base_path.clear();
            }
        }

        std::filesystem::path target_path;
        if (!config.runtime.band_feature_log_file.empty()) {
            target_path = config.runtime.band_feature_log_file;
            if (target_path.is_relative() && !base_path.empty()) {
                target_path = base_path / target_path;
            }
        } else {
            target_path = base_path.empty() ? std::filesystem::path("band-feature-taps.csv")
                                            : base_path / "band-feature-taps.csv";
        }

        log_path_ = target_path.string();
        log_.open(target_path, std::ios::out | std::ios::trunc);
    }

    void write_header() {
        if (!log_ || header_written_) {
            return;
        }
        log_ << "time_s"
             << ",bass_env"
             << ",mid_env"
             << ",treble_env"
             << ",bass_instant"
             << ",mid_instant"
             << ",treble_instant"
             << ",total_energy"
             << ",total_instant"
             << ",spectral_flatness"
             << ",spectral_centroid_norm"
             << ",beat_phase"
             << ",bar_phase"
             << ",low_flux"
             << ",mid_flux"
             << ",high_flux"
             << ",beat_now"
             << ",bass_beat"
             << ",mid_beat"
             << ",treble_beat"
             << ",chroma_available"
             << ",chroma_dominance"
             << '\n';
        header_written_ = true;
    }

    void write_view(double time_s, const when::animations::band::FeatureView& view) {
        if (!log_) {
            return;
        }

        log_ << std::fixed << std::setprecision(6)
             << time_s << ','
             << view.bass_env << ','
             << view.mid_env << ','
             << view.treble_env << ','
             << view.bass_instant << ','
             << view.mid_instant << ','
             << view.treble_instant << ','
             << view.total_energy << ','
             << view.total_instant << ','
             << view.spectral_flatness << ','
             << view.spectral_centroid_norm << ','
             << view.beat_phase << ','
             << view.bar_phase << ','
             << view.low_flux << ','
             << view.mid_flux << ','
             << view.high_flux << ','
             << static_cast<int>(view.beat_now) << ','
             << static_cast<int>(view.bass_beat) << ','
             << static_cast<int>(view.mid_beat) << ','
             << static_cast<int>(view.treble_beat) << ','
             << static_cast<int>(view.chroma_available) << ','
             << view.chroma_dominance << '\n';
        log_.flush();
    }

    bool enabled_ = false;
    bool started_ = false;
    bool header_written_ = false;
    bool notified_stop_ = false;
    double start_time_s_ = 0.0;
    double duration_limit_s_ = 0.0;
    std::ofstream log_;
    std::string log_path_;
    when::animations::band::FeatureTapConfig tap_config_ =
        when::animations::band::feature_tap_config_from(when::FeatureExtractor::Config{});
};

} // namespace

void PluginManager::register_factory(const std::string& id, PluginFactory factory) {
    factories_[id] = std::move(factory);
}

void PluginManager::load_from_config(const AppConfig& config,
                                     const when::FeatureExtractor::Config& feature_config) {
    warnings_.clear();
    active_.clear();
    if (config.plugins.safe_mode) {
        warnings_.push_back("Plug-ins disabled by plugins.safe_mode");
        return;
    }
    std::vector<std::string> requested = config.plugins.autoload;
    if (config.runtime.band_feature_logging) {
        if (std::find(requested.begin(), requested.end(), "band-feature-tap-logger") == requested.end()) {
            requested.push_back("band-feature-tap-logger");
        }
    }

    for (const std::string& id : requested) {
        auto it = factories_.find(id);
        if (it == factories_.end()) {
            warnings_.push_back("Unknown plugin '" + id + "'");
            continue;
        }
        std::unique_ptr<Plugin> plugin = it->second();
        if (!plugin) {
            warnings_.push_back("Factory for plugin '" + id + "' returned null");
            continue;
        }
        plugin->configure_feature_extractor(feature_config);
        plugin->on_load(config);
        active_.push_back(std::move(plugin));
    }
}

void PluginManager::notify_frame(const AudioMetrics& metrics,
                                 const AudioFeatures& features,
                                 double time_s) {
    for (const std::unique_ptr<Plugin>& plugin : active_) {
        plugin->on_frame(metrics, features, time_s);
    }
}

void register_builtin_plugins(PluginManager& manager) {
    manager.register_factory("beat-flash-debug", []() { return std::make_unique<BeatFlashDebugPlugin>(); });
    manager.register_factory("band-feature-tap-logger", []() { return std::make_unique<BandFeatureTapLogger>(); });
}

} // namespace when

