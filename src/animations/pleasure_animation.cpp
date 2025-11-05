#include "pleasure_animation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <random>
#include <vector>

#include "animation_event_utils.h"

namespace when {
namespace animations {

namespace {
constexpr unsigned int kDefaultPlaneRows = 24;
constexpr unsigned int kDefaultPlaneCols = 48;
constexpr int kBrailleRowsPerCell = 4;
constexpr int kBrailleColsPerCell = 2;
constexpr float kMagnitudeScale = 4.5f;
constexpr float kHistorySmoothing = 0.2f;
constexpr float kGlobalEnvelopeSmoothing = 0.08f;
constexpr float kProfileSmoothing = 0.25f;
constexpr float kRidgeMagnitudeSmoothing = 0.18f;
constexpr float kRidgePositionSmoothing = 0.12f;
constexpr float kCenterBandWidth = 0.38f;
constexpr float kRidgeSigma = 0.035f;
constexpr float kRidgePositionJitter = 0.045f;
constexpr float kRidgeMagnitudeJitter = 0.35f;
constexpr float kRidgeIntervalMin = 0.35f;
constexpr float kRidgeIntervalMax = 0.75f;
constexpr int kMinRidges = 3;
constexpr int kMaxRidges = 5;
} // namespace

PleasureAnimation::PleasureAnimation()
    : rng_(std::random_device{}()) {}

PleasureAnimation::~PleasureAnimation() {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }
}

void PleasureAnimation::init(notcurses* nc, const AppConfig& config) {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }

    z_index_ = 0;
    is_active_ = true;

    unsigned int desired_rows = kDefaultPlaneRows;
    unsigned int desired_cols = kDefaultPlaneCols;
    int desired_y = 0;
    int desired_x = 0;
    bool custom_origin_y = false;
    bool custom_origin_x = false;

    for (const auto& anim_config : config.animations) {
        if (anim_config.type == "Pleasure") {
            z_index_ = anim_config.z_index;
            is_active_ = anim_config.initially_active;

            if (anim_config.plane_rows) {
                desired_rows = std::max(1, *anim_config.plane_rows);
            }
            if (anim_config.plane_cols) {
                desired_cols = std::max(1, *anim_config.plane_cols);
            }
            if (anim_config.plane_y) {
                desired_y = *anim_config.plane_y;
                custom_origin_y = true;
            }
            if (anim_config.plane_x) {
                desired_x = *anim_config.plane_x;
                custom_origin_x = true;
            }
            break;
        }
    }

    desired_rows = std::max<unsigned int>(1, desired_rows);
    desired_cols = std::max<unsigned int>(1, desired_cols);

    ncplane* stdplane = notcurses_stdplane(nc);
    if (!stdplane) {
        return;
    }

    unsigned int std_rows = 0;
    unsigned int std_cols = 0;
    ncplane_dim_yx(stdplane, &std_rows, &std_cols);

    plane_rows_ = std::min(desired_rows, std_rows > 0u ? std_rows : desired_rows);
    plane_cols_ = std::min(desired_cols, std_cols > 0u ? std_cols : desired_cols);

    if (std_rows > 0u) {
        int max_origin_y = std::max(0, static_cast<int>(std_rows) - static_cast<int>(plane_rows_));
        plane_origin_y_ = custom_origin_y ? std::clamp(desired_y, 0, max_origin_y) : max_origin_y / 2;
    } else {
        plane_origin_y_ = custom_origin_y ? desired_y : 0;
    }

    if (std_cols > 0u) {
        int max_origin_x = std::max(0, static_cast<int>(std_cols) - static_cast<int>(plane_cols_));
        plane_origin_x_ = custom_origin_x ? std::clamp(desired_x, 0, max_origin_x) : max_origin_x / 2;
    } else {
        plane_origin_x_ = custom_origin_x ? desired_x : 0;
    }

    create_or_resize_plane(nc);

    configure_history_capacity();
    last_magnitude_ = 0.0f;
    global_magnitude_ = 0.0f;
}

