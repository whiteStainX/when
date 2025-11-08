#include "ascii_matrix_animation.h"
#include "animation_event_utils.h"
#include "glyph_utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <numeric>
#include <random>
#include <sstream>

namespace when {
namespace animations {

namespace {
constexpr const char* kDefaultGlyphFilePath = "assets/ascii_matrix.txt";
constexpr const char* kDefaultGlyphs = R"( .:-=+*#%@)";
constexpr int kDefaultMatrixRows = 16;
constexpr int kDefaultMatrixCols = 32;
constexpr float kDefaultBeatBoost = 1.5f;
constexpr float kDefaultBeatThreshold = 0.6f;
constexpr int kMaxLaneCount = 4;
constexpr float kNoiseRefreshThreshold = 0.82f;
constexpr float kLaneResponseRate = 7.0f;
constexpr float kCellResponseRate = 11.0f;
constexpr float kHighlightDecayRate = 3.5f;
constexpr float kMinEnergyEpsilon = 1e-4f;

struct LaneStyle {
    float trigger_probability;
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

constexpr std::array<LaneStyle, kMaxLaneCount> kLaneStyles = {{
    {0.45f, 255u, 120u, 0u},   // Kick lane - warm orange
    {0.30f, 120u, 200u, 255u}, // Snare lane - icy blue
    {0.65f, 220u, 180u, 255u}, // Hi-hat lane - lavender
    {0.25f, 0u, 240u, 180u},   // Perc lane - teal
}};

} // namespace

AsciiMatrixAnimation::AsciiMatrixAnimation()
    : glyphs_(parse_glyphs(kDefaultGlyphs)),
      glyphs_file_path_(kDefaultGlyphFilePath),
      rng_(std::random_device{}()) {
    // Start from a clean state so repeated init() calls behave identically.
    reset_internal_state();
}

AsciiMatrixAnimation::~AsciiMatrixAnimation() {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }
}

void AsciiMatrixAnimation::init(notcurses* nc, const AppConfig& config) {
    if (plane_) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }

    // Reset all runtime state so a fresh init() always starts from the same
    // neutral baseline regardless of previous sessions.
    reset_internal_state();

    glyphs_file_path_ = kDefaultGlyphFilePath;
    show_border_ = true;
    beat_boost_ = kDefaultBeatBoost;
    beat_threshold_ = kDefaultBeatThreshold;
    configured_matrix_rows_ = kDefaultMatrixRows;
    configured_matrix_cols_ = kDefaultMatrixCols;
    matrix_rows_ = configured_matrix_rows_;
    matrix_cols_ = configured_matrix_cols_;

    ncplane* stdplane = notcurses_stdplane(nc);
    unsigned int std_rows = 0;
    unsigned int std_cols = 0;
    ncplane_dim_yx(stdplane, &std_rows, &std_cols);

    int desired_y = 0;
    int desired_x = 0;
    bool custom_origin_y = false;
    bool custom_origin_x = false;
    int desired_rows = static_cast<int>(matrix_rows_ + (show_border_ ? 2 : 0));
    int desired_cols = static_cast<int>(matrix_cols_ + (show_border_ ? 2 : 0));
    bool custom_rows = false;
    bool custom_cols = false;

    for (const auto& anim_config : config.animations) {
        if (anim_config.type == "AsciiMatrix") {
            z_index_ = anim_config.z_index;
            is_active_ = true; // Always active by design

            if (!anim_config.glyphs_file_path.empty()) {
                glyphs_file_path_ = anim_config.glyphs_file_path;
            } else if (!anim_config.text_file_path.empty()) {
                glyphs_file_path_ = anim_config.text_file_path;
            }

            if (anim_config.matrix_rows) {
                configured_matrix_rows_ = std::max(1, *anim_config.matrix_rows);
            }
            if (anim_config.matrix_cols) {
                configured_matrix_cols_ = std::max(1, *anim_config.matrix_cols);
            }

            matrix_rows_ = configured_matrix_rows_;
            matrix_cols_ = configured_matrix_cols_;

            show_border_ = anim_config.matrix_show_border;
            beat_boost_ = anim_config.matrix_beat_boost;
            beat_threshold_ = anim_config.matrix_beat_threshold;

            if (anim_config.plane_y) {
                desired_y = *anim_config.plane_y;
                custom_origin_y = true;
            }
            if (anim_config.plane_x) {
                desired_x = *anim_config.plane_x;
                custom_origin_x = true;
            }
            if (anim_config.plane_rows) {
                desired_rows = std::max(*anim_config.plane_rows, show_border_ ? 3 : 1);
                custom_rows = true;
            }
            if (anim_config.plane_cols) {
                desired_cols = std::max(*anim_config.plane_cols, show_border_ ? 3 : 1);
                custom_cols = true;
            }
            break;
        }
    }

