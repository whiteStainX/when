#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "audio/audio_features.h"

namespace when {

class FeatureExtractor {
public:
    struct BandRange {
        float start_ratio = 0.0f; // Inclusive start position expressed as a fraction of the band count
        float end_ratio = 1.0f;   // Exclusive end position expressed as a fraction of the band count
    };

    struct Config {
        BandRange bass_range{0.0f, 0.2f};
        BandRange mid_range{0.2f, 0.7f};
        BandRange treble_range{0.7f, 1.0f};
        float beat_detection_threshold = 0.35f;
        float silence_threshold = 1e-5f;
    };

    FeatureExtractor();
    explicit FeatureExtractor(Config config);

    AudioFeatures process(const std::vector<float>& fft_bands, float beat_strength) const;

    void set_config(const Config& config);
    const Config& config() const { return config_; }

private:
    static std::pair<std::size_t, std::size_t> resolve_band_indices(std::size_t band_count,
                                                                    const BandRange& range);
    static float compute_average_energy(const std::vector<float>& bands,
                                        std::size_t start,
                                        std::size_t end);
    float compute_spectral_centroid(const std::vector<float>& bands,
                                    double total_energy_sum) const;

    Config config_;
};

} // namespace when
