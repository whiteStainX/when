#pragma once

#include <cstddef>
#include <optional>

#include "feature_taps.h"
#include "sprite_types.h"

namespace when::animations::band {

enum class MemberRole {
    Guitar,
    Bass,
    Drums,
    Vocal,
};

enum class MemberState {
    Idle,
    Normal,
    Fast,
    Spotlight,
};

struct MemberConfig {
    float idle_floor = 0.06f;
    float fast_in = 0.65f;
    float fast_out = 0.45f;
    float spotlight_score_in = 0.9f;
    float spotlight_score_out = 0.55f;
    float spotlight_min_bars = 1.0f;
    float idle_hold_sec = 0.8f;
    float fast_hold_sec = 0.6f;
    float fps_idle = 2.0f;
    float fps_normal = 6.0f;
    float fps_fast = 10.0f;
    float fps_spot = 8.0f;
};

class InstrumentHeuristics {
public:
    explicit InstrumentHeuristics(MemberRole role);

    void reset();

    float activity_score(const FeatureView& view);
    float spotlight_score(const FeatureView& view);

private:
    MemberRole role_;
    float last_bar_phase_ = 0.0f;
    int beats_this_bar_ = 0;
    bool first_frame_ = true;
};

class InstrumentStateMachine {
public:
    explicit InstrumentStateMachine(const MemberConfig& cfg);

    void update(float dt,
                float activity,
                float spotlight,
                bool beat_now,
                float bar_phase);

    MemberState state() const { return state_; }
    void force_spotlight(float bars = 0.0f);
    void end_spotlight();

private:
    void handle_idle(float dt, float activity);
    void handle_normal(float dt, float activity);
    void handle_fast(float dt, float activity);
    void handle_spotlight(float dt, float spotlight, float bar_phase);

    MemberConfig cfg_;
    MemberState state_ = MemberState::Idle;

    float above_idle_timer_ = 0.0f;
    float below_idle_timer_ = 0.0f;
    float above_fast_timer_ = 0.0f;
    float below_fast_timer_ = 0.0f;

    float spotlight_bars_elapsed_ = 0.0f;
    float last_bar_phase_ = 0.0f;
    bool spotlight_locked_ = false;
};

const std::vector<SpriteFrame>* sequence_for_state(const SpriteSet& set, MemberState state);

float fps_for_state(const MemberConfig& cfg, MemberState state);

} // namespace when::animations::band