    if (!custom_rows) {
        desired_rows = matrix_rows_ + (show_border_ ? 2 : 0);
    }
    if (!custom_cols) {
        desired_cols = matrix_cols_ + (show_border_ ? 2 : 0);
    }

    desired_rows = std::max(desired_rows, 1);
    desired_cols = std::max(desired_cols, 1);

    plane_rows_ = 0;
    plane_cols_ = 0;

    if (std_rows > 0u) {
        plane_rows_ = std::min<unsigned int>(std_rows, static_cast<unsigned int>(desired_rows));
    }
    if (std_cols > 0u) {
        plane_cols_ = std::min<unsigned int>(std_cols, static_cast<unsigned int>(desired_cols));
    }

    if (plane_rows_ == 0u && std_rows > 0u) {
        plane_rows_ = std_rows;
    }
    if (plane_cols_ == 0u && std_cols > 0u) {
        plane_cols_ = std_cols;
    }

    if (std_rows > 0u) {
        const int std_rows_i = static_cast<int>(std_rows);
        const int plane_rows_i = static_cast<int>(plane_rows_);
        const int max_origin_y = std::max(0, std_rows_i - plane_rows_i);
        if (custom_origin_y) {
            plane_origin_y_ = std::clamp(desired_y, 0, max_origin_y);
        } else {
            plane_origin_y_ = max_origin_y / 2;
        }
    } else {
        plane_origin_y_ = custom_origin_y ? desired_y : 0;
    }

    if (std_cols > 0u) {
        const int std_cols_i = static_cast<int>(std_cols);
        const int plane_cols_i = static_cast<int>(plane_cols_);
        const int max_origin_x = std::max(0, std_cols_i - plane_cols_i);
        if (custom_origin_x) {
            plane_origin_x_ = std::clamp(desired_x, 0, max_origin_x);
        } else {
            plane_origin_x_ = max_origin_x / 2;
        }
    } else {
        plane_origin_x_ = custom_origin_x ? desired_x : 0;
    }

    if (!load_glyphs_from_file(glyphs_file_path_)) {
        if (glyphs_file_path_ != kDefaultGlyphFilePath) {
            if (!load_glyphs_from_file(kDefaultGlyphFilePath)) {
                glyphs_ = parse_glyphs(kDefaultGlyphs);
            }
        } else {
            glyphs_ = parse_glyphs(kDefaultGlyphs);
        }
    }

