#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct PleasureConfig {
    float slantAngle{0.0f};
    float duration{10.0f};
    float minSpeed{4.0f};
    float maxSpeed{10.0f};
    int minLength{5};
    int maxLength{25};
    float density{0.05f};
    std::string characterSetFile{};
    uint32_t leadCharColor{0xFFFFFFFF};
    uint32_t tailColor{0xFF00FF00};
    std::vector<char32_t> characterSet{};
};

struct PleasureAndConvergeConfig {
    PleasureConfig pleasureConfig{};
    std::u32string title{};
    float convergenceDuration{4.0f};
    float convergenceRandomness{0.2f};
    unsigned int titleRow{10};
};

enum class AnimationType {
    Pleasure,
    PleasureAndConverge,
};

struct SceneConfig {
    AnimationType animation{AnimationType::Pleasure};
    PleasureConfig pleasure{};
    PleasureAndConvergeConfig pleasureAndConverge{};
};

SceneConfig load_scene_config_from_file(const std::filesystem::path& path);