#pragma once

namespace when {

struct AudioFeatures {
    // Energy Bands
    float bass_energy = 0.0f;   // Energy in the low-frequency range
    float mid_energy = 0.0f;    // Energy in the mid-frequency range
    float treble_energy = 0.0f; // Energy in the high-frequency range
    float total_energy = 0.0f;  // Overall energy across all bands

    // Rhythmic Features
    bool beat_detected = false; // True for a single frame when a beat occurs
    float beat_strength = 0.0f; // Confidence/strength of the detected beat

    // Spectral Features
    float spectral_centroid = 0.0f; // Represents the "brightness" of the sound
};

} // namespace when