    if (plane_rows_ == 0u || plane_cols_ == 0u) {
        plane_ = nullptr;
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
        ensure_dimensions_fit();
        if (!target_cells_.empty()) {
            refresh_pattern();
        }
    }
}

void AsciiMatrixAnimation::activate() {
    is_active_ = true;
}

void AsciiMatrixAnimation::deactivate() {
    is_active_ = false;
    if (plane_) {
        ncplane_erase(plane_);
    }
    reset_internal_state();
}

void AsciiMatrixAnimation::update(float delta_time,
                                  const AudioMetrics& /*metrics*/,
                                  const AudioFeatures& features) {
    if (!plane_ || !is_active_) {
        return;
    }

    // Always keep the cached plane size aligned with the terminal so our buffers
    // resize gracefully during window resizes.
    ncplane_dim_yx(plane_, &plane_rows_, &plane_cols_);
    ensure_dimensions_fit();

    if (matrix_rows_ <= 0 || matrix_cols_ <= 0) {
        return;
    }

    // Remember the most recent beat data so render() can colourise the grid.
    latest_beat_strength_ = features.beat_strength;
    latest_downbeat_ = features.downbeat;

    // The lane energy tracker converts high level audio features into the
    // per-row "instrument" intensities of our drum machine visual.
    update_lane_intensities(delta_time, features);

    // Track which sequencer step is currently active and decide whether the
    // pattern should be regenerated based on the spectral characteristics.
    update_step_highlight(delta_time, features);

    if (pattern_dirty_) {
        refresh_pattern();
    }

    // Finally smooth the raw targets into the matrix buffer that render()
    // will map to ASCII glyphs and colours.
    update_cell_targets(delta_time);
}

void AsciiMatrixAnimation::render(notcurses* /*nc*/) {
    if (!plane_ || !is_active_) {
        return;
    }

    ncplane_erase(plane_);
    ncplane_dim_yx(plane_, &plane_rows_, &plane_cols_);

    // Window resizes can happen between update() and render(), so make sure our
    // buffers still match the plane we're drawing on.
    ensure_dimensions_fit();

    if (plane_rows_ == 0u || plane_cols_ == 0u || glyphs_.empty()) {
        return;
    }

    if (show_border_) {
        draw_border();
    }

    draw_matrix();
}

bool AsciiMatrixAnimation::load_glyphs_from_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string contents = buffer.str();
    contents.erase(std::remove(contents.begin(), contents.end(), '\n'), contents.end());
    contents.erase(std::remove(contents.begin(), contents.end(), '\r'), contents.end());

    std::vector<std::string> parsed = parse_glyphs(contents);
    if (parsed.empty()) {
        return false;
    }

    glyphs_ = std::move(parsed);
    return true;
}

void AsciiMatrixAnimation::ensure_dimensions_fit() {
    const int border_padding = show_border_ ? 2 : 0;

    int max_rows = static_cast<int>(plane_rows_);
    int max_cols = static_cast<int>(plane_cols_);

    int available_rows = std::max(0, max_rows - border_padding);
    int available_cols = std::max(0, max_cols - border_padding);

    if (available_rows <= 0 || available_cols <= 0) {
        matrix_rows_ = 0;
        matrix_cols_ = 0;
        allocate_buffers();
        return;
    }

    matrix_rows_ = std::clamp(configured_matrix_rows_, 1, available_rows);
    matrix_cols_ = std::clamp(configured_matrix_cols_, 1, available_cols);
    allocate_buffers();
}

void AsciiMatrixAnimation::draw_border() {
    if (!plane_) {
        return;
    }

    if (plane_rows_ < 2u || plane_cols_ < 2u) {
        return;
    }

    const unsigned int last_row = plane_rows_ - 1u;
    const unsigned int last_col = plane_cols_ - 1u;

    for (unsigned int x = 0; x < plane_cols_; ++x) {
        const char ch = (x == 0u || x == last_col) ? '+' : '-';
        char glyph[2] = {ch, '\0'};
        ncplane_putstr_yx(plane_, 0, x, glyph);
        ncplane_putstr_yx(plane_, last_row, x, glyph);
    }

    for (unsigned int y = 1; y < last_row; ++y) {
        ncplane_putstr_yx(plane_, y, 0, "|");
        ncplane_putstr_yx(plane_, y, last_col, "|");
    }
}

