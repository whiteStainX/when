#pragma once

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "audio/audio_features.h"
#include "audio/feature_input_frame.h"

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

    void prepare(std::size_t band_count);
    void reset();

    AudioFeatures process(const FeatureInputFrame& input_frame);

    void set_config(const Config& config);
    const Config& config() const { return config_; }

private:
    void ensure_band_capacity(std::size_t band_count);

    static std::pair<std::size_t, std::size_t> resolve_band_indices(std::size_t band_count,
                                                                    const BandRange& range);
    static float compute_average_energy(std::span<const float> bands,
                                        std::size_t start,
                                        std::size_t end);
    float compute_spectral_centroid(std::span<const float> bands,
                                    double total_energy_sum) const;

    struct TempoTrackerState {
        float bpm = 0.0f;
        float beat_phase = 0.0f;
        float bar_phase = 0.0f;
    };

    static constexpr std::size_t kDefaultOnsetHistoryLength = 512;

    Config config_;
    std::size_t band_count_ = 0;
    std::vector<float> weighting_curve_;
    std::vector<float> band_envelopes_;
    std::vector<float> last_band_energies_;
    std::vector<float> onset_history_;
    std::size_t onset_history_write_pos_ = 0;
    TempoTrackerState tempo_state_{};
    float bass_envelope_ = 0.0f;
    float mid_envelope_ = 0.0f;
    float treble_envelope_ = 0.0f;
};

} // namespace when
