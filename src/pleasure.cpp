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

    ncplane_erase(plane);

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

    const float clamped_width_percent = std::clamp(config_.width_percent, 0.05f, 1.0f);
    const unsigned int visual_width = std::max<unsigned int>(
        1U, static_cast<unsigned int>(std::round(static_cast<float>(dimx) * clamped_width_percent)));
    const unsigned int clamped_visual_width = std::min<unsigned int>(visual_width, dimx);

    const unsigned int left_edge = (dimx - clamped_visual_width) / 2U;
    const unsigned int right_edge = left_edge + clamped_visual_width - 1U;

    const float center_y = (static_cast<float>(dimy) - 1.0f) / 2.0f;
    const float spacing = (line_count > 1) ? static_cast<float>(dimy) / static_cast<float>(line_count + 1)
                                           : 0.0f;
    const float index_offset = (static_cast<float>(line_count) - 1.0f) / 2.0f;
    const float base_center_x = static_cast<float>(left_edge + (clamped_visual_width - 1U) / 2.0f);
    const float clamped_peak_percent = std::clamp(config_.peak_width_percent, 0.05f, 1.0f);
    const float clamped_randomness = std::clamp(config_.randomness_factor, 0.0f, 1.0f);

    std::uniform_real_distribution<float> offset_dist(-1.0f, 1.0f);

    const std::size_t total_bands = bands.size();
    std::vector<float> line_magnitudes(line_count, 0.0f);

    if (total_bands == 1) {
        std::fill(line_magnitudes.begin(), line_magnitudes.end(), bands.front());
    } else {
        const float inv_total_bands = 1.0f / static_cast<float>(total_bands);

        for (std::size_t line = 0; line < line_count; ++line) {
            const float line_start = static_cast<float>(line) / static_cast<float>(line_count);
            const float line_end = static_cast<float>(line + 1) / static_cast<float>(line_count);

            float weighted_sum = 0.0f;
            float weight_total = 0.0f;
            float peak = 0.0f;

            for (std::size_t band = 0; band < total_bands; ++band) {
                const float band_start = static_cast<float>(band) * inv_total_bands;
                const float band_end = static_cast<float>(band + 1) * inv_total_bands;

                const float overlap_start = std::max(line_start, band_start);
                const float overlap_end = std::min(line_end, band_end);
                const float overlap = std::max(0.0f, overlap_end - overlap_start);

                if (overlap <= 0.0f) {
                    continue;
                }

                const float magnitude = bands[band];
                weighted_sum += magnitude * overlap;
                weight_total += overlap;
                peak = std::max(peak, magnitude);
            }

            if (weight_total > 0.0f) {
                const float average = weighted_sum / weight_total;
                line_magnitudes[line] = std::max(0.0f, average * 0.6f + peak * 0.4f);
            } else {
                const float normalized_line_center = (static_cast<float>(line) + 0.5f)
                                                     / static_cast<float>(line_count);
                const float exact_band = normalized_line_center * static_cast<float>(total_bands - 1);
                const std::size_t lower_band = static_cast<std::size_t>(std::floor(exact_band));
                const std::size_t upper_band = std::min<std::size_t>(total_bands - 1, lower_band + 1);
                const float fraction = exact_band - static_cast<float>(lower_band);
                const float lower_value = bands[lower_band];
                const float upper_value = bands[upper_band];
                line_magnitudes[line] = lower_value + (upper_value - lower_value) * fraction;
            }
        }
    }

    for (std::size_t line = 0; line < line_count; ++line) {
        const float line_overall_magnitude = line_magnitudes[line];

        float base_y = center_y;
        if (line_count > 1) {
            base_y += (static_cast<float>(line) - index_offset) * spacing;
        }
        base_y = std::clamp(base_y, 0.0f, static_cast<float>(dimy - 1));

        float random_offset = 0.0f;
        if (clamped_randomness > 0.0f) {
            const float max_offset = static_cast<float>(clamped_visual_width) * clamped_randomness * 0.5f;
            random_offset = offset_dist(rng_) * max_offset;
        }
        float center_x = base_center_x + random_offset;
        center_x = std::clamp(center_x, static_cast<float>(left_edge), static_cast<float>(right_edge));

        float half_peak_width = clamped_peak_percent * static_cast<float>(clamped_visual_width) * 0.5f;
        half_peak_width = std::max(0.5f, half_peak_width);

        float start_x = center_x - half_peak_width;
        float end_x = center_x + half_peak_width;
        start_x = std::clamp(start_x, static_cast<float>(left_edge), static_cast<float>(right_edge));
        end_x = std::clamp(end_x, static_cast<float>(left_edge), static_cast<float>(right_edge));
        if (end_x < start_x) {
            std::swap(start_x, end_x);
        }

        const int start_col = std::max(static_cast<int>(left_edge), static_cast<int>(std::floor(start_x)));
        const int end_col = std::min(static_cast<int>(right_edge), static_cast<int>(std::ceil(end_x)));
        const float peak_width = std::max(1.0f, end_x - start_x);

        for (unsigned int x = left_edge; x <= right_edge; ++x) {
            float y = base_y;

            if (static_cast<int>(x) >= start_col && static_cast<int>(x) <= end_col) {
                const float local_x = static_cast<float>(x) - start_x;
                const float normalized = std::clamp(local_x / peak_width, 0.0f, 1.0f);
                const float envelope = std::max(0.0f, 1.0f - std::abs(2.0f * normalized - 1.0f));

                const float displacement = line_overall_magnitude * config_.amplitude_scale * envelope;
                y -= displacement;
            }

            int draw_y = static_cast<int>(std::lround(y));
            draw_y = std::clamp(draw_y, 0, static_cast<int>(dimy) - 1);
            ncplane_putchar_yx(plane, draw_y, x, '#');
        }
    }
}

} // namespace why