void AsciiMatrixAnimation::draw_matrix() {
    if (!plane_ || matrix_rows_ <= 0 || matrix_cols_ <= 0 || glyphs_.empty()) {
        return;
    }

    const std::size_t glyph_count = glyphs_.size();
    if (glyph_count == 0) {
        return;
    }

    const bool beat_active = latest_beat_strength_ >= beat_threshold_;
    const unsigned int y_offset = show_border_ ? 1u : 0u;
    const unsigned int x_offset = show_border_ ? 1u : 0u;

    for (int row = 0; row < matrix_rows_; ++row) {
        const int lane = resolve_lane_for_row(row);
        const LaneStyle lane_style = (lane >= 0 && lane < static_cast<int>(kLaneStyles.size()))
                                         ? kLaneStyles[static_cast<std::size_t>(lane)]
                                         : LaneStyle{1.0f, 200u, 200u, 200u};

        for (int col = 0; col < matrix_cols_; ++col) {
            const std::size_t cell_index = static_cast<std::size_t>(row) * static_cast<std::size_t>(matrix_cols_) +
                                           static_cast<std::size_t>(col);
            if (cell_index >= cell_values_.size()) {
                continue;
            }

            float value = std::clamp(cell_values_[cell_index], 0.0f, 1.0f);

            // The currently playing step receives a subtle lift so users can
            // follow the sequencer playhead.
            if (highlighted_step_ >= 0 && col == highlighted_step_) {
                value = std::min(1.0f, value + highlight_pulse_ * 0.6f);
            }
            if (latest_downbeat_ && col == 0) {
                value = std::max(value, 0.7f);
            }

            std::size_t glyph_index = 0;
            if (glyph_count > 1) {
                glyph_index = std::min<std::size_t>(glyph_count - 1,
                                                     static_cast<std::size_t>(std::round(value *
                                                                                       static_cast<float>(glyph_count - 1))));
            }
            const std::string& glyph = glyphs_[glyph_index];

            const float brightness = beat_active ? std::min(1.0f, value * beat_boost_) : value;
            const float colour_mix = 0.18f + 0.82f * brightness;
            const uint8_t r = static_cast<uint8_t>(std::round(std::clamp(colour_mix * lane_style.r, 0.0f, 255.0f)));
            const uint8_t g = static_cast<uint8_t>(std::round(std::clamp(colour_mix * lane_style.g, 0.0f, 255.0f)));
            const uint8_t b = static_cast<uint8_t>(std::round(std::clamp(colour_mix * lane_style.b, 0.0f, 255.0f)));

            ncplane_set_fg_rgb8(plane_, r, g, b);
            ncplane_putstr_yx(plane_, static_cast<int>(y_offset + static_cast<unsigned int>(row)),
                              static_cast<int>(x_offset + static_cast<unsigned int>(col)),
                              glyph.c_str());
        }
    }
}

// Clear all dynamic state so the animation can be rebuilt from scratch on the
// next update() call.
void AsciiMatrixAnimation::reset_internal_state() {
    cell_values_.clear();
    target_cells_.clear();
    lane_levels_.clear();
    latest_beat_strength_ = 0.0f;
    highlight_pulse_ = 0.0f;
    highlighted_step_ = -1;
    latest_downbeat_ = false;
    pattern_dirty_ = true;
}

// Ensure the backing buffers match the configured matrix size. We only rebuild
// the storage when the dimensions actually change to keep transitions smooth.
void AsciiMatrixAnimation::allocate_buffers() {
    const std::size_t desired_cells = (matrix_rows_ > 0 && matrix_cols_ > 0)
                                          ? static_cast<std::size_t>(matrix_rows_) * static_cast<std::size_t>(matrix_cols_)
                                          : 0u;

    bool resized = false;
    if (desired_cells == 0u) {
        if (!cell_values_.empty()) {
            cell_values_.clear();
            resized = true;
        }
        if (!target_cells_.empty()) {
            target_cells_.clear();
            resized = true;
        }
    } else {
        if (cell_values_.size() != desired_cells) {
            cell_values_.assign(desired_cells, 0.0f);
            resized = true;
        }
        if (target_cells_.size() != desired_cells) {
            target_cells_.assign(desired_cells, 0.0f);
            resized = true;
        }
    }

    const int lane_count = matrix_rows_ > 0 ? std::min(matrix_rows_, kMaxLaneCount) : 0;
    if (lane_count <= 0) {
        if (!lane_levels_.empty()) {
            lane_levels_.clear();
            resized = true;
        }
    } else if (lane_levels_.size() != static_cast<std::size_t>(lane_count)) {
        lane_levels_.assign(static_cast<std::size_t>(lane_count), 0.0f);
        resized = true;
    }

    if (resized) {
        highlight_pulse_ = 0.0f;
        pattern_dirty_ = true;
    }
}

