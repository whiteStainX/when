#include "animation_manager.h"

#include <iostream>

#include "random_text_animation.h"
#include "bar_visual_animation.h"
#include "ascii_matrix_animation.h"
#include "cyber_rain_animation.h"
#include "lightning_wave_animation.h"
#include "breathe_animation.h"
#include "logging_animation.h"

#include "../config/raw_config.h"

namespace why {
namespace animations {

void AnimationManager::load_animations(notcurses* nc, const AppConfig& app_config) {
    event_bus_.reset();
    animations_.clear();
    animations_.reserve(app_config.animations.size());

    for (const auto& anim_config : app_config.animations) {
        std::unique_ptr<Animation> new_animation;
        std::string cleaned_type = config::detail::sanitize_string_value(anim_config.type);

        if (cleaned_type == "RandomText") {
            new_animation = std::make_unique<RandomTextAnimation>();
        } else if (cleaned_type == "BarVisual") {
            new_animation = std::make_unique<BarVisualAnimation>();
        } else if (cleaned_type == "AsciiMatrix") {
            new_animation = std::make_unique<AsciiMatrixAnimation>();
        } else if (cleaned_type == "CyberRain") {
            new_animation = std::make_unique<CyberRainAnimation>();
        } else if (cleaned_type == "LightningWave") {
            new_animation = std::make_unique<LightningWaveAnimation>();
        } else if (cleaned_type == "Breathe") {
            new_animation = std::make_unique<BreatheAnimation>();
        } else if (cleaned_type == "Logging") {
            new_animation = std::make_unique<LoggingAnimation>();
        }

        if (new_animation) {
            new_animation->init(nc, app_config);
            new_animation->clear_event_subscriptions();

            auto managed = std::make_unique<ManagedAnimation>();
            managed->config = anim_config;
            managed->animation = std::move(new_animation);

            managed->animation->bind_events(managed->config, event_bus_);
            animations_.push_back(std::move(managed));
        } else {
            // std::cerr << "[AnimationManager::load_animations] Unknown animation type: " << anim_config.type << std::endl;
        }
    }
}

void AnimationManager::update_all(float delta_time,
                                  const AudioMetrics& metrics,
                                  const std::vector<float>& bands,
                                  float beat_strength) {
    events::BeatDetectedEvent beat_event{beat_strength};
    event_bus_.publish(beat_event);

    events::FrameUpdateEvent frame_event{delta_time, metrics, bands, beat_strength};
    event_bus_.publish(frame_event);
}

void AnimationManager::render_all(notcurses* nc) {
    std::sort(animations_.begin(), animations_.end(), [](const auto& a, const auto& b) {
        return a->animation->get_z_index() < b->animation->get_z_index();
    });

    for (const auto& managed_anim : animations_) {
        if (auto* plane = managed_anim->animation->get_plane()) {
            ncplane_move_bottom(plane);
        }
    }

    for (const auto& managed_anim : animations_) {
        if (managed_anim->animation->is_active()) {
            managed_anim->animation->render(nc);
        }
    }
}

} // namespace animations
} // namespace why

