#include "ConfigLoader.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <toml.hpp>

namespace {
float get_float(const toml::table& table, std::string_view key, float fallback) {
    if (const auto value = table[key].value<double>()) {
        return static_cast<float>(*value);
    }
    if (const auto value_int = table[key].value<std::int64_t>()) {
        return static_cast<float>(*value_int);
    }
    return fallback;
}

int get_int(const toml::table& table, std::string_view key, int fallback) {
    if (const auto value = table[key].value<std::int64_t>()) {
        return static_cast<int>(*value);
    }
    return fallback;
}

uint32_t get_color(const toml::table& table, std::string_view key, uint32_t fallback) {
    if (const auto value_unsigned = table[key].value<std::uint64_t>()) {
        return static_cast<uint32_t>(*value_unsigned);
    }
    if (const auto value_signed = table[key].value<std::int64_t>()) {
        return static_cast<uint32_t>(*value_signed);
    }
    if (const auto value_string = table[key].value<std::string>()) {
        try {
            size_t processed = 0;
            const auto raw = std::stoul(*value_string, &processed, 0);
            if (processed == value_string->size()) {
                return static_cast<uint32_t>(raw);
            }
        } catch (const std::exception&) {
        }
    }
    return fallback;
}

void populate_character_set(const toml::table& table, RainConfig& config) {
    if (const auto* array = table["characterSet"].as_array()) {
        std::vector<char32_t> characters;
        characters.reserve(array->size());
        for (const auto& node : *array) {
            if (const auto value = node.value<std::string>()) {
                if (!value->empty()) {
                    const char32_t ch = static_cast<char32_t>(static_cast<unsigned char>((*value)[0]));
                    characters.push_back(ch);
                }
            }
            if (const auto value_int = node.value<std::int64_t>()) {
                characters.push_back(static_cast<char32_t>(*value_int));
            }
        }
        if (!characters.empty()) {
            config.characterSet = std::move(characters);
        }
    }
}

void load_rain_settings(const toml::table& table, RainConfig& config, const std::filesystem::path& root_path) {
    config.slantAngle = get_float(table, "slantAngle", config.slantAngle);
    config.duration = get_float(table, "duration", config.duration);
    config.minSpeed = get_float(table, "minSpeed", config.minSpeed);
    config.maxSpeed = get_float(table, "maxSpeed", config.maxSpeed);
    config.minLength = get_int(table, "minLength", config.minLength);
    config.maxLength = get_int(table, "maxLength", config.maxLength);
    config.density = get_float(table, "density", config.density);

    if (const auto character_file = table["characterSetFile"].value<std::string>()) {
        std::filesystem::path character_path{*character_file};
        if (character_path.is_relative()) {
            character_path = root_path.parent_path() / character_path;
        }
        config.characterSetFile = character_path.string();
    }

    config.leadCharColor = get_color(table, "leadCharColor", config.leadCharColor);
    config.tailColor = get_color(table, "tailColor", config.tailColor);

    populate_character_set(table, config);

    // Alternate naming for integrated effect configuration.
    config.duration = get_float(table, "rain_duration", config.duration);
}

std::u32string utf8_to_u32(const std::string& input) {
    std::u32string result;
    result.reserve(input.size());
    for (unsigned char ch : input) {
        result.push_back(static_cast<char32_t>(ch));
    }
    return result;
}

} // namespace

SceneConfig load_scene_config_from_file(const std::filesystem::path& path) {
    SceneConfig sceneConfig{};
    sceneConfig.rainAndConverge.rainConfig = sceneConfig.rain;

    std::error_code exists_error;
    if (!std::filesystem::exists(path, exists_error)) {
        std::cerr << "Configuration file '" << path.string()
                  << "' not found. Falling back to built-in defaults.\n";
        return sceneConfig;
    }

    try {
        const toml::table table = toml::parse_file(path.string());

        if (const auto* scene_table = table["scene"].as_table()) {
            if (const auto animation_value = (*scene_table)["animation"].value<std::string>()) {
                if (*animation_value == "rain_and_converge") {
                    sceneConfig.animation = AnimationType::RainAndConverge;
                } else {
                    sceneConfig.animation = AnimationType::Rain;
                }
            }
        }

        if (sceneConfig.animation == AnimationType::RainAndConverge) {
            if (const auto* rac_table = table["rain_and_converge"].as_table()) {
                load_rain_settings(*rac_table, sceneConfig.rainAndConverge.rainConfig, path);
                if (const auto title_value = (*rac_table)["title"].value<std::string>()) {
                    sceneConfig.rainAndConverge.title = utf8_to_u32(*title_value);
                }
                sceneConfig.rainAndConverge.convergenceDuration = get_float(*rac_table, "convergence_duration", sceneConfig.rainAndConverge.convergenceDuration);
                sceneConfig.rainAndConverge.convergenceRandomness = get_float(*rac_table, "convergence_randomness", sceneConfig.rainAndConverge.convergenceRandomness);
                const int row_hint = get_int(*rac_table, "title_row", static_cast<int>(sceneConfig.rainAndConverge.titleRow));
                if (row_hint > 0) {
                    sceneConfig.rainAndConverge.titleRow = static_cast<unsigned int>(row_hint);
                }
            }
        } else {
            const toml::node_view effect = table["effect"];
            if (const auto* effect_table = effect.as_table()) {
                if (const auto* rain_table = (*effect_table)["cyberrain"].as_table()) {
                    load_rain_settings(*rain_table, sceneConfig.rain, path);
                }
            }
        }
    } catch (const toml::parse_error& err) {
        std::cerr << "Failed to parse configuration file '" << path.string() << "': "
                  << err.description() << "\n";
        const auto& region = err.source();
        if (region.begin) {
            std::cerr << "  (line " << region.begin.line << ", column " << region.begin.column << ")\n";
        }
    } catch (const std::exception& ex) {
        std::cerr << "Unexpected error while reading configuration file '" << path.string() << "': "
                  << ex.what() << "\n";
    }

    return sceneConfig;
}
