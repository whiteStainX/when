#include "ascii_matrix_animation.h"
#include "animation_event_utils.h"
#include "glyph_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace why {
namespace animations {

namespace {
constexpr const char* kDefaultGlyphFilePath = "assets/ascii_matrix.txt";
constexpr const char* kDefaultGlyphs = R"( .:-=+*#%@)";
constexpr int kDefaultMatrixRows = 16;
constexpr int kDefaultMatrixCols = 32;
constexpr float kDefaultBeatBoost = 1.5f;
constexpr float kDefaultBeatThreshold = 0.6f;

} // namespace

AsciiMatrixAnimation::AsciiMatrixAnimation()
    : glyphs_(parse_glyphs(kDefaultGlyphs)),
      glyphs_file_path_(kDefaultGlyphFilePath) {}

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

    int desired_y = plane_origin_y_;
    int desired_x = plane_origin_x_;
    int desired_rows = static_cast<int>(matrix_rows_ + (show_border_ ? 2 : 0));
    int desired_cols = static_cast<int>(matrix_cols_ + (show_border_ ? 2 : 0));

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
            }
            if (anim_config.plane_x) {
                desired_x = *anim_config.plane_x;
            }
            if (anim_config.plane_rows) {
                desired_rows = std::max(*anim_config.plane_rows, show_border_ ? 3 : 1);
            } else {
                desired_rows = matrix_rows_ + (show_border_ ? 2 : 0);
            }
            if (anim_config.plane_cols) {
                desired_cols = std::max(*anim_config.plane_cols, show_border_ ? 3 : 1);
            } else {
                desired_cols = matrix_cols_ + (show_border_ ? 2 : 0);
            }
            break;
        }
    }

    if (std_rows > 0) {
        plane_origin_y_ = std::clamp(desired_y, 0, static_cast<int>(std_rows) - 1);
    } else {
        plane_origin_y_ = 0;
    }

    if (std_cols > 0) {
        plane_origin_x_ = std::clamp(desired_x, 0, static_cast<int>(std_cols) - 1);
    } else {
        plane_origin_x_ = 0;
    }

    plane_rows_ = 0;
    plane_cols_ = 0;

    const unsigned int available_rows = (std_rows > static_cast<unsigned int>(plane_origin_y_))
                                            ? std_rows - static_cast<unsigned int>(plane_origin_y_)
                                            : 0u;
    const unsigned int available_cols = (std_cols > static_cast<unsigned int>(plane_origin_x_))
                                            ? std_cols - static_cast<unsigned int>(plane_origin_x_)
                                            : 0u;

    if (available_rows > 0u) {
        plane_rows_ = std::clamp(static_cast<unsigned int>(desired_rows), 1u, available_rows);
    }
    if (available_cols > 0u) {
        plane_cols_ = std::clamp(static_cast<unsigned int>(desired_cols), 1u, available_cols);
    }

    if (plane_rows_ == 0u) {
        plane_rows_ = std_rows;
        plane_origin_y_ = 0;
    }
    if (plane_cols_ == 0u) {
        plane_cols_ = std_cols;
        plane_origin_x_ = 0;
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
}

void AsciiMatrixAnimation::update(float /*delta_time*/,
                                  const AudioMetrics& /*metrics*/,
                                  const std::vector<float>& bands,
                                  float beat_strength) {
    if (!plane_ || !is_active_) {
        return;
    }

    latest_beat_strength_ = beat_strength;

    const std::size_t cell_count = static_cast<std::size_t>(matrix_rows_) * static_cast<std::size_t>(matrix_cols_);
    if (cell_values_.size() != cell_count) {
        cell_values_.assign(cell_count, 0.0f);
    }

    if (bands.empty()) {
        std::fill(cell_values_.begin(), cell_values_.end(), 0.0f);
        return;
    }

    float max_energy = 0.0f;
    for (float energy : bands) {
        max_energy = std::max(max_energy, energy);
    }

    const bool beat_active = beat_strength >= beat_threshold_;

    for (std::size_t idx = 0; idx < cell_count; ++idx) {
        const float normalized_position = static_cast<float>(idx) / static_cast<float>(cell_count);
        std::size_t band_index = static_cast<std::size_t>(std::floor(normalized_position * static_cast<float>(bands.size())));
        if (band_index >= bands.size()) {
            band_index = bands.size() - 1;
        }

        float value = bands[band_index];
        if (max_energy > 0.0f) {
            value /= max_energy;
        }

        if (beat_active) {
            value = std::min(1.0f, value * beat_boost_);
        }

        cell_values_[idx] = value;
    }
}

void AsciiMatrixAnimation::render(notcurses* /*nc*/) {
    if (!plane_ || !is_active_) {
        return;
    }

    ncplane_erase(plane_);
    ncplane_dim_yx(plane_, &plane_rows_, &plane_cols_);

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
        cell_values_.clear();
        return;
    }

    matrix_rows_ = std::clamp(configured_matrix_rows_, 1, available_rows);
    matrix_cols_ = std::clamp(configured_matrix_cols_, 1, available_cols);

    const std::size_t desired_cells = static_cast<std::size_t>(matrix_rows_) * static_cast<std::size_t>(matrix_cols_);
    if (cell_values_.size() != desired_cells) {
        cell_values_.assign(desired_cells, 0.0f);
    }
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
        for (int col = 0; col < matrix_cols_; ++col) {
            const std::size_t cell_index = static_cast<std::size_t>(row) * static_cast<std::size_t>(matrix_cols_) + static_cast<std::size_t>(col);
            if (cell_index >= cell_values_.size()) {
                continue;
            }

            float value = std::clamp(cell_values_[cell_index], 0.0f, 1.0f);
            std::size_t glyph_index = 0;
            if (glyph_count > 1) {
                glyph_index = std::min<std::size_t>(glyph_count - 1,
                                                     static_cast<std::size_t>(std::round(value * static_cast<float>(glyph_count - 1))));
            }
            const std::string& glyph = glyphs_[glyph_index];

            const float color_value = std::clamp(value, 0.0f, 1.0f);
            const float boosted_color = beat_active ? std::min(1.0f, color_value * beat_boost_) : color_value;
            const uint8_t intensity = static_cast<uint8_t>(std::round(boosted_color * 255.0f));

            if (beat_active) {
                const uint8_t r = intensity;
                const uint8_t g = static_cast<uint8_t>(std::min(255.0f, intensity * 0.6f));
                ncplane_set_fg_rgb8(plane_, r, g, 0u);
            } else {
                const uint8_t g = intensity;
                const uint8_t b = static_cast<uint8_t>(std::min(255.0f, intensity * 0.8f));
                ncplane_set_fg_rgb8(plane_, 0u, g, b);
            }

            ncplane_putstr_yx(plane_, static_cast<int>(y_offset + static_cast<unsigned int>(row)),
                              static_cast<int>(x_offset + static_cast<unsigned int>(col)),
                              glyph.c_str());
        }
    }
}

void AsciiMatrixAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    bind_standard_frame_updates(this, config, bus);
}

} // namespace animations
} // namespace why
