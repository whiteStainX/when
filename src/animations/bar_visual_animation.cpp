#include "bar_visual_animation.h"
#include "animation_event_utils.h"
#include "glyph_utils.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace why {
namespace animations {

namespace {
constexpr const char* kDefaultGlyphFilePath = "assets/bar.txt";
constexpr const char* kDefaultGlyphs = R"( .'`^",:;Il!i><~+_-?][}{1)(|\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$)";

} // namespace

BarVisualAnimation::BarVisualAnimation()
    : glyphs_(parse_glyphs(kDefaultGlyphs)),
      glyphs_file_path_(kDefaultGlyphFilePath) {}

BarVisualAnimation::~BarVisualAnimation() {
    if (plane_) {
        ncplane_destroy(plane_);
    }
}

void BarVisualAnimation::init(notcurses* nc, const AppConfig& config) {
    ncplane* stdplane = notcurses_stdplane(nc);
    unsigned int std_rows = 0;
    unsigned int std_cols = 0;
    ncplane_dim_yx(stdplane, &std_rows, &std_cols);

    int desired_y = plane_origin_y_;
    int desired_x = plane_origin_x_;
    int desired_rows = static_cast<int>(std_rows);
    int desired_cols = static_cast<int>(std_cols);
    bool has_custom_rows = false;
    bool has_custom_cols = false;

    for (const auto& anim_config : config.animations) {
        if (anim_config.type == "BarVisual") { // Assuming type is used to identify
            z_index_ = anim_config.z_index;
            is_active_ = anim_config.initially_active;
            if (!anim_config.text_file_path.empty()) {
                glyphs_file_path_ = anim_config.text_file_path;
            }
            if (anim_config.plane_y) {
                desired_y = *anim_config.plane_y;
            }
            if (anim_config.plane_x) {
                desired_x = *anim_config.plane_x;
            }
            if (anim_config.plane_rows) {
                if (*anim_config.plane_rows > 0) {
                    desired_rows = *anim_config.plane_rows;
                    has_custom_rows = true;
                } else {
                    has_custom_rows = false;
                }
            }
            if (anim_config.plane_cols) {
                if (*anim_config.plane_cols > 0) {
                    desired_cols = *anim_config.plane_cols;
                    has_custom_cols = true;
                } else {
                    has_custom_cols = false;
                }
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

    const unsigned int available_rows = (std_rows > static_cast<unsigned int>(plane_origin_y_))
                                            ? std_rows - static_cast<unsigned int>(plane_origin_y_)
                                            : 0u;
    const unsigned int available_cols = (std_cols > static_cast<unsigned int>(plane_origin_x_))
                                            ? std_cols - static_cast<unsigned int>(plane_origin_x_)
                                            : 0u;

    if (has_custom_rows && available_rows > 0u) {
        plane_rows_ = static_cast<unsigned int>(std::clamp(desired_rows, 1, static_cast<int>(available_rows)));
    } else {
        plane_rows_ = available_rows;
    }

    if (has_custom_cols && available_cols > 0u) {
        plane_cols_ = static_cast<unsigned int>(std::clamp(desired_cols, 1, static_cast<int>(available_cols)));
    } else {
        plane_cols_ = available_cols;
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

    ncplane_options p_opts{};
    p_opts.rows = plane_rows_;
    p_opts.cols = plane_cols_;
    p_opts.y = plane_origin_y_;
    p_opts.x = plane_origin_x_;
    plane_ = ncplane_create(stdplane, &p_opts);

    if (plane_) {
        ncplane_dim_yx(plane_, &plane_rows_, &plane_cols_);
    }
}

void BarVisualAnimation::activate() {
    is_active_ = true;
    if (plane_) {
        // Optionally reset or re-draw something on activation
    }
}

void BarVisualAnimation::deactivate() {
    is_active_ = false;
    if (plane_) {
        ncplane_erase(plane_); // Clear the plane when deactivated
    }
}

void BarVisualAnimation::update(float delta_time,
                                const AudioMetrics& metrics,
                                const std::vector<float>& bands,
                                float beat_strength) {
    if (!plane_ || !is_active_) return;

    // Copy bands data for rendering
    current_bands_ = bands;

    // Normalize band energies for visualization
    float max_energy = 0.0f;
    for (float energy : current_bands_) {
        max_energy = std::max(max_energy, energy);
    }

    if (max_energy > 0.0f) {
        for (float& energy : current_bands_) {
            energy /= max_energy;
        }
    }

    // Example: Adjust bar height based on beat strength (simple effect)
    // For a more complex visual, you'd process bands more extensively here.
    if (beat_strength > 0.5f) {
        // Maybe amplify bands temporarily or change color in render
    }
}

void BarVisualAnimation::render(notcurses* nc) {
    if (!plane_ || !is_active_) return;

    ncplane_erase(plane_);

    unsigned int current_rows = 0;
    unsigned int current_cols = 0;
    ncplane_dim_yx(plane_, &current_rows, &current_cols);
    plane_rows_ = current_rows;
    plane_cols_ = current_cols;

    if (plane_rows_ == 0u || plane_cols_ == 0u || glyphs_.empty()) {
        return;
    }

    if (current_bands_.empty()) {
        ncplane_set_fg_rgb8(plane_, 255, 0, 0); // Red for no bands
        ncplane_putstr_yx(plane_, plane_rows_ / 2, plane_cols_ / 2 - 5, "No Audio");
        return;
    }

    const unsigned int num_bands = current_bands_.size();
    if (num_bands == 0) return;

    const unsigned int bar_width = std::max(1u, plane_cols_ / num_bands);
    const unsigned int max_bar_height = plane_rows_ - 1; // Leave some space for metrics

    for (unsigned int i = 0; i < num_bands; ++i) {
        const float band_energy = current_bands_[i];
        // Simple scaling for visualization
        const unsigned int bar_height = static_cast<unsigned int>(std::round(band_energy * max_bar_height));

        // Determine glyph based on energy/height
        const std::size_t glyph_count = glyphs_.size();
        const std::size_t glyph_idx = (glyph_count > 1)
                                          ? std::min<std::size_t>(
                                                glyph_count - 1,
                                                static_cast<std::size_t>(std::round(
                                                    band_energy * static_cast<float>(glyph_count - 1))))
                                          : 0;
        const std::string& glyph = glyphs_[glyph_idx];

        // Draw each bar
        for (unsigned int h = 0; h < bar_height; ++h) {
            for (unsigned int w = 0; w < bar_width; ++w) {
                ncplane_set_fg_rgb8(plane_, 0, 255, 0); // Green bars
                ncplane_set_bg_rgb8(plane_, 0, 0, 0);   // Black background
                ncplane_putstr_yx(plane_, plane_rows_ - 1 - h, i * bar_width + w, glyph.c_str());
            }
        }
    }
}

bool BarVisualAnimation::load_glyphs_from_file(const std::string& path) {
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

void BarVisualAnimation::bind_events(const AnimationConfig& config, events::EventBus& bus) {
    bind_standard_frame_updates(this, config, bus);
}
} // namespace animations
} // namespace why
