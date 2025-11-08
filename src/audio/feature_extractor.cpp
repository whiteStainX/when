#include "audio/feature_extractor.h"

#include <algorithm>
#include <cmath>

namespace when {

FeatureExtractor::FeatureExtractor() { reset(); }

FeatureExtractor::FeatureExtractor(Config config) : config_(config) { reset(); }

void FeatureExtractor::prepare(std::size_t band_count) {
    ensure_band_capacity(band_count);
    reset();
}

void FeatureExtractor::reset() {
    if (band_count_ > 0) {
        std::fill(band_envelopes_.begin(), band_envelopes_.end(), 0.0f);
        std::fill(last_band_energies_.begin(), last_band_energies_.end(), 0.0f);
    }
    if (onset_history_.empty()) {
        onset_history_.resize(kDefaultOnsetHistoryLength, 0.0f);
    } else {
        std::fill(onset_history_.begin(), onset_history_.end(), 0.0f);
    }
    onset_history_write_pos_ = 0;
    tempo_state_ = {};
    bass_envelope_ = 0.0f;
    mid_envelope_ = 0.0f;
    treble_envelope_ = 0.0f;
}

void FeatureExtractor::set_config(const Config& config) {
    config_ = config;
    reset();
}

AudioFeatures FeatureExtractor::process(const std::vector<float>& fft_bands, float beat_strength) {
    if (band_count_ != fft_bands.size()) {
        prepare(fft_bands.size());
    }

    AudioFeatures features{};
    features.beat_strength = beat_strength;
    features.beat_detected = beat_strength >= config_.beat_detection_threshold;

    const std::size_t band_count = fft_bands.size();
    if (band_count == 0) {
        return features;
    }

    if (!last_band_energies_.empty()) {
        std::copy(fft_bands.begin(), fft_bands.end(), last_band_energies_.begin());
    }

    auto [bass_start, bass_end] = resolve_band_indices(band_count, config_.bass_range);
    auto [mid_start, mid_end] = resolve_band_indices(band_count, config_.mid_range);
    auto [treble_start, treble_end] = resolve_band_indices(band_count, config_.treble_range);

    features.bass_energy = compute_average_energy(fft_bands, bass_start, bass_end);
    features.mid_energy = compute_average_energy(fft_bands, mid_start, mid_end);
    features.treble_energy = compute_average_energy(fft_bands, treble_start, treble_end);

    double total_sum = 0.0;
    for (float band : fft_bands) {
        total_sum += std::max(band, 0.0f);
    }
    if (band_count > 0) {
        features.total_energy = static_cast<float>(total_sum / static_cast<double>(band_count));
    }

    if (total_sum > static_cast<double>(config_.silence_threshold)) {
        features.spectral_centroid = compute_spectral_centroid(fft_bands, total_sum);
    } else {
        features.spectral_centroid = 0.0f;
    }

    bass_envelope_ = features.bass_energy;
    mid_envelope_ = features.mid_energy;
    treble_envelope_ = features.treble_energy;

    return features;
}

void FeatureExtractor::ensure_band_capacity(std::size_t band_count) {
    band_count_ = band_count;
    weighting_curve_.assign(band_count_, 1.0f);
    band_envelopes_.assign(band_count_, 0.0f);
    last_band_energies_.assign(band_count_, 0.0f);
}

std::pair<std::size_t, std::size_t> FeatureExtractor::resolve_band_indices(std::size_t band_count,
                                                                           const BandRange& range) {
    if (band_count == 0) {
        return {0, 0};
    }

    const float clamped_start = std::clamp(range.start_ratio, 0.0f, 1.0f);
    const float clamped_end = std::clamp(range.end_ratio, clamped_start, 1.0f);

    std::size_t start = static_cast<std::size_t>(std::floor(clamped_start * static_cast<float>(band_count)));
    std::size_t end = static_cast<std::size_t>(std::ceil(clamped_end * static_cast<float>(band_count)));

    if (start >= band_count) {
        start = band_count - 1;
    }
    end = std::clamp(end, start + 1, band_count);

    return {start, end};
}

float FeatureExtractor::compute_average_energy(const std::vector<float>& bands,
                                               std::size_t start,
                                               std::size_t end) {
    if (bands.empty() || start >= bands.size()) {
        return 0.0f;
    }

    end = std::min(end, bands.size());
    if (end <= start) {
        return 0.0f;
    }

    double sum = 0.0;
    for (std::size_t i = start; i < end; ++i) {
        sum += std::max(bands[i], 0.0f);
    }
    const std::size_t count = end - start;
    return (count > 0) ? static_cast<float>(sum / static_cast<double>(count)) : 0.0f;
}

float FeatureExtractor::compute_spectral_centroid(const std::vector<float>& bands,
                                                  double total_energy_sum) const {
    if (bands.empty() || total_energy_sum <= 0.0) {
        return 0.0f;
    }

    const double band_count = static_cast<double>(bands.size());
    double weighted_sum = 0.0;
    for (std::size_t i = 0; i < bands.size(); ++i) {
        const double energy = std::max(bands[i], 0.0f);
        if (energy <= 0.0) {
            continue;
        }
        const double normalized_center = (static_cast<double>(i) + 0.5) / band_count;
        weighted_sum += energy * normalized_center;
    }

    if (weighted_sum <= 0.0) {
        return 0.0f;
    }

    const double centroid = weighted_sum / total_energy_sum;
    return static_cast<float>(std::clamp(centroid, 0.0, 1.0));
}

} // namespace when
