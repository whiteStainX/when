#include "audio/feature_extractor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

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
    if (!band_flux_baseline_.empty()) {
        std::fill(band_flux_baseline_.begin(), band_flux_baseline_.end(), 0.0f);
    }
    if (onset_history_.empty()) {
        resize_onset_history(kDefaultOnsetHistoryLength);
    } else {
        std::fill(onset_history_.begin(), onset_history_.end(), 0.0f);
        if (onset_history_linear_.size() != onset_history_.size()) {
            onset_history_linear_.assign(onset_history_.size(), 0.0f);
        } else {
            std::fill(onset_history_linear_.begin(), onset_history_linear_.end(), 0.0f);
        }
    }
    onset_history_write_pos_ = 0;
    tempo_state_ = {};
    beat_counter_in_bar_ = 0;
    bass_envelope_ = 0.0f;
    mid_envelope_ = 0.0f;
    treble_envelope_ = 0.0f;
    total_envelope_ = 0.0f;
    weighting_sample_rate_ = 0.0f;
    weighting_fft_size_ = 0;
    chroma_sample_rate_ = 0.0f;
    chroma_fft_size_ = 0;
    chroma_bin_map_.clear();
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

    if (config_.enable_spectral_flatness) {
        std::span<const float> flatness_bins;
        if (can_apply_weighting && !weighted_bins_.empty()) {
            flatness_bins = std::span<const float>(weighted_bins_.data(), weighted_bins_.size());
        } else if (!fft_bins.empty()) {
            flatness_bins = fft_bins;
        }

        if (!flatness_bins.empty()) {
            double log_sum = 0.0;
            double linear_sum = 0.0;
            std::size_t count = 0;
            constexpr double kEpsilon = 1e-12;
            for (float magnitude : flatness_bins) {
                const double value = std::max(static_cast<double>(magnitude), kEpsilon);
                log_sum += std::log(value);
                linear_sum += value;
                ++count;
            }

            if (count > 0 && linear_sum > kEpsilon) {
                const double geometric_mean = std::exp(log_sum / static_cast<double>(count));
                const double arithmetic_mean = linear_sum / static_cast<double>(count);
                const double ratio = (arithmetic_mean > kEpsilon) ? (geometric_mean / arithmetic_mean) : 0.0;
                features.spectral_flatness = static_cast<float>(std::clamp(ratio, 0.0, 1.0));
            } else {
                features.spectral_flatness = 0.0f;
            }
        } else {
            features.spectral_flatness = 0.0f;
        }
    } else {
        features.spectral_flatness = 0.0f;
    }

    if (config_.enable_chroma && can_apply_weighting && !weighted_bins_.empty()) {
        update_chroma_mapping(weighted_bins_.size(), input_frame.sample_rate, (weighted_bins_.size() > 0) ? (weighted_bins_.size() - 1) * 2 : 0);

        std::array<float, 12> chroma_accumulator{};
        double total_energy = 0.0;
        constexpr double kEnergyFloor = 1e-12;

        const std::size_t usable_bins = std::min(weighted_bins_.size(), chroma_bin_map_.size());
        for (std::size_t bin = 0; bin < usable_bins; ++bin) {
            const std::uint8_t pitch_class = chroma_bin_map_[bin];
            if (pitch_class >= 12) {
                continue;
            }

            const double magnitude = static_cast<double>(weighted_bins_[bin]);
            if (magnitude <= 0.0) {
                continue;
            }

            const double energy = magnitude * magnitude;
            chroma_accumulator[pitch_class] += static_cast<float>(energy);
            total_energy += energy;
        }

        if (total_energy > kEnergyFloor) {
            const float normalization = static_cast<float>(1.0 / total_energy);
            for (float& value : chroma_accumulator) {
                value *= normalization;
            }
            features.chroma = chroma_accumulator;
            features.chroma_available = true;
        } else {
            features.chroma = {};
            features.chroma_available = false;
        }
    } else {
        features.chroma = {};
        features.chroma_available = false;
    }

    float onset_strength = 0.0f;
    bool aggregated_onset = false;
    const std::span<const float> band_flux = input_frame.band_flux;
    if (!band_flux.empty() && band_flux.size() == band_count_) {
        if (band_flux_baseline_.size() != band_count_) {
            band_flux_baseline_.assign(band_count_, 0.0f);
        }

        const float flux_alpha = std::clamp(config_.band_flux_smoothing, 0.0f, 1.0f);
        double aggregated_excess = 0.0;
        for (std::size_t i = 0; i < band_count_; ++i) {
            const float flux_value = std::max(band_flux[i], 0.0f);
            float& baseline = band_flux_baseline_[i];
            baseline += (flux_value - baseline) * flux_alpha;
            const float excess = std::max(0.0f, flux_value - baseline);
            aggregated_excess += excess;
        }

        onset_strength = (band_count_ > 0)
                             ? static_cast<float>(aggregated_excess / static_cast<double>(band_count_))
                             : 0.0f;

        const std::span<const float> baseline_span(band_flux_baseline_.data(), band_flux_baseline_.size());
        const auto detect_band = [&](std::size_t start, std::size_t end) {
            if (start >= end) {
                return false;
            }
            const float band_value = compute_average_energy(band_flux, start, end);
            const float band_baseline = compute_average_energy(baseline_span, start, end);
            const float threshold =
                std::max(config_.band_onset_min_flux, band_baseline * config_.band_onset_sensitivity);
            return band_value > threshold;
        };

        features.bass_beat = detect_band(bass_start, bass_end);
        features.mid_beat = detect_band(mid_start, mid_end);
        features.treble_beat = detect_band(treble_start, treble_end);

        aggregated_onset = onset_strength > config_.global_onset_threshold;
    } else {
        features.bass_beat = false;
        features.mid_beat = false;
        features.treble_beat = false;
    }

    features.beat_detected = features.beat_detected || aggregated_onset || features.bass_beat || features.mid_beat ||
                             features.treble_beat;

    const bool downbeat =
        update_tempo_tracking(onset_strength, input_frame.frame_period, features.beat_detected, features);
    features.downbeat = downbeat;

    return features;
}

