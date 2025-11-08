#include <gtest/gtest.h>

#include "animations/band/instrument.h"

using when::animations::band::FeatureView;
using when::animations::band::InstrumentHeuristics;
using when::animations::band::InstrumentStateMachine;
using when::animations::band::MemberConfig;
using when::animations::band::MemberRole;
using when::animations::band::MemberState;

namespace {
FeatureView make_view(float bass_instant,
                      float treble_instant,
                      float high_flux,
                      bool beat = false) {
    FeatureView view{};
    view.bass_instant = bass_instant;
    view.treble_instant = treble_instant;
    view.high_flux = high_flux;
    view.bass_beat = beat;
    view.treble_beat = beat;
    view.beat_now = beat;
    view.bar_phase = 0.25f;
    return view;
}

} // namespace

TEST(BandInstrumentStateMachineTest, DrummerTransitionsThroughStates) {
    MemberConfig config;
    InstrumentHeuristics heur(MemberRole::Drums);
    InstrumentStateMachine fsm(config);

    FeatureView quiet = make_view(0.05f, 0.05f, 0.05f, false);

    for (int i = 0; i < 20; ++i) {
        const float activity = heur.activity_score(quiet);
        const float spotlight = heur.spotlight_score(quiet);
        fsm.update(0.05f, activity, spotlight, quiet.beat_now, quiet.bar_phase);
    }

    EXPECT_EQ(fsm.state(), MemberState::Idle);

    FeatureView groove = make_view(0.35f, 0.35f, 0.3f, false);
    for (int i = 0; i < 20; ++i) {
        const float activity = heur.activity_score(groove);
        const float spotlight = heur.spotlight_score(groove);
        fsm.update(0.05f, activity, spotlight, groove.beat_now, groove.bar_phase);
    }

    EXPECT_EQ(fsm.state(), MemberState::Normal);

    FeatureView blast = make_view(0.9f, 0.9f, 0.9f, true);
    for (int i = 0; i < 20; ++i) {
        const float activity = heur.activity_score(blast);
        const float spotlight = heur.spotlight_score(blast);
        fsm.update(0.05f, activity, spotlight, blast.beat_now, blast.bar_phase);
    }

    EXPECT_EQ(fsm.state(), MemberState::Fast);
}

TEST(BandInstrumentStateMachineTest, SpotlightRequiresBeatAndFlux) {
    MemberConfig config;
    config.spotlight_score_in = 0.8f;
    InstrumentHeuristics heur(MemberRole::Drums);
    InstrumentStateMachine fsm(config);

    FeatureView view = make_view(0.8f, 0.8f, 0.7f, false);
    view.bar_phase = 0.0f;

    // Without beat trigger we should not enter spotlight.
    for (int i = 0; i < 10; ++i) {
        const float activity = heur.activity_score(view);
        const float spotlight = heur.spotlight_score(view);
        fsm.update(0.1f, activity, spotlight, view.beat_now, view.bar_phase);
    }
    EXPECT_NE(fsm.state(), MemberState::Spotlight);

    // Trigger beats and high flux, expect spotlight at beat boundary.
    view.beat_now = true;
    view.bass_beat = true;
    view.treble_beat = true;
    view.high_flux = 0.9f;
    view.bar_phase = 0.5f;

    const float activity = heur.activity_score(view);
    const float spotlight = heur.spotlight_score(view);
    fsm.update(0.1f, activity, spotlight, view.beat_now, view.bar_phase);

    EXPECT_EQ(fsm.state(), MemberState::Spotlight);
}
