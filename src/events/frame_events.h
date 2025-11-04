#pragma once

#include <vector>

#include "../audio_engine.h"

namespace why {
namespace events {

struct FrameUpdateEvent {
    float delta_time;
    const AudioMetrics& metrics;
    const std::vector<float>& bands;
    float beat_strength;
};

struct BeatDetectedEvent {
    float strength;
};

} // namespace events
} // namespace why

