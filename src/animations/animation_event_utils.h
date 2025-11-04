#pragma once

#include "../config.h"
#include "../events/event_bus.h"
#include "../events/frame_events.h"
#include "animation.h"

namespace why {
namespace animations {

inline bool has_custom_triggers(const AnimationConfig& config) {
    return config.trigger_band_index != -1 ||
           config.trigger_beat_min > 0.0f ||
           config.trigger_beat_max < 1.0f;
}

inline bool evaluate_band_condition(const AnimationConfig& config,
                                    const std::vector<float>& bands) {
    if (config.trigger_band_index == -1) {
        return true;
    }

    const int index = config.trigger_band_index;
    if (index < 0 || index >= static_cast<int>(bands.size())) {
        return false;
    }

    return bands[static_cast<std::size_t>(index)] >= config.trigger_threshold;
}

inline bool evaluate_beat_condition(const AnimationConfig& config, float beat_strength) {
    if (config.trigger_beat_min <= 0.0f && config.trigger_beat_max >= 1.0f) {
        return true;
    }

    return beat_strength >= config.trigger_beat_min &&
           beat_strength <= config.trigger_beat_max;
}

template<typename AnimationT>
void bind_standard_frame_updates(AnimationT* animation,
                                 const AnimationConfig& config,
                                 events::EventBus& bus) {
    AnimationConfig captured_config = config;
    auto handle = bus.subscribe<events::FrameUpdateEvent>(
        [animation, captured_config](const events::FrameUpdateEvent& event) {
            const bool meets_band = evaluate_band_condition(captured_config, event.bands);
            const bool meets_beat = evaluate_beat_condition(captured_config, event.beat_strength);
            const bool should_be_active = has_custom_triggers(captured_config)
                                              ? (meets_band && meets_beat)
                                              : captured_config.initially_active;

            if (should_be_active && !animation->is_active()) {
                animation->activate();
            } else if (!should_be_active && animation->is_active()) {
                animation->deactivate();
            }

            if (animation->is_active()) {
                animation->update(event.delta_time,
                                  event.metrics,
                                  event.bands,
                                  event.beat_strength);
            }
        });
    animation->track_subscription(std::move(handle));
}

} // namespace animations
} // namespace why

