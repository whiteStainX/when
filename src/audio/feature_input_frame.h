#pragma once

#include <span>
#include <utility>

namespace when {

struct FeatureInputFrame {
    std::span<const float> fft_magnitudes;            // Raw FFT magnitudes for each bin
    std::span<const float> fft_phases;                // Optional FFT phase information per bin
    std::span<const float> instantaneous_band_energies; // Unsmoothed log-band magnitudes
    std::span<const float> smoothed_band_energies;    // Optional pre-smoothed magnitudes (may be empty)
    std::span<const float> band_flux;                 // Per-band spectral flux deltas
    std::span<const std::pair<std::size_t, std::size_t>> band_bin_ranges; // Mapping of bands to FFT bins
    float sample_rate = 0.0f;                         // Sample rate associated with this frame
    float beat_strength = 0.0f;                       // Beat strength calculated by DSP
};

} // namespace when
