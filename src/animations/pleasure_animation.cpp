#include "pleasure_animation.h"

#include <algorithm>
#include <cwchar>

namespace when {
namespace animations {

namespace {
constexpr unsigned int kDefaultPlaneRows = 24;
constexpr unsigned int kDefaultPlaneCols = 48;
constexpr wchar_t kBrailleLineChar = L'\u2840';
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

    unsigned int center_row = rows / 2;
    for (unsigned int x = 0; x < cols; ++x) {
        ncplane_putwc_yx(plane_, static_cast<int>(center_row), static_cast<int>(x), kBrailleLineChar);
    }
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

