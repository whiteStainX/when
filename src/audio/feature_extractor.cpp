#include "audio/feature_extractor.h"

#include <algorithm>
#include <cmath>

#include "audio/feature_input_frame.h"

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

AudioFeatures FeatureExtractor::process(const FeatureInputFrame& input_frame) {
    const auto instantaneous_bands = input_frame.instantaneous_band_energies;
    const auto smoothed_from_dsp = input_frame.smoothed_band_energies;
    const bool has_instantaneous = !instantaneous_bands.empty();
    const auto bands = has_instantaneous ? instantaneous_bands : smoothed_from_dsp;

    if (band_count_ != bands.size()) {
        prepare(bands.size());
    }

    AudioFeatures features{};
    features.band_flux = input_frame.band_flux;
    features.beat_strength = input_frame.beat_strength;
    features.beat_detected = input_frame.beat_strength >= config_.beat_detection_threshold;

    const std::size_t band_count = bands.size();
    if (band_count == 0) {
        return features;
    }

    for (std::size_t i = 0; i < band_count; ++i) {
        const float target = std::max(bands[i], 0.0f);
        float& envelope = band_envelopes_[i];
        const float alpha = (target > envelope) ? config_.smoothing_attack : config_.smoothing_release;
        envelope += (target - envelope) * alpha;
    }

    auto [bass_start, bass_end] = resolve_band_indices(band_count, config_.bass_range);
    auto [mid_start, mid_end] = resolve_band_indices(band_count, config_.mid_range);
    auto [treble_start, treble_end] = resolve_band_indices(band_count, config_.treble_range);

    const float bass_instant = compute_average_energy(bands, bass_start, bass_end);
    const float mid_instant = compute_average_energy(bands, mid_start, mid_end);
    const float treble_instant = compute_average_energy(bands, treble_start, treble_end);

    const std::span<const float> smoothed_span(band_envelopes_.data(), band_envelopes_.size());
    const float bass_smoothed = compute_average_energy(smoothed_span, bass_start, bass_end);
    const float mid_smoothed = compute_average_energy(smoothed_span, mid_start, mid_end);
    const float treble_smoothed = compute_average_energy(smoothed_span, treble_start, treble_end);

    features.bass_energy_instantaneous = bass_instant;
    features.mid_energy_instantaneous = mid_instant;
    features.treble_energy_instantaneous = treble_instant;

    features.bass_energy = bass_smoothed;
    features.mid_energy = mid_smoothed;
    features.treble_energy = treble_smoothed;

    double total_sum = 0.0;
    double smoothed_total_sum = 0.0;
    for (std::size_t i = 0; i < band_count; ++i) {
        total_sum += std::max(bands[i], 0.0f);
        smoothed_total_sum += std::max(band_envelopes_[i], 0.0f);
    }
    if (band_count > 0) {
        const double divisor = static_cast<double>(band_count);
        features.total_energy_instantaneous = static_cast<float>(total_sum / divisor);
        features.total_energy = static_cast<float>(smoothed_total_sum / divisor);
    }

    if (smoothed_total_sum > static_cast<double>(config_.silence_threshold)) {
        features.spectral_centroid = compute_spectral_centroid(smoothed_span, smoothed_total_sum);
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

float FeatureExtractor::compute_average_energy(std::span<const float> bands,
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

float FeatureExtractor::compute_spectral_centroid(std::span<const float> bands,
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
