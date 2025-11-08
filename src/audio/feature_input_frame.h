#pragma once

#include <span>

namespace when {

struct FeatureInputFrame {
    std::span<const float> fft_magnitudes;            // Raw FFT magnitudes for each bin
    std::span<const float> fft_phases;                // Optional FFT phase information per bin
    std::span<const float> instantaneous_band_energies; // Unsmoothed log-band magnitudes
    std::span<const float> smoothed_band_energies;    // Smoothed band magnitudes maintained by DSP
    std::span<const float> band_flux;                 // Per-band spectral flux deltas
    float beat_strength = 0.0f;                       // Beat strength calculated by DSP
};

} // namespace when
