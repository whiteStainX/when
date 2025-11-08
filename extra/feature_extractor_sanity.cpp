#include <iomanip>
#include <iostream>
#include <vector>

#include "audio/feature_extractor.h"
#include "audio/feature_input_frame.h"

int main() {
    using when::FeatureExtractor;

    FeatureExtractor extractor;
    std::vector<float> synthetic_bands = {0.9f, 0.8f, 0.2f, 0.1f, 0.05f, 0.02f, 0.01f, 0.005f};
    extractor.prepare(synthetic_bands.size());

    std::vector<float> fft_magnitudes(synthetic_bands.size(), 0.0f);
    std::vector<float> fft_phases(synthetic_bands.size(), 0.0f);
    std::vector<float> band_flux(synthetic_bands.size(), 0.0f);

    when::FeatureInputFrame input{};
    input.smoothed_band_energies = synthetic_bands;
    input.instantaneous_band_energies = synthetic_bands;
    input.fft_magnitudes = fft_magnitudes;
    input.fft_phases = fft_phases;
    input.band_flux = band_flux;
    input.sample_rate = 48000.0f;
    input.beat_strength = 0.5f;

    const auto features = extractor.process(input);

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Bass: " << features.bass_energy << "\n";
    std::cout << "Mid: " << features.mid_energy << "\n";
    std::cout << "Treble: " << features.treble_energy << "\n";
    std::cout << "Total: " << features.total_energy << "\n";
    std::cout << "Centroid: " << features.spectral_centroid << "\n";
    std::cout << "Beat detected: " << std::boolalpha << features.beat_detected << "\n";
    std::cout << "Beat strength: " << features.beat_strength << "\n";

    return 0;
}