void PleasureAnimation::update(float delta_time,
                               const AudioMetrics& /*metrics*/,
                               const std::vector<float>& bands,
                               float /*beat_strength*/) {
    if (history_capacity_ < 2u) {
        return;
    }

    float magnitude = 0.0f;
    if (!bands.empty()) {
        const std::size_t slice_size = std::max<std::size_t>(1u, bands.size() / 8u);
        const std::size_t upper = std::min<std::size_t>(bands.size(), slice_size);
        if (upper > 0u) {
            float sum = 0.0f;
            for (std::size_t i = 0; i < upper; ++i) {
                sum += std::abs(bands[i]);
            }
            magnitude = sum / static_cast<float>(upper);
        }
    }

    const float smoothed = (1.0f - kHistorySmoothing) * last_magnitude_ +
                           kHistorySmoothing * magnitude;
    last_magnitude_ = smoothed;

    const float normalized_magnitude = std::clamp(smoothed * kMagnitudeScale, 0.0f, 1.0f);
    global_magnitude_ += (normalized_magnitude - global_magnitude_) * kGlobalEnvelopeSmoothing;

    const float center_band_start = 0.5f - kCenterBandWidth * 0.5f;
    const float center_band_end = 0.5f + kCenterBandWidth * 0.5f;

    for (auto& ridge : ridges_) {
        ridge.noise_timer += delta_time;
        if (ridge.noise_timer >= ridge.noise_interval) {
            const float jitter = random_between(-kRidgePositionJitter, kRidgePositionJitter);
            ridge.target_pos = std::clamp(ridge.target_pos + jitter, center_band_start, center_band_end);

            const float magnitude_jitter = 1.0f + random_between(-kRidgeMagnitudeJitter, kRidgeMagnitudeJitter);
            ridge.target_magnitude = std::clamp(global_magnitude_ * magnitude_jitter, 0.0f, 1.0f);

            ridge.noise_timer = 0.0f;
            ridge.noise_interval = random_between(kRidgeIntervalMin, kRidgeIntervalMax);
        }

        ridge.current_pos += (ridge.target_pos - ridge.current_pos) * kRidgePositionSmoothing;
        ridge.current_magnitude += (ridge.target_magnitude - ridge.current_magnitude) * kRidgeMagnitudeSmoothing;
    }

    if (history_capacity_ == 0u || line_profile_.size() != history_capacity_) {
        line_profile_.assign(history_capacity_, 0.0f);
    }

    const float base_level = global_magnitude_ * 0.08f;
    const float two_sigma_sq = 2.0f * kRidgeSigma * kRidgeSigma;

    for (std::size_t i = 0; i < history_capacity_; ++i) {
        const float x_norm = (history_capacity_ > 1u)
                                 ? static_cast<float>(i) / static_cast<float>(history_capacity_ - 1u)
                                 : 0.0f;

        float ridge_sum = 0.0f;
        for (const auto& ridge : ridges_) {
            const float dx = x_norm - ridge.current_pos;
            const float gaussian = std::exp(-(dx * dx) / std::max(two_sigma_sq, 1e-6f));
            ridge_sum += ridge.current_magnitude * gaussian;
        }

        const float target_value = std::clamp(base_level + ridge_sum, 0.0f, 1.0f);
        line_profile_[i] += (target_value - line_profile_[i]) * kProfileSmoothing;
    }
}

