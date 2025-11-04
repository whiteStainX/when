#pragma once

#include <vector>

#include <notcurses/notcurses.h>

#include "audio_engine.h"
#include "animations/animation.h"
#include "animations/animation_manager.h" // Include AnimationManager
#include "config.h" // Include AppConfig

namespace why {

void render_frame(notcurses* nc,
               float time_s,
               const AudioMetrics& metrics,
               const std::vector<float>& bands,
               float beat_strength,
               bool file_stream,
               bool show_metrics,
               bool show_overlay_metrics);

void load_animations_from_config(notcurses* nc, const AppConfig& config);

} // namespace why

