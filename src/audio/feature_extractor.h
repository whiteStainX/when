#pragma once

#include <cstddef>
#include <cstdint>
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
        float smoothing_attack = 0.35f;
        float smoothing_release = 0.08f;
        float band_flux_smoothing = 0.08f;
        float band_onset_sensitivity = 1.5f;
        float bass_onset_sensitivity = 2.0f;
        float mid_onset_sensitivity = 2.0f;
        float treble_onset_sensitivity = 2.0f;
        float band_onset_min_flux = 1e-4f;
        float global_onset_threshold = 1e-3f;
        float tempo_history_seconds = 4.0f;
        float tempo_smoothing = 0.12f;
        float tempo_min_bpm = 60.0f;
        float tempo_max_bpm = 180.0f;
        float tempo_confidence_threshold = 1e-4f;
        float beat_phase_realign = 0.25f;
        std::size_t beats_per_bar = 4;
        bool apply_a_weighting = true;
        bool enable_spectral_flatness = true;
        bool enable_chroma = true;
        float chroma_min_frequency = 32.703f;  // C1
        float chroma_max_frequency = 4186.01f; // C8
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
    void update_weighting_curve(std::size_t fft_bin_count, float sample_rate, std::size_t fft_size);
    void update_chroma_mapping(std::size_t fft_bin_count, float sample_rate, std::size_t fft_size);
    float apply_envelope(float target, float& state) const;
    void resize_onset_history(std::size_t desired_length);
    bool update_tempo_tracking(float onset_strength,
                               float frame_period,
                               bool beat_observed,
                               AudioFeatures& features);

    static std::pair<std::size_t, std::size_t> resolve_band_indices(std::size_t band_count,
                                                                    const BandRange& range);
    static float compute_average_energy(std::span<const float> bands,
                                        std::size_t start,
                                        std::size_t end);
    float compute_spectral_centroid(std::span<const float> bands,
                                    double total_energy_sum) const;
    static float compute_a_weighting_coefficient(double frequency_hz);

    struct TempoTrackerState {
        float bpm = 0.0f;
        float beat_phase = 0.0f;
        float bar_phase = 0.0f;
        float confidence = 0.0f;
    };

    static constexpr std::size_t kDefaultOnsetHistoryLength = 512;
    static constexpr std::size_t kMinOnsetHistoryLength = 32;
    static constexpr std::size_t kMaxOnsetHistoryLength = 2048;

    Config config_;
    std::size_t band_count_ = 0;
    std::vector<float> weighting_curve_;
    std::vector<float> weighted_bins_;
    std::vector<float> weighted_band_buffer_;
    std::vector<float> band_envelopes_;
    std::vector<float> onset_history_;
    std::vector<float> onset_history_linear_;
    std::vector<float> band_flux_baseline_;
    std::vector<std::uint8_t> chroma_bin_map_;
    std::size_t onset_history_write_pos_ = 0;
    TempoTrackerState tempo_state_{};
    float bass_envelope_ = 0.0f;
    float mid_envelope_ = 0.0f;
    float treble_envelope_ = 0.0f;
    float total_envelope_ = 0.0f;
    float weighting_sample_rate_ = 0.0f;
    std::size_t weighting_fft_size_ = 0;
    float chroma_sample_rate_ = 0.0f;
    std::size_t chroma_fft_size_ = 0;
    int beat_counter_in_bar_ = 0;
};

} // namespace when