void PleasureAnimation::render(notcurses* /*nc*/) {
    if (!plane_ || !is_active_) {
        return;
    }

    ncplane_erase(plane_);

    unsigned int rows = 0;
    unsigned int cols = 0;
    ncplane_dim_yx(plane_, &rows, &cols);
    if (rows == 0u || cols == 0u) {
        return;
    }

    const int pixel_rows = static_cast<int>(rows) * kBrailleRowsPerCell;
    const int pixel_cols = static_cast<int>(cols) * kBrailleColsPerCell;

    if (pixel_rows <= 0 || pixel_cols <= 0) {
        return;
    }

    std::vector<uint8_t> braille_cells(rows * cols, 0);

    const std::size_t profile_size = line_profile_.size();
    if (profile_size < 2u) {
        return;
    }

    const float max_index = static_cast<float>(profile_size - 1u);
    const float max_x = static_cast<float>(pixel_cols - 1);

    const int baseline = pixel_rows / 2;
    const int amplitude_range = std::max(1, std::min(baseline, pixel_rows - 1 - baseline));

    for (std::size_t j = 0; j + 1 < profile_size; ++j) {
        const float sample_a = std::clamp(line_profile_[j], 0.0f, 1.0f);
        const float sample_b = std::clamp(line_profile_[j + 1], 0.0f, 1.0f);

        const float centered_a = sample_a * 2.0f - 1.0f;
        const float centered_b = sample_b * 2.0f - 1.0f;

        int y1 = baseline - static_cast<int>(std::lround(centered_a * amplitude_range));
        int y2 = baseline - static_cast<int>(std::lround(centered_b * amplitude_range));

        y1 = std::clamp(y1, 0, pixel_rows - 1);
        y2 = std::clamp(y2, 0, pixel_rows - 1);

        const float x_norm_a = static_cast<float>(j) / std::max(1.0f, max_index);
        const float x_norm_b = static_cast<float>(j + 1u) / std::max(1.0f, max_index);

        int x1 = static_cast<int>(std::lround(x_norm_a * max_x));
        int x2 = static_cast<int>(std::lround(x_norm_b * max_x));

        x1 = std::clamp(x1, 0, pixel_cols - 1);
        x2 = std::clamp(x2, 0, pixel_cols - 1);

        draw_line(braille_cells, rows, cols, y1, x1, y2, x2);
    }

    blit_braille_cells(plane_, braille_cells, rows, cols);
}

void PleasureAnimation::activate() {
    is_active_ = true;
}

void PleasureAnimation::deactivate() {
    is_active_ = false;
    if (plane_) {
        ncplane_erase(plane_);
    }
}

void PleasureAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    bind_standard_frame_updates(this, config, bus);
}

void PleasureAnimation::initialize_ridges() {
    ridges_.clear();

    if (history_capacity_ < 2u) {
        return;
    }

    const float center_band_start = 0.5f - kCenterBandWidth * 0.5f;
    const float center_band_end = 0.5f + kCenterBandWidth * 0.5f;

    std::uniform_int_distribution<int> ridge_count_dist(kMinRidges, kMaxRidges);
    const int ridge_count = ridge_count_dist(rng_);

    for (int i = 0; i < ridge_count; ++i) {
        const float pos = random_between(center_band_start, center_band_end);
        PleasureAnimation::RidgeState ridge{};
        ridge.current_pos = pos;
        ridge.target_pos = pos;
        ridge.current_magnitude = 0.0f;
        ridge.target_magnitude = 0.0f;
        ridge.noise_timer = random_between(0.0f, kRidgeIntervalMin);
        ridge.noise_interval = random_between(kRidgeIntervalMin, kRidgeIntervalMax);
        ridges_.push_back(ridge);
    }
}

float PleasureAnimation::random_between(float min_value, float max_value) {
    if (min_value > max_value) {
        std::swap(min_value, max_value);
    }
    std::uniform_real_distribution<float> dist(min_value, max_value);
    return dist(rng_);
}

