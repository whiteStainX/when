#pragma once

#include <vector>

#include "../audio_engine.h"
#include "audio/audio_features.h"

namespace when {
namespace events {

struct AudioFeaturesUpdatedEvent {
    const AudioFeatures& features;
};

struct FrameUpdateEvent {
    float delta_time;
    const AudioMetrics& metrics;
    const AudioFeatures& features;
    const std::vector<float>& bands;
    float beat_strength;
};

struct BeatDetectedEvent {
    float strength;
};

} // namespace events
} // namespace when

