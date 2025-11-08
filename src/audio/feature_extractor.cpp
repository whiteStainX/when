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
    if (!band_envelopes_.empty()) {
        std::fill(band_envelopes_.begin(), band_envelopes_.end(), 0.0f);
    }
    if (!weighted_band_buffer_.empty()) {
        std::fill(weighted_band_buffer_.begin(), weighted_band_buffer_.end(), 0.0f);
    }
    if (!weighted_bins_.empty()) {
        std::fill(weighted_bins_.begin(), weighted_bins_.end(), 0.0f);
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
    total_envelope_ = 0.0f;
    weighting_sample_rate_ = 0.0f;
    weighting_fft_size_ = 0;
}

void FeatureExtractor::set_config(const Config& config) {
    config_ = config;
    reset();
}

AudioFeatures FeatureExtractor::process(const FeatureInputFrame& input_frame) {
    AudioFeatures features{};
    features.band_flux = input_frame.band_flux;
    features.beat_strength = input_frame.beat_strength;
    features.beat_detected = input_frame.beat_strength >= config_.beat_detection_threshold;

    const auto fft_bins = input_frame.fft_magnitudes;
    const auto band_ranges = input_frame.band_bin_ranges;
    const auto instantaneous_bands = input_frame.instantaneous_band_energies;
    const auto smoothed_from_dsp = input_frame.smoothed_band_energies;

    std::span<const float> bands;
    const bool can_apply_weighting =
        !fft_bins.empty() && !band_ranges.empty() && input_frame.sample_rate > 0.0f;

    if (can_apply_weighting) {
        const std::size_t band_count = band_ranges.size();
        if (band_count_ != band_count) {
            prepare(band_count);
        }

        const std::size_t fft_bin_count = fft_bins.size();
        const std::size_t fft_size = (fft_bin_count > 0) ? (fft_bin_count - 1) * 2 : 0;
        update_weighting_curve(fft_bin_count, input_frame.sample_rate, fft_size);

        if (weighted_bins_.size() != fft_bin_count) {
            weighted_bins_.assign(fft_bin_count, 0.0f);
        }
        for (std::size_t i = 0; i < fft_bin_count; ++i) {
            const float weight = (i < weighting_curve_.size()) ? weighting_curve_[i] : 1.0f;
            weighted_bins_[i] = fft_bins[i] * weight;
        }

        if (weighted_band_buffer_.size() != band_count_) {
            weighted_band_buffer_.assign(band_count_, 0.0f);
        }

        const std::size_t resolved_count = std::min(band_count_, band_ranges.size());
        for (std::size_t band = 0; band < resolved_count; ++band) {
            const auto [raw_start, raw_end] = band_ranges[band];
            const std::size_t start = std::min(raw_start, fft_bin_count);
            const std::size_t end = std::min(raw_end, fft_bin_count);
            if (end <= start) {
                weighted_band_buffer_[band] = 0.0f;
                continue;
            }

            double sum_sq = 0.0;
            for (std::size_t bin = start; bin < end; ++bin) {
                const double magnitude = weighted_bins_[bin];
                sum_sq += magnitude * magnitude;
            }

            const std::size_t span = end - start;
            const double average = (span > 0)
                                       ? sum_sq / static_cast<double>(span)
                                       : 0.0;
            weighted_band_buffer_[band] =
                static_cast<float>(std::sqrt(std::max(average, 0.0)));
        }

        for (std::size_t band = resolved_count; band < band_count_; ++band) {
            weighted_band_buffer_[band] = 0.0f;
        }

        bands =
            std::span<const float>(weighted_band_buffer_.data(), weighted_band_buffer_.size());
    } else {
        const bool has_instantaneous = !instantaneous_bands.empty();
        bands = has_instantaneous ? instantaneous_bands : smoothed_from_dsp;
        if (band_count_ != bands.size()) {
            prepare(bands.size());
        }
    }

    const std::size_t band_count = bands.size();
    if (band_count == 0) {
        return features;
    }

    double total_sum = 0.0;
    for (std::size_t i = 0; i < band_count; ++i) {
        const float target = std::max(bands[i], 0.0f);
        total_sum += target;
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

    features.bass_energy_instantaneous = bass_instant;
    features.mid_energy_instantaneous = mid_instant;
    features.treble_energy_instantaneous = treble_instant;

    bass_envelope_ = apply_envelope(bass_instant, bass_envelope_);
    mid_envelope_ = apply_envelope(mid_instant, mid_envelope_);
    treble_envelope_ = apply_envelope(treble_instant, treble_envelope_);

    features.bass_energy = bass_envelope_;
    features.mid_energy = mid_envelope_;
    features.treble_energy = treble_envelope_;
    features.bass_envelope = bass_envelope_;
    features.mid_envelope = mid_envelope_;
    features.treble_envelope = treble_envelope_;

    const float total_instant = (band_count > 0)
                                    ? static_cast<float>(total_sum / static_cast<double>(band_count))
                                    : 0.0f;
    total_envelope_ = apply_envelope(total_instant, total_envelope_);
    features.total_energy_instantaneous = total_instant;
    features.total_energy = total_envelope_;

    double smoothed_total_sum = 0.0;
    for (float value : band_envelopes_) {
        smoothed_total_sum += std::max(value, 0.0f);
    }

    if (smoothed_total_sum > static_cast<double>(config_.silence_threshold)) {
        const std::span<const float> smoothed_span(band_envelopes_.data(), band_envelopes_.size());
        features.spectral_centroid = compute_spectral_centroid(smoothed_span, smoothed_total_sum);
    } else {
        features.spectral_centroid = 0.0f;
    }

    return features;
}

void FeatureExtractor::ensure_band_capacity(std::size_t band_count) {
    band_count_ = band_count;
    band_envelopes_.assign(band_count_, 0.0f);
    weighted_band_buffer_.assign(band_count_, 0.0f);
}

void FeatureExtractor::update_weighting_curve(std::size_t fft_bin_count,
                                              float sample_rate,
                                              std::size_t fft_size) {
    if (fft_bin_count == 0 || sample_rate <= 0.0f || fft_size == 0) {
        weighting_curve_.assign(fft_bin_count, 1.0f);
        weighting_sample_rate_ = sample_rate;
        weighting_fft_size_ = fft_size;
        return;
    }

    if (weighting_curve_.size() == fft_bin_count && weighting_sample_rate_ == sample_rate &&
        weighting_fft_size_ == fft_size) {
        return;
    }

    weighting_curve_.resize(fft_bin_count);
    weighting_sample_rate_ = sample_rate;
    weighting_fft_size_ = fft_size;

    const double sr = static_cast<double>(sample_rate);
    const double fft = static_cast<double>(fft_size);
    const double bin_width = (fft > 0.0) ? sr / fft : 0.0;

    for (std::size_t bin = 0; bin < fft_bin_count; ++bin) {
        const double frequency = bin_width * static_cast<double>(bin);
        weighting_curve_[bin] = compute_a_weighting_coefficient(frequency);
    }
}

float FeatureExtractor::apply_envelope(float target, float& state) const {
    target = std::max(target, 0.0f);
    const float alpha = (target > state) ? config_.smoothing_attack : config_.smoothing_release;
    state += (target - state) * alpha;
    return state;
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

float FeatureExtractor::compute_a_weighting_coefficient(double frequency_hz) {
    if (frequency_hz <= 0.0) {
        return 0.0f;
    }

    const double f2 = frequency_hz * frequency_hz;
    const double numerator = 12200.0 * 12200.0 * f2 * f2;
    const double term1 = f2 + 20.6 * 20.6;
    const double term2 = std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9));
    const double term3 = f2 + 12200.0 * 12200.0;
    const double denominator = term1 * term2 * term3;

    if (denominator <= 0.0) {
        return 0.0f;
    }

    const double ra = numerator / denominator;
    if (ra <= 0.0) {
        return 0.0f;
    }

    const double a_db = 2.0 + 20.0 * std::log10(ra);
    const double linear = std::pow(10.0, a_db / 20.0);
    return static_cast<float>(std::clamp(linear, 0.0, 10.0));
}

} // namespace when