void FeatureExtractor::ensure_band_capacity(std::size_t band_count) {
    band_count_ = band_count;
    band_envelopes_.assign(band_count_, 0.0f);
    weighted_band_buffer_.assign(band_count_, 0.0f);
    band_flux_baseline_.assign(band_count_, 0.0f);
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

void FeatureExtractor::update_chroma_mapping(std::size_t fft_bin_count,
                                             float sample_rate,
                                             std::size_t fft_size) {
    if (fft_bin_count == 0 || sample_rate <= 0.0f || fft_size == 0) {
        chroma_bin_map_.assign(fft_bin_count, 0xFFu);
        chroma_sample_rate_ = sample_rate;
        chroma_fft_size_ = fft_size;
        return;
    }

    if (chroma_bin_map_.size() == fft_bin_count && chroma_sample_rate_ == sample_rate && chroma_fft_size_ == fft_size) {
        return;
    }

    chroma_bin_map_.assign(fft_bin_count, 0xFFu);
    chroma_sample_rate_ = sample_rate;
    chroma_fft_size_ = fft_size;

    const double sr = static_cast<double>(sample_rate);
    const double fft = static_cast<double>(fft_size);
    const double bin_width = (fft > 0.0) ? sr / fft : 0.0;
    if (bin_width <= 0.0) {
        return;
    }

    const double min_frequency = static_cast<double>(std::max(std::min(config_.chroma_min_frequency, config_.chroma_max_frequency), 0.0f));
    const double max_frequency = static_cast<double>(std::max({config_.chroma_min_frequency, config_.chroma_max_frequency, 0.0f}));

    if (min_frequency >= max_frequency) {
        return;
    }

    for (std::size_t bin = 0; bin < fft_bin_count; ++bin) {
        const double frequency = bin_width * static_cast<double>(bin);
        if (frequency < min_frequency || frequency > max_frequency) {
            continue;
        }

        if (frequency <= 0.0) {
            continue;
        }

        const double midi_note = 69.0 + 12.0 * std::log2(frequency / 440.0);
        const int rounded_note = static_cast<int>(std::lround(midi_note));
        int pitch_class = rounded_note % 12;
        if (pitch_class < 0) {
            pitch_class += 12;
        }

        chroma_bin_map_[bin] = static_cast<std::uint8_t>(pitch_class);
    }
}

float FeatureExtractor::apply_envelope(float target, float& state) const {
    target = std::max(target, 0.0f);
    const float alpha = (target > state) ? config_.smoothing_attack : config_.smoothing_release;
    state += (target - state) * alpha;
    return state;
}

void FeatureExtractor::resize_onset_history(std::size_t desired_length) {
    if (desired_length == 0) {
        desired_length = kMinOnsetHistoryLength;
    }
    desired_length = std::clamp(desired_length, kMinOnsetHistoryLength, kMaxOnsetHistoryLength);
    if (onset_history_.size() == desired_length) {
        if (onset_history_linear_.size() != desired_length) {
            onset_history_linear_.assign(desired_length, 0.0f);
        }
        return;
    }

    onset_history_.assign(desired_length, 0.0f);
    onset_history_linear_.assign(desired_length, 0.0f);
    onset_history_write_pos_ = 0;
}

bool FeatureExtractor::update_tempo_tracking(float onset_strength,
                                             float frame_period,
                                             bool beat_observed,
                                             AudioFeatures& features) {
    const std::size_t beats_per_bar = std::max<std::size_t>(1, config_.beats_per_bar);
    const int beats_per_bar_int = static_cast<int>(beats_per_bar);
    bool downbeat = false;

    if (frame_period <= 0.0f) {
        tempo_state_.bar_phase = (static_cast<float>(beat_counter_in_bar_) + tempo_state_.beat_phase) /
                                 static_cast<float>(beats_per_bar_int);
        tempo_state_.bar_phase = std::clamp(tempo_state_.bar_phase, 0.0f, 1.0f);
        features.bpm = tempo_state_.bpm;
        features.beat_phase = tempo_state_.beat_phase;
        features.bar_phase = tempo_state_.bar_phase;
        features.downbeat = false;
        return false;
    }

    const double frame_period_d = static_cast<double>(frame_period);
    const double window_seconds = std::max(static_cast<double>(config_.tempo_history_seconds), frame_period_d);
    double frames_needed = window_seconds / frame_period_d;
    if (frames_needed < static_cast<double>(kMinOnsetHistoryLength)) {
        frames_needed = static_cast<double>(kMinOnsetHistoryLength);
    }
    if (frames_needed > static_cast<double>(kMaxOnsetHistoryLength)) {
        frames_needed = static_cast<double>(kMaxOnsetHistoryLength);
    }

    const std::size_t desired_length = std::max<std::size_t>(kMinOnsetHistoryLength,
                                                             static_cast<std::size_t>(frames_needed));
    if (onset_history_.empty() || onset_history_.size() != desired_length) {
        resize_onset_history(desired_length);
    }

    if (onset_history_.empty()) {
        features.bpm = tempo_state_.bpm;
        features.beat_phase = tempo_state_.beat_phase;
        features.bar_phase = tempo_state_.bar_phase;
        features.downbeat = false;
        return false;
    }

    onset_history_[onset_history_write_pos_] = std::max(onset_strength, 0.0f);
    onset_history_write_pos_ = (onset_history_write_pos_ + 1) % onset_history_.size();

    const std::size_t history_size = onset_history_.size();
    if (onset_history_linear_.size() != history_size) {
        onset_history_linear_.assign(history_size, 0.0f);
    }
    for (std::size_t i = 0; i < history_size; ++i) {
        const std::size_t index = (onset_history_write_pos_ + i) % history_size;
        onset_history_linear_[i] = onset_history_[index];
    }

    float tempo_candidate = 0.0f;
    float best_score = 0.0f;

    if (history_size >= 8) {
        const float min_bpm = std::min(config_.tempo_min_bpm, config_.tempo_max_bpm);
        const float max_bpm = std::max(config_.tempo_min_bpm, config_.tempo_max_bpm);
        if (max_bpm > 0.0f && frame_period_d > 0.0) {
            double min_period = 60.0 / static_cast<double>(max_bpm);
            double max_period = 60.0 / static_cast<double>(std::max(min_bpm, 1.0f));
            if (min_period > max_period) {
                std::swap(min_period, max_period);
            }

            std::size_t min_lag = static_cast<std::size_t>(std::floor(min_period / frame_period_d));
            std::size_t max_lag = static_cast<std::size_t>(std::ceil(max_period / frame_period_d));

            min_lag = std::max<std::size_t>(1, min_lag);
            max_lag = std::max<std::size_t>(min_lag, max_lag);
            if (max_lag >= history_size) {
                max_lag = history_size - 1;
            }

            if (max_lag > min_lag && history_size > 1) {
                const float mean = std::accumulate(onset_history_linear_.begin(), onset_history_linear_.end(), 0.0f) /
                                   static_cast<float>(history_size);

                for (std::size_t lag = min_lag; lag <= max_lag; ++lag) {
                    float score = 0.0f;
                    for (std::size_t i = lag; i < history_size; ++i) {
                        const float a = onset_history_linear_[i] - mean;
                        const float b = onset_history_linear_[i - lag] - mean;
                        score += a * b;
                    }
                    const std::size_t sample_count = history_size - lag;
                    if (sample_count > 0) {
                        score /= static_cast<float>(sample_count);
                    }

                    if (score > best_score) {
                        best_score = score;
                        tempo_candidate = (lag > 0)
                                              ? static_cast<float>(60.0 / (static_cast<double>(lag) * frame_period_d))
                                              : 0.0f;
                    }
                }
            }
        }
    }

    if (tempo_candidate > 0.0f && best_score > config_.tempo_confidence_threshold) {
        const float smoothing = std::clamp(config_.tempo_smoothing, 0.0f, 1.0f);
        if (tempo_state_.bpm <= 0.0f) {
            tempo_state_.bpm = tempo_candidate;
        } else {
            tempo_state_.bpm += (tempo_candidate - tempo_state_.bpm) * smoothing;
        }
        tempo_state_.confidence = best_score;
    } else {
        tempo_state_.confidence *= 0.95f;
        if (tempo_state_.confidence < config_.tempo_confidence_threshold * 0.5f) {
            tempo_state_.bpm *= 0.98f;
            if (tempo_state_.bpm < 1e-3f) {
                tempo_state_.bpm = 0.0f;
            }
        }
    }

    if (tempo_state_.bpm > 0.0f) {
        const float beats_advanced = (tempo_state_.bpm / 60.0f) * frame_period;
        float phase_accumulator = tempo_state_.beat_phase + beats_advanced;
        if (phase_accumulator >= 1.0f) {
            const float wrapped = std::floor(phase_accumulator);
            const int wraps = static_cast<int>(wrapped);
            phase_accumulator -= wrapped;
            for (int i = 0; i < wraps; ++i) {
                beat_counter_in_bar_ = (beat_counter_in_bar_ + 1) % beats_per_bar_int;
                if (beat_counter_in_bar_ == 0) {
                    downbeat = true;
                }
            }
        }

        tempo_state_.beat_phase = std::clamp(phase_accumulator, 0.0f, 1.0f);

        if (beat_observed) {
            const float realign = std::clamp(config_.beat_phase_realign, 0.0f, 1.0f);
            tempo_state_.beat_phase -= tempo_state_.beat_phase * realign;
            tempo_state_.beat_phase = std::clamp(tempo_state_.beat_phase, 0.0f, 1.0f);
        }

        const float per_bar = static_cast<float>(beats_per_bar_int);
        tempo_state_.bar_phase =
            (static_cast<float>(beat_counter_in_bar_) + tempo_state_.beat_phase) / std::max(per_bar, 1.0f);
        tempo_state_.bar_phase = std::clamp(tempo_state_.bar_phase, 0.0f, 1.0f);
    } else {
        tempo_state_.beat_phase = 0.0f;
        tempo_state_.bar_phase = 0.0f;
        beat_counter_in_bar_ = 0;
    }

    features.bpm = tempo_state_.bpm;
    features.beat_phase = tempo_state_.beat_phase;
    features.bar_phase = tempo_state_.bar_phase;
    features.downbeat = downbeat;
    return downbeat;
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
