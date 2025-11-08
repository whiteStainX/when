#pragma once

#include <span>

#include "audio/audio_features.h"
#include "audio/feature_extractor.h"

namespace when::animations::band {

struct FeatureView {
    float bass_env = 0.0f;
    float mid_env = 0.0f;
    float treble_env = 0.0f;

    float bass_instant = 0.0f;
    float mid_instant = 0.0f;
    float treble_instant = 0.0f;

    float total_energy = 0.0f;
    float total_instant = 0.0f;

    float spectral_flatness = 0.0f;
    float spectral_centroid_norm = 0.0f;

    float beat_phase = 0.0f;
    float bar_phase = 0.0f;

    float low_flux = 0.0f;
    float mid_flux = 0.0f;
    float high_flux = 0.0f;

    bool beat_now = false;
    bool bass_beat = false;
    bool mid_beat = false;
    bool treble_beat = false;

    bool chroma_available = false;
    float chroma_dominance = 0.0f;
};

struct FeatureTapConfig {
    float bass_start_ratio = 0.0f;
    float bass_end_ratio = 0.2f;
    float mid_start_ratio = 0.2f;
    float mid_end_ratio = 0.7f;
    float treble_start_ratio = 0.7f;
    float treble_end_ratio = 1.0f;

    float centroid_floor = 0.0f;
    float centroid_ceiling = 1.0f;
};

FeatureTapConfig feature_tap_config_from(const FeatureExtractor::Config& feature_config);

FeatureView build_feature_view(const AudioFeatures& features,
                               const FeatureTapConfig& config = FeatureTapConfig{});

} // namespace when::animations::band