// Generate a new sequencer pattern. Each "lane" is treated like an instrument
// with its own trigger probability so the ASCII matrix reads like a drum grid.
void AsciiMatrixAnimation::refresh_pattern() {
    if (target_cells_.empty() || matrix_rows_ <= 0 || matrix_cols_ <= 0) {
        pattern_dirty_ = false;
        return;
    }

    // Randomise the step pattern with lane-specific probabilities so the grid
    // reads like a programmed drum machine.
    std::uniform_real_distribution<float> random01(0.0f, 1.0f);

    for (int row = 0; row < matrix_rows_; ++row) {
        const int lane = resolve_lane_for_row(row);
        const float base_probability = (lane >= 0 && lane < static_cast<int>(kLaneStyles.size()))
                                           ? kLaneStyles[static_cast<std::size_t>(lane)].trigger_probability
                                           : 0.0f;

        for (int col = 0; col < matrix_cols_; ++col) {
            const std::size_t idx = static_cast<std::size_t>(row) * static_cast<std::size_t>(matrix_cols_) +
                                    static_cast<std::size_t>(col);
            float probability = base_probability;

            const bool strong_step = (col % 4) == 0;
            if (strong_step) {
                probability = std::min(1.0f, probability + 0.35f);
            }
            if (lane == 2 && (col % 2) == 0) {
                probability = std::min(1.0f, probability + 0.15f);
            }

            // Keep kick drums driving the downbeat to anchor the visual rhythm.
            if (lane == 0 && strong_step) {
                probability = 1.0f;
            }

            target_cells_[idx] = random01(rng_) < probability ? 1.0f : 0.0f;
        }
    }

    pattern_dirty_ = false;
}

// Translate audio energy in the different frequency bands into per-lane energy
// levels. We mix envelopes and instantaneous values so the visual reacts to
// both sustained pads and short transients.
void AsciiMatrixAnimation::update_lane_intensities(float delta_time, const AudioFeatures& features) {
    if (lane_levels_.empty()) {
        return;
    }

    const float total_energy = std::max(features.total_energy, kMinEnergyEpsilon);
    const float total_instant = std::max(features.total_energy_instantaneous, kMinEnergyEpsilon);

    std::array<float, kMaxLaneCount> lane_targets = {{
        std::clamp(features.bass_envelope / total_energy, 0.0f, 1.0f),
        std::clamp(features.mid_envelope / total_energy, 0.0f, 1.0f),
        std::clamp(features.treble_envelope / total_energy, 0.0f, 1.0f),
        std::clamp(features.total_energy_instantaneous / total_instant, 0.0f, 1.0f),
    }};

    // Blend in the instantaneous band energies so short transients still pop
    // even with heavy smoothing enabled in the DSP configuration.
    lane_targets[0] = std::max(lane_targets[0], std::clamp(features.bass_energy_instantaneous / total_instant, 0.0f, 1.0f));
    lane_targets[1] = std::max(lane_targets[1], std::clamp(features.mid_energy_instantaneous / total_instant, 0.0f, 1.0f));
    lane_targets[2] = std::max(lane_targets[2], std::clamp(features.treble_energy_instantaneous / total_instant, 0.0f, 1.0f));

    float flux_mean = 0.0f;
    if (!features.band_flux.empty()) {
        for (float flux : features.band_flux) {
            flux_mean += std::fabs(flux);
        }
        flux_mean /= static_cast<float>(features.band_flux.size());
        lane_targets[3] = std::max(lane_targets[3], std::clamp(flux_mean, 0.0f, 1.0f));
    }

    if (features.bass_beat) {
        lane_targets[0] = 1.0f;
    }
    if (features.mid_beat) {
        lane_targets[1] = std::max(lane_targets[1], 0.9f);
    }
    if (features.treble_beat) {
        lane_targets[2] = std::max(lane_targets[2], 0.85f);
    }

    const float smoothing = std::clamp(delta_time * kLaneResponseRate, 0.0f, 1.0f);
    for (std::size_t lane = 0; lane < lane_levels_.size(); ++lane) {
        const std::size_t style_index = std::min<std::size_t>(lane, lane_targets.size() - 1);
        const float target = lane_targets[style_index];
        if (smoothing >= 1.0f) {
            lane_levels_[lane] = target;
        } else {
            lane_levels_[lane] += (target - lane_levels_[lane]) * smoothing;
        }
    }
}

