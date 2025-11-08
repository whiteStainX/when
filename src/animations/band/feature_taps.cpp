#include "feature_taps.h"

#include <algorithm>
#include <cmath>

namespace when::animations::band {

namespace {

std::pair<std::size_t, std::size_t> resolve_band_indices(std::size_t band_count,
                                                         float start_ratio,
                                                         float end_ratio) {
    if (band_count == 0) {
        return {0, 0};
    }

    const float clamped_start = std::clamp(start_ratio, 0.0f, 1.0f);
    const float clamped_end = std::clamp(end_ratio, clamped_start, 1.0f);

    std::size_t start = static_cast<std::size_t>(std::floor(clamped_start * static_cast<float>(band_count)));
    std::size_t end = static_cast<std::size_t>(std::ceil(clamped_end * static_cast<float>(band_count)));

    if (start >= band_count) {
        start = band_count - 1;
    }
    end = std::clamp<std::size_t>(end, start + 1, band_count);

    return {start, end};
}

float average_flux(std::span<const float> flux,
                   float start_ratio,
                   float end_ratio) {
    if (flux.empty()) {
        return 0.0f;
    }

    const auto [start, end] = resolve_band_indices(flux.size(), start_ratio, end_ratio);
    if (start >= end || start >= flux.size()) {
        return 0.0f;
    }

    double sum = 0.0;
    for (std::size_t i = start; i < end; ++i) {
        sum += static_cast<double>(std::max(flux[i], 0.0f));
    }

    const std::size_t count = std::max<std::size_t>(1, end - start);
    return static_cast<float>(sum / static_cast<double>(count));
}

float compute_chroma_dominance(const AudioFeatures& features) {
    if (!features.chroma_available) {
        return 0.0f;
    }

    double sum = 0.0;
    float peak = 0.0f;
    for (float value : features.chroma) {
        const float clamped = std::max(value, 0.0f);
        sum += static_cast<double>(clamped);
        peak = std::max(peak, clamped);
    }

    if (sum <= 0.0) {
        return 0.0f;
    }

    const double ratio = static_cast<double>(peak) / sum;
    return static_cast<float>(std::clamp(ratio, 0.0, 1.0));
}

float normalise_centroid(float centroid,
                         float floor,
                         float ceiling) {
    if (ceiling <= floor) {
        return std::clamp(centroid, 0.0f, 1.0f);
    }

    const float normalised = (centroid - floor) / (ceiling - floor);
    return std::clamp(normalised, 0.0f, 1.0f);
}

} // namespace

FeatureTapConfig feature_tap_config_from(const FeatureExtractor::Config& feature_config) {
    FeatureTapConfig config{};
    config.bass_start_ratio = feature_config.bass_range.start_ratio;
    config.bass_end_ratio = feature_config.bass_range.end_ratio;
    config.mid_start_ratio = feature_config.mid_range.start_ratio;
    config.mid_end_ratio = feature_config.mid_range.end_ratio;
    config.treble_start_ratio = feature_config.treble_range.start_ratio;
    config.treble_end_ratio = feature_config.treble_range.end_ratio;
    return config;
}

FeatureView build_feature_view(const AudioFeatures& features, const FeatureTapConfig& config) {
    FeatureView view{};

    view.bass_env = std::max(features.bass_energy, 0.0f);
    view.mid_env = std::max(features.mid_energy, 0.0f);
    view.treble_env = std::max(features.treble_energy, 0.0f);

    view.bass_instant = std::max(features.bass_energy_instantaneous, 0.0f);
    view.mid_instant = std::max(features.mid_energy_instantaneous, 0.0f);
    view.treble_instant = std::max(features.treble_energy_instantaneous, 0.0f);

    view.total_energy = std::max(features.total_energy, 0.0f);
    view.total_instant = std::max(features.total_energy_instantaneous, 0.0f);

    view.spectral_flatness = std::clamp(features.spectral_flatness, 0.0f, 1.0f);
    view.spectral_centroid_norm = normalise_centroid(features.spectral_centroid,
                                                     config.centroid_floor,
                                                     config.centroid_ceiling);

    view.beat_phase = std::clamp(features.beat_phase, 0.0f, 1.0f);
    view.bar_phase = std::clamp(features.bar_phase, 0.0f, 1.0f);

    view.low_flux = average_flux(features.band_flux,
                                 config.bass_start_ratio,
                                 config.bass_end_ratio);
    view.mid_flux = average_flux(features.band_flux,
                                 config.mid_start_ratio,
                                 config.mid_end_ratio);
    view.high_flux = average_flux(features.band_flux,
                                  config.treble_start_ratio,
                                  config.treble_end_ratio);

    view.beat_now = features.beat_detected;
    view.bass_beat = features.bass_beat;
    view.mid_beat = features.mid_beat;
    view.treble_beat = features.treble_beat;

    view.chroma_available = features.chroma_available;
    view.chroma_dominance = compute_chroma_dominance(features);

    return view;
}

} // namespace when::animations::band

