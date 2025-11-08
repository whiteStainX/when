#pragma once

#include <span>

namespace when {

struct AudioFeatures {
    // Energy Bands
    float bass_energy = 0.0f;   // Smoothed energy in the low-frequency range
    float mid_energy = 0.0f;    // Smoothed energy in the mid-frequency range
    float treble_energy = 0.0f; // Smoothed energy in the high-frequency range
    float total_energy = 0.0f;  // Smoothed overall energy across all bands
    float bass_envelope = 0.0f;   // Explicit envelope follower for bass energy
    float mid_envelope = 0.0f;    // Explicit envelope follower for mid energy
    float treble_envelope = 0.0f; // Explicit envelope follower for treble energy

    // Instantaneous Energy Bands
    float bass_energy_instantaneous = 0.0f;   // Instantaneous low-frequency energy
    float mid_energy_instantaneous = 0.0f;    // Instantaneous mid-frequency energy
    float treble_energy_instantaneous = 0.0f; // Instantaneous high-frequency energy
    float total_energy_instantaneous = 0.0f;  // Instantaneous overall energy

    // Rhythmic Features
    bool beat_detected = false; // True for a single frame when a beat occurs
    float beat_strength = 0.0f; // Confidence/strength of the detected beat

    // Spectral Features
    float spectral_centroid = 0.0f; // Represents the "brightness" of the sound

    // Raw analysis context
    std::span<const float> band_flux; // Per-band spectral flux deltas from the DSP stage
};

} // namespace when
