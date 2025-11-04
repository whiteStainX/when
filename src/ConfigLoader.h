#pragma once

#include "effects/RainAndConvergeEffect.h"
#include "effects/RainEffect.h"

#include <filesystem>

enum class AnimationType {
    Rain,
    RainAndConverge,
};

struct SceneConfig {
    AnimationType animation{AnimationType::Rain};
    RainConfig rain{};
    RainAndConvergeConfig rainAndConverge{};
};

SceneConfig load_scene_config_from_file(const std::filesystem::path& path);
