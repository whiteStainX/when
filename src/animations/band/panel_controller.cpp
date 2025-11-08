#include "panel_controller.h"

#include <algorithm>
#include <utility>

namespace when::animations::band {

PanelController::PanelController(MemberRole role, MemberConfig config, SpriteSet sprites)
    : role_(role)
    , config_(std::move(config))
    , sprites_(std::move(sprites))
    , heuristics_(role)
    , fsm_(config_) {}

PanelController::~PanelController() {
    destroy_plane();
}

PanelController::PanelController(PanelController&& other) noexcept
    : nc_(other.nc_)
    , plane_(other.plane_)
    , origin_x_(other.origin_x_)
    , origin_y_(other.origin_y_)
    , width_(other.width_)
    , height_(other.height_)
    , border_(other.border_)
    , role_(other.role_)
    , config_(other.config_)
    , sprites_(std::move(other.sprites_))
    , player_(other.player_)
    , heuristics_(role_)
    , fsm_(config_)
    , last_state_(other.last_state_)
    , debug_info_(other.debug_info_)
    , title_(std::move(other.title_)) {
    other.nc_ = nullptr;
    other.plane_ = nullptr;

    update_sequence(last_state_);
}

PanelController& PanelController::operator=(PanelController&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_plane();

    nc_ = other.nc_;
    plane_ = other.plane_;
    origin_x_ = other.origin_x_;
    origin_y_ = other.origin_y_;
    width_ = other.width_;
    height_ = other.height_;
    border_ = other.border_;
    role_ = other.role_;
    config_ = other.config_;
    sprites_ = std::move(other.sprites_);
    player_ = other.player_;
    heuristics_.reset();
    fsm_ = InstrumentStateMachine(config_);
    last_state_ = other.last_state_;
    debug_info_ = other.debug_info_;
    title_ = std::move(other.title_);

    other.nc_ = nullptr;
    other.plane_ = nullptr;

    update_sequence(last_state_);

    return *this;
}

void PanelController::destroy_plane() {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }
    nc_ = nullptr;
}

void PanelController::init(notcurses* nc, int x, int y, int width, int height, bool border) {
    destroy_plane();

    nc_ = nc;
    origin_x_ = x;
    origin_y_ = y;
    width_ = width;
    height_ = height;
    border_ = border;

    ncplane_options opts{};
    opts.y = origin_y_;
    opts.x = origin_x_;
    opts.rows = std::max(1, height_);
    opts.cols = std::max(1, width_);
    opts.userptr = nullptr;
    opts.name = nullptr;
    opts.flags = 0;

    plane_ = ncplane_create(notcurses_stdplane(nc_), &opts);

    if (border_) {
        draw_border();
    }

    update_sequence(fsm_.state());
}

void PanelController::set_title(std::string title) {
    title_ = std::move(title);
}

void PanelController::update(const FeatureView& view, float dt) {
    debug_info_.activity = heuristics_.activity_score(view);
    debug_info_.spotlight = heuristics_.spotlight_score(view);
    debug_info_.state = fsm_.state();

    fsm_.update(dt,
                debug_info_.activity,
                debug_info_.spotlight,
                view.beat_now,
                view.bar_phase);

    const MemberState current_state = fsm_.state();
    if (current_state != last_state_) {
        update_sequence(current_state);
        last_state_ = current_state;
        debug_info_.state = current_state;
    }

    player_.update(dt, view.beat_phase, view.bar_phase);
}

void PanelController::render() {
    if (!plane_ || !player_.has_sequence()) {
        return;
    }

    const SpriteFrame& frame = player_.current();
    blit_frame(frame);
}

void PanelController::update_sequence(MemberState state) {
    const auto* sequence = sequence_for_state(sprites_, state);
    if (!sequence) {
        return;
    }
    player_.set_sequence(sequence);
    player_.set_fps(fps_for_state(config_, state));
}

void PanelController::draw_border() {
    if (!plane_) {
        return;
    }

    ncplane_erase(plane_);
    unsigned rows = 0;
    unsigned cols = 0;
    ncplane_dim_yx(plane_, &rows, &cols);

    const unsigned ystop = rows > 0 ? rows - 1 : 0;
    const unsigned xstop = cols > 0 ? cols - 1 : 0;

    nccell ul = NCCELL_INITIALIZER(' ', 0, 0);
    nccell ur = NCCELL_INITIALIZER(' ', 0, 0);
    nccell ll = NCCELL_INITIALIZER(' ', 0, 0);
    nccell lr = NCCELL_INITIALIZER(' ', 0, 0);
    nccell hl = NCCELL_INITIALIZER(' ', 0, 0);
    nccell vl = NCCELL_INITIALIZER(' ', 0, 0);
    ncplane_box(plane_, &ul, &ur, &ll, &lr, &hl, &vl, ystop, xstop, 0);

    if (!title_.empty()) {
        ncplane_putstr_yx(plane_, 0, 2, title_.c_str());
    }
}

void PanelController::blit_frame(const SpriteFrame& frame) {
    if (!plane_) {
        return;
    }

    const int start_y = border_ ? 1 : 0;
    const int start_x = border_ ? 1 : 0;

    for (int row = 0; row < frame.height && (start_y + row) < height_; ++row) {
        if (row >= static_cast<int>(frame.rows.size())) {
            break;
        }
        ncplane_putstr_yx(plane_, start_y + row, start_x, frame.rows[row].c_str());
    }
}

} // namespace when::animations::band
