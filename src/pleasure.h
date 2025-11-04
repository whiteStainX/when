#pragma once

#include <cstddef>
#include <random>

#include "visualizer.h"

namespace why {

struct PleasureConfig {
    std::size_t line_count = 32;
    float amplitude_scale = 10.0f;
    float peak_width_percent = 0.5f;
    float randomness_factor = 0.2f;
};

class PleasureVisualizer : public Visualizer {
public:
    explicit PleasureVisualizer(PleasureConfig config = {});

    void render(struct ncplane* plane, const DspEngine& dsp) override;

private:
    PleasureConfig config_;
    std::mt19937 rng_;
};

} // namespace why
