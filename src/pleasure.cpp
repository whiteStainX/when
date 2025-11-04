#include "pleasure.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include <notcurses/notcurses.h>

#include "dsp.h"

namespace why {

PleasureVisualizer::PleasureVisualizer(PleasureConfig config)
    : config_(config)
    , rng_(std::random_device{}()) {}

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

    const float center_y = (static_cast<float>(dimy) - 1.0f) / 2.0f;
    const float spacing = (line_count > 1) ? static_cast<float>(dimy) / static_cast<float>(line_count + 1)
                                           : 0.0f;
    const float index_offset = (static_cast<float>(line_count) - 1.0f) / 2.0f;
    const float base_center_x = (static_cast<float>(dimx) - 1.0f) / 2.0f;
    const float clamped_peak_percent = std::clamp(config_.peak_width_percent, 0.05f, 1.0f);
    const float clamped_randomness = std::clamp(config_.randomness_factor, 0.0f, 1.0f);

    std::uniform_real_distribution<float> offset_dist(-1.0f, 1.0f);

    for (std::size_t line = 0; line < line_count; ++line) {
        const std::size_t start_idx = line * bands_per_line;
        const std::size_t end_idx = std::min<std::size_t>(start_idx + bands_per_line, bands.size());
        if (start_idx >= end_idx) {
            break;
        }

        float base_y = center_y;
        if (line_count > 1) {
            base_y += (static_cast<float>(line) - index_offset) * spacing;
        }
        base_y = std::clamp(base_y, 0.0f, static_cast<float>(dimy - 1));

        float random_offset = 0.0f;
        if (clamped_randomness > 0.0f) {
            const float max_offset = static_cast<float>(dimx) * clamped_randomness * 0.5f;
            random_offset = offset_dist(rng_) * max_offset;
        }
        const float center_x = base_center_x + random_offset;

        float half_peak_width = clamped_peak_percent * static_cast<float>(dimx) * 0.5f;
        half_peak_width = std::max(0.5f, half_peak_width);

        float start_x = center_x - half_peak_width;
        float end_x = center_x + half_peak_width;
        start_x = std::clamp(start_x, 0.0f, static_cast<float>(dimx - 1));
        end_x = std::clamp(end_x, 0.0f, static_cast<float>(dimx - 1));
        if (end_x < start_x) {
            std::swap(start_x, end_x);
        }

        const int start_col = std::max(0, static_cast<int>(std::floor(start_x)));
        const int end_col = std::min(static_cast<int>(dimx) - 1, static_cast<int>(std::ceil(end_x)));
        const float peak_width = std::max(1.0f, end_x - start_x);
        const std::size_t line_bins = std::max<std::size_t>(1, end_idx - start_idx);

        for (unsigned int x = 0; x < dimx; ++x) {
            float y = base_y;

            if (static_cast<int>(x) >= start_col && static_cast<int>(x) <= end_col) {
                const float local_x = static_cast<float>(x) - start_x;
                const float normalized = std::clamp(local_x / peak_width, 0.0f, 1.0f);
                const float envelope = std::max(0.0f, 1.0f - std::abs(2.0f * normalized - 1.0f));

                const float band_pos = normalized * static_cast<float>(line_bins - 1);
                const float band_floor = std::floor(band_pos);
                const std::size_t lower_index = start_idx + static_cast<std::size_t>(band_floor);
                const std::size_t upper_index = std::min(end_idx - 1, lower_index + 1);
                const float frac = band_pos - band_floor;
                const float magnitude = bands[lower_index] + (bands[upper_index] - bands[lower_index]) * frac;

                const float displacement = magnitude * config_.amplitude_scale * envelope;
                y -= displacement;
            }

            int draw_y = static_cast<int>(std::lround(y));
            draw_y = std::clamp(draw_y, 0, static_cast<int>(dimy) - 1);
            ncplane_putchar_yx(plane, draw_y, x, '#');
        }
    }
}

} // namespace why