void PleasureAnimation::draw_line(std::vector<uint8_t>& cells,
                                  unsigned int cell_rows,
                                  unsigned int cell_cols,
                                  int y1,
                                  int x1,
                                  int y2,
                                  int x2) {
    if (cells.empty() || cell_rows == 0u || cell_cols == 0u) {
        return;
    }

    const int pixel_rows = static_cast<int>(cell_rows) * kBrailleRowsPerCell;
    const int pixel_cols = static_cast<int>(cell_cols) * kBrailleColsPerCell;
    if (pixel_rows <= 0 || pixel_cols <= 0) {
        return;
    }

    auto clamp_coord = [](int value, int max_value) {
        if (max_value <= 0) {
            return 0;
        }
        if (value < 0) {
            return 0;
        }
        if (value >= max_value) {
            return max_value - 1;
        }
        return value;
    };

    x1 = clamp_coord(x1, pixel_cols);
    x2 = clamp_coord(x2, pixel_cols);
    y1 = clamp_coord(y1, pixel_rows);
    y2 = clamp_coord(y2, pixel_rows);

    int x = x1;
    int y = y1;
    const int dx = std::abs(x2 - x1);
    const int sx = (x1 < x2) ? 1 : ((x1 > x2) ? -1 : 0);
    const int dy = -std::abs(y2 - y1);
    const int sy = (y1 < y2) ? 1 : ((y1 > y2) ? -1 : 0);
    int err = dx + dy;

    while (true) {
        if (x >= 0 && x < pixel_cols && y >= 0 && y < pixel_rows) {
            set_braille_pixel(cells, cell_cols, y, x);
        }

        if (x == x2 && y == y2) {
            break;
        }

        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

void PleasureAnimation::set_braille_pixel(std::vector<uint8_t>& cells,
                                          unsigned int cell_cols,
                                          int y,
                                          int x) const {
    if (cells.empty() || cell_cols == 0u || y < 0 || x < 0) {
        return;
    }

    const unsigned int cell_y = static_cast<unsigned int>(y / kBrailleRowsPerCell);
    const unsigned int cell_x = static_cast<unsigned int>(x / kBrailleColsPerCell);
    const unsigned int cell_rows = static_cast<unsigned int>(cells.size() / cell_cols);
    if (cell_y >= cell_rows || cell_x >= cell_cols) {
        return;
    }

    static constexpr uint8_t kDotMasks[kBrailleRowsPerCell][kBrailleColsPerCell] = {
        {0x01, 0x08},
        {0x02, 0x10},
        {0x04, 0x20},
        {0x40, 0x80},
    };

    const int sub_y = y % kBrailleRowsPerCell;
    const int sub_x = x % kBrailleColsPerCell;
    const unsigned int index = cell_y * cell_cols + cell_x;
    cells[index] |= kDotMasks[sub_y][sub_x];
}

void PleasureAnimation::blit_braille_cells(ncplane* plane,
                                           const std::vector<uint8_t>& cells,
                                           unsigned int cell_rows,
                                           unsigned int cell_cols) const {
    if (!plane || cells.empty() || cell_rows == 0u || cell_cols == 0u) {
        return;
    }

    const unsigned int expected_size = cell_rows * cell_cols;
    if (cells.size() < expected_size) {
        return;
    }

    for (unsigned int row = 0; row < cell_rows; ++row) {
        for (unsigned int col = 0; col < cell_cols; ++col) {
            const uint8_t mask = cells[row * cell_cols + col];
            if (mask == 0u) {
                continue;
            }

            const wchar_t ch = static_cast<wchar_t>(0x2800u + mask);
            ncplane_putwc_yx(plane, row, col, ch);
        }
    }
}

void PleasureAnimation::create_or_resize_plane(notcurses* nc) {
    if (!nc) {
        return;
    }

    ncplane* stdplane = notcurses_stdplane(nc);
    if (!stdplane) {
        return;
    }

    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }

    if (plane_rows_ == 0u || plane_cols_ == 0u) {
        return;
    }

    ncplane_options opts{};
    opts.rows = plane_rows_;
    opts.cols = plane_cols_;
    opts.y = plane_origin_y_;
    opts.x = plane_origin_x_;

    plane_ = ncplane_create(stdplane, &opts);

    if (plane_) {
        ncplane_dim_yx(plane_, &plane_rows_, &plane_cols_);
    }
}

void PleasureAnimation::configure_history_capacity() {
    history_capacity_ = 0u;

    if (plane_) {
        const std::size_t pixel_cols = static_cast<std::size_t>(plane_cols_) *
                                       static_cast<std::size_t>(kBrailleColsPerCell);
        history_capacity_ = std::max<std::size_t>(2u, pixel_cols);
    }

    line_profile_.assign(history_capacity_, 0.0f);
    initialize_ridges();
}

} // namespace animations
} // namespace when

