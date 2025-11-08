#pragma once

#include <optional>
#include <string>

#include <notcurses/notcurses.h>

#include "instrument.h"

namespace when::animations::band {

struct PanelDebugInfo {
    float activity = 0.0f;
    float spotlight = 0.0f;
    MemberState state = MemberState::Idle;
};

class PanelController {
public:
    PanelController(MemberRole role, MemberConfig config, SpriteSet sprites);
    ~PanelController();

    PanelController(const PanelController&) = delete;
    PanelController& operator=(const PanelController&) = delete;
    PanelController(PanelController&&) noexcept;
    PanelController& operator=(PanelController&&) noexcept;

    void init(notcurses* nc, int x, int y, int width, int height, bool border);
    void set_title(std::string title);

    void update(const FeatureView& view, float dt);
    void render();

    const PanelDebugInfo& debug() const { return debug_info_; }
    MemberRole role() const { return role_; }

private:
    void destroy_plane();
    void update_sequence(MemberState state);
    void draw_border();
    void blit_frame(const SpriteFrame& frame);

    notcurses* nc_ = nullptr;
    ncplane* plane_ = nullptr;
    int origin_x_ = 0;
    int origin_y_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool border_ = true;

    MemberRole role_;
    MemberConfig config_;
    SpriteSet sprites_;

    SpritePlayer player_;
    InstrumentHeuristics heuristics_;
    InstrumentStateMachine fsm_;
    MemberState last_state_ = MemberState::Idle;

    PanelDebugInfo debug_info_;
    std::string title_;
};

} // namespace when::animations::band
