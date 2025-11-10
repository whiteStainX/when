#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <utility>
#include <vector>

#include "audio/audio_features.h"
#include "audio/feature_extractor.h"
#include "audio/feature_input_frame.h"

namespace {
int pitch_class_for_frequency(double frequency_hz) {
    const double midi_note = 69.0 + 12.0 * std::log2(frequency_hz / 440.0);
    int rounded = static_cast<int>(std::lround(midi_note));
    int pitch = rounded % 12;
    if (pitch < 0) {
        pitch += 12;
    }
    return pitch;
}
}

int main() {
    when::FeatureExtractor::Config weighted_config{};
    weighted_config.smoothing_attack = 1.0f;
    weighted_config.smoothing_release = 1.0f;
    weighted_config.apply_a_weighting = true;
    weighted_config.enable_chroma = true;
    weighted_config.chroma_max_frequency = 20000.0f;

    when::FeatureExtractor weighted(weighted_config);

    when::FeatureExtractor::Config flat_config = weighted_config;
    flat_config.apply_a_weighting = false;
    when::FeatureExtractor unweighted(flat_config);

    const std::array<float, 9> fft_bins{0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    const std::vector<std::pair<std::size_t, std::size_t>> band_ranges{{0, 3}, {3, 6}, {6, 9}};

    when::FeatureInputFrame frame{};
    frame.fft_magnitudes = std::span<const float>(fft_bins.data(), fft_bins.size());
    frame.band_bin_ranges =
        std::span<const std::pair<std::size_t, std::size_t>>(band_ranges.data(), band_ranges.size());
    frame.sample_rate = 48000.0f;
    frame.frame_period = 256.0f / 48000.0f;

    const when::AudioFeatures weighted_features = weighted.process(frame);
    const when::AudioFeatures unweighted_features = unweighted.process(frame);

    assert(unweighted_features.treble_energy_instantaneous > weighted_features.treble_energy_instantaneous);
    assert(unweighted_features.spectral_centroid > weighted_features.spectral_centroid);

    assert(weighted_features.chroma_available);
    assert(unweighted_features.chroma_available);

    const double bin_width = 48000.0 / 16.0;
    const int low_pitch = pitch_class_for_frequency(bin_width * 1.0);
    const int high_pitch = pitch_class_for_frequency(bin_width * 6.0);

    const float weighted_high = weighted_features.chroma[high_pitch];
    const float weighted_low = weighted_features.chroma[low_pitch];
    const float unweighted_high = unweighted_features.chroma[high_pitch];
    const float unweighted_low = unweighted_features.chroma[low_pitch];

    assert(unweighted_high > weighted_high);
    assert(unweighted_high > 0.0f);
    assert(weighted_high > 0.0f);

    const float weighted_sum = std::max(weighted_high + weighted_low, 1e-6f);
    const float unweighted_sum = std::max(unweighted_high + unweighted_low, 1e-6f);
    const float weighted_ratio = weighted_high / weighted_sum;
    const float unweighted_ratio = unweighted_high / unweighted_sum;

    assert(weighted_ratio < unweighted_ratio);

    return 0;
}
