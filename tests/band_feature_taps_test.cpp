#include <cassert>
#include <cmath>
#include <vector>

#include "animations/band/feature_taps.h"

using when::animations::band::FeatureTapConfig;
using when::animations::band::FeatureView;
using when::animations::band::build_feature_view;

namespace {

when::AudioFeatures make_base_features() {
    when::AudioFeatures features{};
    features.bass_energy = 0.4f;
    features.mid_energy = 0.6f;
    features.treble_energy = 0.2f;
    features.bass_energy_instantaneous = 0.5f;
    features.mid_energy_instantaneous = 0.4f;
    features.treble_energy_instantaneous = 0.3f;
    features.total_energy = 0.5f;
    features.total_energy_instantaneous = 0.45f;
    features.beat_phase = 0.25f;
    features.bar_phase = 0.75f;
    features.beat_detected = true;
    features.bass_beat = true;
    features.mid_beat = false;
    features.treble_beat = true;
    features.spectral_flatness = 0.3f;
    features.spectral_centroid = 0.55f;
    return features;
}

} // namespace

int main() {
    std::vector<float> flux{0.1f, 0.2f, 0.4f, 0.8f, 1.0f, 1.2f};

    when::AudioFeatures features = make_base_features();
    features.band_flux = std::span<const float>(flux.data(), flux.size());

    FeatureView view = build_feature_view(features);

    assert(view.bass_env == 0.4f);
    assert(view.mid_env == 0.6f);
    assert(view.treble_env == 0.2f);

    // With default ratios (0-0.2, 0.2-0.7, 0.7-1.0) and 6 bands we expect:
    // low: average of indices [0,1] => (0.1 + 0.2)/2 = 0.15
    // mid: average of indices [1,2,3,4] => (0.2 + 0.4 + 0.8 + 1.0)/4 = 0.6
    // high: average of indices [4,5] => (1.0 + 1.2)/2 = 1.1
    assert(std::abs(view.low_flux - 0.15f) < 1e-5f);
    assert(std::abs(view.mid_flux - 0.6f) < 1e-5f);
    assert(std::abs(view.high_flux - 1.1f) < 1e-5f);

    // Chromatic dominance guard when data unavailable
    assert(!view.chroma_available);
    assert(view.chroma_dominance == 0.0f);

    // Custom centroid range
    FeatureTapConfig config{};
    config.centroid_floor = 0.3f;
    config.centroid_ceiling = 0.6f;
    FeatureView custom_view = build_feature_view(features, config);
    assert(std::abs(custom_view.spectral_centroid_norm - 0.8333333f) < 1e-5f);

    // Chroma dominance with valid vector
    features.chroma_available = true;
    features.chroma = {0.0f, 1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    FeatureView chroma_view = build_feature_view(features);
    assert(chroma_view.chroma_available);
    assert(std::abs(chroma_view.chroma_dominance - 0.5f) < 1e-5f);

    return 0;
}

