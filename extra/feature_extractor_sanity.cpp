#include <iomanip>
#include <iostream>
#include <vector>

#include "audio/feature_extractor.h"

int main() {
    using when::FeatureExtractor;

    FeatureExtractor extractor;
    std::vector<float> synthetic_bands = {0.9f, 0.8f, 0.2f, 0.1f, 0.05f, 0.02f, 0.01f, 0.005f};

    const float beat_strength = 0.5f;
    const auto features = extractor.process(synthetic_bands, beat_strength);

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
