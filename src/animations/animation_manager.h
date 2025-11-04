#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include <notcurses/notcurses.h>

#include "animation.h"
#include "../config.h"
#include "../events/event_bus.h"
#include "../events/frame_events.h"

namespace why {
namespace animations {

class AnimationManager {
public:
    AnimationManager() = default;
    ~AnimationManager() = default;

    void load_animations(notcurses* nc, const AppConfig& config);
    void update_all(float delta_time,
                    const AudioMetrics& metrics,
                    const std::vector<float>& bands,
                    float beat_strength);
    void render_all(notcurses* nc);

    events::EventBus& event_bus() { return event_bus_; }
    const events::EventBus& event_bus() const { return event_bus_; }

private:
    struct ManagedAnimation {
        std::unique_ptr<Animation> animation;
        AnimationConfig config;
    };

    std::vector<std::unique_ptr<ManagedAnimation>> animations_;
    events::EventBus event_bus_;
};

} // namespace animations
} // namespace why

