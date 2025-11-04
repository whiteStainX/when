#include <algorithm>
#include <chrono>
#include <cmath>
#include <clocale>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "audio_engine.h"
#include "config.h"
#include "dsp.h"
#include "plugins.h"
#include "renderer.h"
#include "animations/random_text_animation.h"

int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "");

    std::string config_path = "why.toml";
    std::string file_path;
    std::string device_name_override;
    int system_override = -1; // -1 = use config, 0 = mic, 1 = system
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            config_path = argv[i + 1];
            ++i;
            continue;
        }
        if ((arg == "--file" || arg == "-f") && i + 1 < argc) {
            file_path = argv[i + 1];
            ++i;
            continue;
        }
        if ((arg == "--device" || arg == "-d") && i + 1 < argc) {
            device_name_override = argv[i + 1];
            ++i;
            continue;
        }
        if (arg == "--system") {
            system_override = 1;
            continue;
        }
        if (arg == "--mic") {
            system_override = 0;
            continue;
        }
    }

    const why::ConfigLoadResult config_result = why::load_app_config(config_path);
    const why::AppConfig& config = config_result.config;
    if (!config_result.loaded_file) {
        std::clog << "[config] using built-in defaults (missing '" << config_path << "')" << std::endl;
    } else {
        std::clog << "[config] loaded '" << config_path << "'" << std::endl;
    }
    for (const std::string& warning : config_result.warnings) {
        std::cerr << "[config] " << warning << std::endl;
    }

    if (file_path.empty() && config.audio.prefer_file && config.audio.file.enabled && !config.audio.file.path.empty()) {
        file_path = config.audio.file.path;
    }

    std::string capture_device = config.audio.capture.device;
    if (!device_name_override.empty()) {
        capture_device = device_name_override;
    }
    bool use_system_audio = config.audio.capture.system;
    if (system_override == 1) {
        use_system_audio = true;
    } else if (system_override == 0) {
        use_system_audio = false;
    }

    const bool use_file_stream = config.audio.file.enabled && !file_path.empty();
    const ma_uint32 sample_rate = config.audio.capture.sample_rate;
    ma_uint32 channels = use_file_stream ? config.audio.file.channels : config.audio.capture.channels;
    if (channels == 0) {
        channels = 1;
    }
    const std::size_t ring_frames = std::max<std::size_t>(1024, config.audio.capture.ring_frames);

    why::AudioEngine audio(sample_rate,
                           channels,
                           ring_frames,
                           use_file_stream ? file_path : std::string{},
                           capture_device,
                           use_system_audio);
    bool audio_active = false;
    if (use_file_stream || config.audio.capture.enabled) {
        audio_active = audio.start();
        if (!audio_active) {
            std::cerr << "[audio] failed to start audio backend";
            if (!audio.last_error().empty()) {
                std::cerr << ": " << audio.last_error();
            }
            std::cerr << std::endl;
        }
    }

    why::DspEngine dsp(sample_rate,
                       channels,
                       config.dsp.fft_size,
                       config.dsp.hop_size,
                       config.dsp.bands);

    why::PluginManager plugin_manager;
    why::register_builtin_plugins(plugin_manager);
    plugin_manager.load_from_config(config);
    for (const std::string& warning : plugin_manager.warnings()) {
        std::cerr << "[plugin] " << warning << std::endl;
    }

    notcurses_options opts{};
    opts.flags = NCOPTION_SUPPRESS_BANNERS;
    notcurses* nc = notcurses_init(&opts, nullptr);
    if (!nc) {
        std::cerr << "Failed to initialize notcurses" << std::endl;
        audio.stop();
        return 1;
    }



    const std::chrono::duration<double> frame_time(1.0 / config.visual.target_fps);

    const std::size_t scratch_samples = std::max<std::size_t>(4096, ring_frames * static_cast<std::size_t>(channels));
    std::vector<float> audio_scratch(scratch_samples);
    why::AudioMetrics audio_metrics{};
    audio_metrics.active = audio_active;

    // Load animations from config
    why::load_animations_from_config(nc, config);

    bool running = true;
    const auto start_time = std::chrono::steady_clock::now();

    while (running) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - start_time;
        const float time_s = std::chrono::duration_cast<std::chrono::duration<float>>(elapsed).count();

        if (audio_active) {
            const std::size_t samples_read = audio.read_samples(audio_scratch.data(), audio_scratch.size());
            if (samples_read > 0) {
                dsp.push_samples(audio_scratch.data(), samples_read);
                double sum_squares = 0.0;
                float peak_value = 0.0f;
                for (std::size_t i = 0; i < samples_read; ++i) {
                    const float sample = audio_scratch[i];
                    sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
                    peak_value = std::max(peak_value, std::abs(sample));
                }
                const float rms_instant = std::sqrt(sum_squares / static_cast<double>(samples_read));
                audio_metrics.rms = audio_metrics.rms * 0.9f + rms_instant * 0.1f;
                audio_metrics.peak = std::max(peak_value, audio_metrics.peak * 0.95f);
            } else {
                audio_metrics.rms *= 0.98f;
                audio_metrics.peak *= 0.98f;
            }
            audio_metrics.dropped = audio.dropped_samples();
        }

        plugin_manager.notify_frame(audio_metrics, dsp.band_energies(), dsp.beat_strength(), time_s);

        why::render_frame(nc,
                       time_s,
                       audio_metrics,
                       dsp.band_energies(),
                       dsp.beat_strength(),
                       audio.using_file_stream(),
                       config.runtime.show_metrics,
                       config.runtime.show_overlay_metrics);

        if (notcurses_render(nc) != 0) {
            std::cerr << "Failed to render frame" << std::endl;
            break;
        }

        ncinput input{};
        const timespec ts{0, 0};
        uint32_t key = 0;
        while ((key = notcurses_get(nc, &ts, &input)) != 0) {
            if (key == static_cast<uint32_t>(-1)) {
                running = false;
                break;
            }
            if (key == 'q' || key == 'Q') {
                running = false;
                break;
            }



            if (key == NCKEY_RESIZE) {
                break;
            }
        }

        const auto frame_end = std::chrono::steady_clock::now();
        if (frame_end - now < frame_time) {
            std::this_thread::sleep_for(frame_time - (frame_end - now));
        }
    }

    audio.stop();

    if (notcurses_stop(nc) != 0) {
        std::cerr << "Failed to stop notcurses cleanly" << std::endl;
        return 1;
    }

    return 0;
}
