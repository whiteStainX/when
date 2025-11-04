#include "pleasure.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <notcurses/notcurses.h>

#include "dsp.h"

namespace why {

PleasureVisualizer::PleasureVisualizer(PleasureConfig config)
    : config_(config) {}

void PleasureVisualizer::render(struct ncplane* plane, const DspEngine& dsp) {
    if (!plane) {
        return;
    }

    unsigned int dimy = 0;
    unsigned int dimx = 0;
    ncplane_dim_yx(plane, &dimy, &dimx);
    if (dimy == 0 || dimx == 0) {
        return;
    }

    const auto& bands = dsp.band_energies();
    if (bands.empty()) {
        return;
    }

    const std::size_t line_count = std::min<std::size_t>(config_.line_count, dimy);
    if (line_count == 0) {
        return;
    }

    const std::size_t bands_per_line = std::max<std::size_t>(1, bands.size() / line_count);

    std::string row(dimx, '#');
    const float spacing = static_cast<float>(dimy) / static_cast<float>(line_count + 1);

    for (std::size_t line = 0; line < line_count; ++line) {
        const std::size_t start_idx = line * bands_per_line;
        const std::size_t end_idx = std::min<std::size_t>(start_idx + bands_per_line, bands.size());
        if (start_idx >= end_idx) {
            break;
        }

        float magnitude = 0.0f;
        for (std::size_t i = start_idx; i < end_idx; ++i) {
            magnitude += bands[i];
        }
        magnitude /= static_cast<float>(end_idx - start_idx);

        const float base_y = (static_cast<float>(line) + 1.0f) * spacing;
        const float offset = magnitude * config_.amplitude_scale;
        int draw_y = static_cast<int>(std::round(base_y - offset));
        draw_y = std::clamp(draw_y, 0, static_cast<int>(dimy) - 1);

        ncplane_putstr_yx(plane, draw_y, 0, row.c_str());
    }
}

} // namespace why
