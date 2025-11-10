#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>
#include <vector>

#include "audio_engine.h"

int main() {
    const std::filesystem::path data_path =
        std::filesystem::path(__FILE__).parent_path() / "data" / "stereo_tone_22050.wav";

    when::AudioEngine engine(48000, 2, 4096, data_path.string(), "", false);
    const bool started = engine.start();
    assert(started);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::vector<float> buffer(4096, 0.0f);
    std::size_t total_samples = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(750);

    while (total_samples < 512 && std::chrono::steady_clock::now() < deadline) {
        total_samples += engine.read_samples(buffer.data() + total_samples, buffer.size() - total_samples);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    assert(total_samples >= 256);
    assert(total_samples % 2 == 0);

    bool saw_variation = false;
    for (std::size_t i = 0; i + 1 < total_samples; i += 2) {
        const float left = buffer[i];
        const float right = buffer[i + 1];
        assert(std::isfinite(left));
        assert(std::isfinite(right));
        assert(std::abs(left - right) < 1e-4f);
        if (i >= 2 && std::abs(left - buffer[i - 2]) > 1e-5f) {
            saw_variation = true;
        }
    }

    assert(saw_variation);
    assert(engine.dropped_samples() == 0);

    engine.stop();
    return 0;
}
