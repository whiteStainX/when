#include "instrument.h"

#include <algorithm>

namespace when::animations::band {

namespace {
float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float drummer_activity(const FeatureView& view) {
    const float beat_bonus = (view.bass_beat || view.treble_beat) ? 1.0f : 0.0f;
    const float base = 0.35f * (view.bass_instant + view.treble_instant);
    const float flux = 0.3f * view.high_flux;
    const float beat = 0.35f * beat_bonus;
    return clamp01(base + flux + beat);
}

float drummer_spotlight(const FeatureView& view,
                        int beats_this_bar,
                        float spotlight_threshold) {
    float beat_factor = clamp01(static_cast<float>(beats_this_bar) / spotlight_threshold);
    if (view.beat_now) {
        beat_factor = 1.0f;
    }

    const float flux_factor = clamp01((view.high_flux - 0.5f) / 0.35f);
    const float combined = 0.6f * flux_factor + 0.4f * beat_factor;
    return std::min(0.85f, combined);
}

} // namespace

InstrumentHeuristics::InstrumentHeuristics(MemberRole role)
    : role_(role) {}

void InstrumentHeuristics::reset() {
    last_bar_phase_ = 0.0f;
    beats_this_bar_ = 0;
    first_frame_ = true;
}

float InstrumentHeuristics::activity_score(const FeatureView& view) {
    switch (role_) {
    case MemberRole::Drums:
        return drummer_activity(view);
    default:
        return clamp01(view.total_energy);
    }
}

float InstrumentHeuristics::spotlight_score(const FeatureView& view) {
    if (first_frame_) {
        last_bar_phase_ = view.bar_phase;
        beats_this_bar_ = 0;
        first_frame_ = false;
    }

    if (view.bar_phase < last_bar_phase_) {
        beats_this_bar_ = 0;
    }

    if (view.bass_beat || view.treble_beat) {
        ++beats_this_bar_;
    }

    last_bar_phase_ = view.bar_phase;

    switch (role_) {
    case MemberRole::Drums:
        return drummer_spotlight(view, beats_this_bar_, 4.0f);
    default:
        return clamp01(view.total_energy);
    }
}

InstrumentStateMachine::InstrumentStateMachine(const MemberConfig& cfg)
    : cfg_(cfg) {}

void InstrumentStateMachine::update(float dt,
                                    float activity,
                                    float spotlight,
                                    bool beat_now,
                                    float bar_phase) {
    switch (state_) {
    case MemberState::Idle:
        handle_idle(dt, activity);
        break;
    case MemberState::Normal:
        handle_normal(dt, activity);
        break;
    case MemberState::Fast:
        handle_fast(dt, activity);
        break;
    case MemberState::Spotlight:
        handle_spotlight(dt, spotlight, bar_phase);
        break;
    }

    if (state_ != MemberState::Spotlight && beat_now && spotlight >= cfg_.spotlight_score_in) {
        state_ = MemberState::Spotlight;
        spotlight_locked_ = true;
        spotlight_bars_elapsed_ = 0.0f;
        last_bar_phase_ = bar_phase;
    }
}

void InstrumentStateMachine::force_spotlight(float bars) {
    state_ = MemberState::Spotlight;
    spotlight_locked_ = true;
    spotlight_bars_elapsed_ = std::max(0.0f, bars);
}

void InstrumentStateMachine::end_spotlight() {
    if (state_ == MemberState::Spotlight) {
        state_ = MemberState::Normal;
        spotlight_locked_ = false;
        spotlight_bars_elapsed_ = 0.0f;
    }
}

void InstrumentStateMachine::handle_idle(float dt, float activity) {
    if (activity > cfg_.idle_floor) {
        above_idle_timer_ += dt;
        if (above_idle_timer_ >= cfg_.idle_hold_sec) {
            state_ = MemberState::Normal;
            above_idle_timer_ = 0.0f;
            below_idle_timer_ = 0.0f;
        }
    } else {
        above_idle_timer_ = 0.0f;
    }
}

void InstrumentStateMachine::handle_normal(float dt, float activity) {
    if (activity > cfg_.fast_in) {
        above_fast_timer_ += dt;
        if (above_fast_timer_ >= cfg_.fast_hold_sec) {
            state_ = MemberState::Fast;
            above_fast_timer_ = 0.0f;
            below_fast_timer_ = 0.0f;
        }
    } else {
        above_fast_timer_ = 0.0f;
    }

    if (activity < cfg_.idle_floor) {
        below_idle_timer_ += dt;
        if (below_idle_timer_ >= cfg_.idle_hold_sec) {
            state_ = MemberState::Idle;
            below_idle_timer_ = 0.0f;
            above_idle_timer_ = 0.0f;
        }
    } else {
        below_idle_timer_ = 0.0f;
    }
}

void InstrumentStateMachine::handle_fast(float dt, float activity) {
    if (activity < cfg_.fast_out) {
        below_fast_timer_ += dt;
        if (below_fast_timer_ >= cfg_.fast_hold_sec) {
            state_ = MemberState::Normal;
            below_fast_timer_ = 0.0f;
        }
    } else {
        below_fast_timer_ = 0.0f;
    }
}

void InstrumentStateMachine::handle_spotlight(float dt,
                                              float spotlight,
                                              float bar_phase) {
    (void)dt;
    if (bar_phase < last_bar_phase_) {
        spotlight_bars_elapsed_ += 1.0f;
    }
    last_bar_phase_ = bar_phase;

    if (!spotlight_locked_) {
        if (spotlight < cfg_.spotlight_score_out) {
            state_ = MemberState::Normal;
            spotlight_bars_elapsed_ = 0.0f;
        }
        return;
    }

    if (spotlight_bars_elapsed_ >= cfg_.spotlight_min_bars && spotlight < cfg_.spotlight_score_out) {
        spotlight_locked_ = false;
        state_ = MemberState::Normal;
        spotlight_bars_elapsed_ = 0.0f;
    }
}

const std::vector<SpriteFrame>* sequence_for_state(const SpriteSet& set, MemberState state) {
    switch (state) {
    case MemberState::Idle:
        return set.idle.empty() ? nullptr : &set.idle;
    case MemberState::Normal:
        return set.normal.empty() ? nullptr : &set.normal;
    case MemberState::Fast:
        return set.fast.empty() ? nullptr : &set.fast;
    case MemberState::Spotlight:
        if (!set.spotlight.empty()) {
            return &set.spotlight;
        }
        if (!set.spotlight_hi.empty()) {
            return &set.spotlight_hi;
        }
        return nullptr;
    }
    return nullptr;
}

float fps_for_state(const MemberConfig& cfg, MemberState state) {
    switch (state) {
    case MemberState::Idle:
        return cfg.fps_idle;
    case MemberState::Normal:
        return cfg.fps_normal;
    case MemberState::Fast:
        return cfg.fps_fast;
    case MemberState::Spotlight:
        return cfg.fps_spot;
    }
    return cfg.fps_normal;
}

} // namespace when::animations::band
