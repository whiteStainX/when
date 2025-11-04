#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../config.h"
#include "raw_config.h"

namespace when::config::detail {

std::optional<AnimationConfig> parse_animation_config(
    const std::unordered_map<std::string, RawScalar>& raw_anim_config,
    std::vector<std::string>& warnings);

} // namespace when::config::detail

