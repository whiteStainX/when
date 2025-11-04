#pragma once

#include <cstddef>

#include "visualizer.h"

namespace why {

struct PleasureConfig {
    std::size_t line_count = 32;
    float amplitude_scale = 10.0f;
};

class PleasureVisualizer : public Visualizer {
public:
    explicit PleasureVisualizer(PleasureConfig config = {});

    void render(struct ncplane* plane, const DspEngine& dsp) override;

private:
    PleasureConfig config_;
};

} // namespace why