// Track the current playhead position using bar_phase and react to beat/flatness
// cues. A burst of spectral flatness requests a full matrix refresh.
void AsciiMatrixAnimation::update_step_highlight(float delta_time, const AudioFeatures& features) {
    if (matrix_cols_ <= 0) {
        highlighted_step_ = -1;
    } else {
        const float step_position = features.bar_phase * static_cast<float>(matrix_cols_);
        int computed_step = static_cast<int>(std::floor(step_position));
        if (computed_step < 0) {
            computed_step += matrix_cols_;
        }
        highlighted_step_ = computed_step % matrix_cols_;
    }

    const float decay = std::clamp(delta_time * kHighlightDecayRate, 0.0f, 1.0f);
    highlight_pulse_ = std::max(0.0f, highlight_pulse_ - decay);
    if (features.beat_detected || features.beat_strength >= beat_threshold_ ||
        features.bass_beat || features.mid_beat || features.treble_beat) {
        highlight_pulse_ = 1.0f;
    }

    // Noisy passages cause the machine to re-roll the pattern, creating a
    // glitch-inspired full matrix refresh.
    if (features.spectral_flatness > kNoiseRefreshThreshold && features.total_energy > 0.15f) {
        pattern_dirty_ = true;
    }

    if (features.chroma_available) {
        const float harmonic_energy =
            std::accumulate(features.chroma.begin(), features.chroma.end(), 0.0f);
        if (harmonic_energy > 0.0f) {
            highlight_pulse_ = std::min(1.0f, highlight_pulse_ + harmonic_energy * 0.05f);
        }
    }
}

// Blend the lane intensities, the programmed pattern, and the beat highlights
// into a smooth 0..1 value per cell which render() later maps to glyphs.
void AsciiMatrixAnimation::update_cell_targets(float delta_time) {
    if (cell_values_.empty() || target_cells_.empty() || matrix_rows_ <= 0 || matrix_cols_ <= 0) {
        return;
    }

    const float smoothing = std::clamp(delta_time * kCellResponseRate, 0.0f, 1.0f);
    const bool beat_active = latest_beat_strength_ >= beat_threshold_;

    for (int row = 0; row < matrix_rows_; ++row) {
        const int lane = resolve_lane_for_row(row);
        const float lane_energy = (lane >= 0 && lane < static_cast<int>(lane_levels_.size()))
                                      ? lane_levels_[static_cast<std::size_t>(lane)]
                                      : 0.0f;

        for (int col = 0; col < matrix_cols_; ++col) {
            const std::size_t idx = static_cast<std::size_t>(row) * static_cast<std::size_t>(matrix_cols_) +
                                    static_cast<std::size_t>(col);
            if (idx >= target_cells_.size() || idx >= cell_values_.size()) {
                continue;
            }

            float target = lane_energy * target_cells_[idx];
            target = std::max(target, target_cells_[idx] * 0.15f);

            if (highlighted_step_ >= 0 && col == highlighted_step_) {
                target = std::max(target, lane_energy);
                target += highlight_pulse_ * 0.4f;
            }

            if (latest_downbeat_ && col == 0) {
                target = std::max(target, 0.8f);
            }

            if (beat_active) {
                target = std::min(1.0f, target * beat_boost_);
            }

            target = std::clamp(target, 0.0f, 1.0f);

            if (smoothing >= 1.0f) {
                cell_values_[idx] = target;
            } else {
                cell_values_[idx] += (target - cell_values_[idx]) * smoothing;
            }
        }
    }
}

// Map a matrix row to a logical drum lane. Rows are grouped so tall matrices
// simply repeat the lane palette.
int AsciiMatrixAnimation::resolve_lane_for_row(int row) const {
    if (matrix_rows_ <= 0) {
        return -1;
    }

    const int lane_count = std::min(matrix_rows_, kMaxLaneCount);
    if (lane_count <= 0) {
        return -1;
    }

    const float ratio = static_cast<float>(std::clamp(row, 0, matrix_rows_ - 1)) /
                        static_cast<float>(std::max(matrix_rows_, 1));
    int lane = static_cast<int>(std::floor(ratio * lane_count));
    lane = std::clamp(lane, 0, lane_count - 1);
    return lane;
}

void AsciiMatrixAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    bind_standard_frame_updates(this, config, bus);
}

} // namespace animations
} // namespace when
