#include "pleasure_animation.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace when {
namespace animations {

namespace {
constexpr unsigned int kDefaultPlaneRows = 24;
constexpr unsigned int kDefaultPlaneCols = 48;
constexpr int kBrailleRowsPerCell = 4;
constexpr int kBrailleColsPerCell = 2;
} // namespace

PleasureAnimation::PleasureAnimation() = default;

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
}

void PleasureAnimation::update(float /*delta_time*/,
                               const AudioMetrics& /*metrics*/,
                               const std::vector<float>& /*bands*/,
                               float /*beat_strength*/) {
    // No-op for scaffolding step.
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

    const int start_y = std::clamp(10, 0, pixel_rows - 1);
    const int start_x = std::clamp(10, 0, pixel_cols - 1);
    const int end_y = std::clamp(70, 0, pixel_rows - 1);
    const int end_x = std::clamp(90, 0, pixel_cols - 1);

    std::vector<uint8_t> braille_cells(rows * cols, 0);
    draw_line(braille_cells, rows, cols, start_y, start_x, end_y, end_x);
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

void PleasureAnimation::bind_events(const AnimationConfig& /*config*/, events::EventBus& /*bus*/) {
    // No event subscriptions for scaffolding.
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

} // namespace animations
} // namespace when

