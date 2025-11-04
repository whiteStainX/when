#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <cxxopts.hpp>
#include <notcurses/notcurses.h>

#include "audio_engine.h"
#include "ConfigLoader.h"

// Default audio settings
constexpr ma_uint32 SAMPLE_RATE = 48000;
constexpr ma_uint32 CHANNELS = 2;
constexpr std::size_t RING_FRAMES = 48000; // 1 second buffer

static std::atomic<bool> running = true;

void run_visualization(struct notcurses* nc, why::AudioEngine& audio_engine, bool dev_mode) {
    struct ncplane* stdplane = notcurses_stdplane(nc);
    ncplane_erase(stdplane);

    std::vector<float> audio_buffer(RING_FRAMES);

    while (running) {
        ncplane_erase(stdplane);

        // Input
        ncinput nci;
        if (notcurses_get_nblock(nc, &nci) > 0) {
            if (nci.id == 'q' || nci.id == NCKEY_ESC) {
                running = false;
            }
        }

        // Audio
        const auto samples_read = audio_engine.read_samples(audio_buffer.data(), audio_buffer.size());

        // Dev info
        if (dev_mode) {
            unsigned int dimy, dimx;
            ncplane_dim_yx(stdplane, &dimy, &dimx);
            float peak = 0.0f;
            double sum_square = 0.0;
            for (std::size_t i = 0; i < samples_read; ++i) {
                const float sample = audio_buffer[i];
                peak = std::max(peak, std::abs(sample));
                sum_square += static_cast<double>(sample) * static_cast<double>(sample);
            }
            const double rms = samples_read > 0 ? std::sqrt(sum_square / static_cast<double>(samples_read)) : 0.0;

            char info[256];
            snprintf(info,
                     sizeof(info),
                     "Capturing... Samples read: %zu | RMS: %.4f | Peak: %.4f | Dropped: %zu",
                     samples_read,
                     rms,
                     peak,
                     audio_engine.dropped_samples());
            ncplane_putstr_yx(stdplane, dimy > 0 ? dimy - 1 : 0, 0, info);
        }

        // Render
        notcurses_render(nc);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main(int argc, char** argv) {
    cxxopts::Options options("when", "A music visualizer");
    options.add_options()
        ("s,system", "Capture system audio", cxxopts::value<bool>()->default_value("false"))
        ("d,dev", "Enable developer info overlay", cxxopts::value<bool>()->default_value("false"))
        ("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    bool use_system_audio = result["system"].as<bool>();
    bool dev_mode = result["dev"].as<bool>();

    // Notcurses initialization
    notcurses_options ncopts = {
        .flags = NCOPTION_INHIBIT_SETLOCALE | NCOPTION_NO_WINCH_SIGHANDLER | NCOPTION_SUPPRESS_BANNERS
    };
    struct notcurses* nc = notcurses_init(&ncopts, NULL);
    if (!nc) {
        std::cerr << "Failed to initialize notcurses." << std::endl;
        return 1;
    }

    // AudioEngine initialization
    why::AudioEngine audio_engine(SAMPLE_RATE, CHANNELS, RING_FRAMES, "", "", use_system_audio);
    if (!audio_engine.start()) {
        std::cerr << "Failed to start audio engine: " << audio_engine.last_error() << std::endl;
        notcurses_stop(nc);
        return 1;
    }

    run_visualization(nc, audio_engine, dev_mode);

    audio_engine.stop();
    notcurses_stop(nc);

    return 0;
}