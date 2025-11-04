#pragma once

#include <utility>
#include <vector>

#include <notcurses/notcurses.h>

#include "../audio_engine.h"
#include "../config.h" // Include AppConfig
#include "../events/event_bus.h"

namespace why {
namespace animations {

template<typename AnimationT>
void bind_standard_frame_updates(AnimationT* animation,
                                 const AnimationConfig& config,
                                 events::EventBus& bus);

class Animation {
public:
    virtual ~Animation() = default;

    // Lifecycle methods
    virtual void init(notcurses* nc, const AppConfig& config) = 0;
    virtual void update(float delta_time,
                        const AudioMetrics& metrics,
                        const std::vector<float>& bands,
                        float beat_strength) = 0;
    virtual void render(notcurses* nc) = 0;
    virtual void activate() = 0;
    virtual void deactivate() = 0;

    // State queries
    virtual bool is_active() const = 0;
    virtual int get_z_index() const = 0;
    virtual ncplane* get_plane() const = 0; // Or a way to get the primary plane

    virtual void bind_events(const AnimationConfig& config, events::EventBus& bus) {
        (void)config;
        (void)bus;
    }

    void clear_event_subscriptions() {
        for (auto& handle : event_subscriptions_) {
            handle.reset();
        }
        event_subscriptions_.clear();
    }

protected:
    void track_subscription(events::EventBus::SubscriptionHandle handle) {
        event_subscriptions_.push_back(std::move(handle));
    }

private:
    std::vector<events::EventBus::SubscriptionHandle> event_subscriptions_;

    template<typename AnimationT>
    friend void bind_standard_frame_updates(AnimationT* animation,
                                            const AnimationConfig& config,
                                            events::EventBus& bus);
};

} // namespace animations
} // namespace why

