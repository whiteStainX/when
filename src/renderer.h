#pragma once

#include <vector>

#include <notcurses/notcurses.h>

#include "audio/audio_features.h"
#include "audio_engine.h"
#include "animations/animation.h"
#include "animations/animation_manager.h" // Include AnimationManager
#include "config.h" // Include AppConfig

namespace when {

void render_frame(notcurses* nc,
               float time_s,
               const AudioMetrics& metrics,
               const AudioFeatures& features,
               const std::vector<float>& bands,
               float beat_strength,
               bool file_stream,
               bool show_metrics,
               bool show_overlay_metrics);

void load_animations_from_config(notcurses* nc, const AppConfig& config);

} // namespace when

