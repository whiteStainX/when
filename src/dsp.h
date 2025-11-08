#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

#include "audio/audio_features.h"
#include "audio/feature_extractor.h"
#include "audio/feature_input_frame.h"

extern "C" {
#include <kiss_fft.h>
}

namespace when {

namespace events {
class EventBus;
} // namespace events

class DspEngine {
public:
    static constexpr std::size_t kDefaultFftSize = 1024;
    static constexpr std::size_t kDefaultHopSize = kDefaultFftSize / 2;
    static constexpr std::size_t kDefaultBands = 16;

    DspEngine(events::EventBus& event_bus,
              std::uint32_t sample_rate,
              std::uint32_t channels,
              std::size_t fft_size = kDefaultFftSize,
              std::size_t hop_size = kDefaultHopSize,
              std::size_t bands = kDefaultBands,
              FeatureExtractor::Config feature_config = FeatureExtractor::Config{});
    ~DspEngine();

    void push_samples(const float* interleaved_samples, std::size_t count);

    const AudioFeatures& audio_features() const { return latest_features_; }

private:
    void compute_band_ranges();
    void process_frame();

    events::EventBus& event_bus_;

    std::uint32_t sample_rate_;
    std::uint32_t channels_;
    std::size_t fft_size_;
    std::size_t hop_size_;

    std::vector<float> window_;
    std::vector<float> frame_buffer_;
    std::deque<float> mono_fifo_;

    std::vector<std::pair<std::size_t, std::size_t>> band_bin_ranges_;
    std::vector<float> prev_magnitudes_;
    std::vector<float> instantaneous_band_energies_;
    std::vector<float> band_flux_;
    std::vector<float> fft_magnitudes_;
    std::vector<float> fft_phases_;

    FeatureExtractor feature_extractor_;
    FeatureInputFrame feature_input_frame_{};
    AudioFeatures latest_features_{};

    kiss_fft_cfg fft_cfg_;
    std::vector<kiss_fft_cpx> fft_in_;
    std::vector<kiss_fft_cpx> fft_out_;

    float flux_average_;
    float beat_strength_;
};

} // namespace when
