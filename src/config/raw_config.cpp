#include "raw_config.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string_view>
#include <utility>

#include <toml.hpp>

namespace when::config::detail {
namespace {

std::string ltrim(std::string_view sv) {
    std::size_t idx = 0;
    while (idx < sv.size() && std::isspace(static_cast<unsigned char>(sv[idx]))) {
        ++idx;
    }
    return std::string(sv.substr(idx));
}

std::string rtrim(std::string_view sv) {
    std::size_t idx = sv.size();
    while (idx > 0 && std::isspace(static_cast<unsigned char>(sv[idx - 1]))) {
        --idx;
    }
    return std::string(sv.substr(0, idx));
}

std::string trim(std::string_view sv) {
    return rtrim(ltrim(sv));
}

int node_line(const toml::node& node) {
    const toml::source_region& src = node.source();
    return static_cast<int>(src.begin.line);
}

std::string node_to_string(const toml::node& node) {
    if (auto value = node.value<std::string>()) {
        return *value;
    }
    if (auto value = node.value<bool>()) {
        return *value ? "true" : "false";
    }
    if (auto value = node.value<std::int64_t>()) {
        return std::to_string(*value);
    }
    if (auto value = node.value<double>()) {
        std::ostringstream oss;
        oss << *value;
        return oss.str();
    }
    if (auto value = node.value<toml::date>()) {
        std::ostringstream oss;
        oss << *value;
        return oss.str();
    }
    if (auto value = node.value<toml::time>()) {
        std::ostringstream oss;
        oss << *value;
        return oss.str();
    }
    if (auto value = node.value<toml::date_time>()) {
        std::ostringstream oss;
        oss << *value;
        return oss.str();
    }
    std::ostringstream oss;
    oss << toml::default_formatter{node};
    return oss.str();
}

void append_animation_configs(const toml::array& array,
                              RawConfig& out,
                              std::vector<std::string>& warnings) {
    for (const toml::node& element : array) {
        if (const auto* table = element.as_table()) {
            std::unordered_map<std::string, RawScalar> anim_map;
            for (const auto& [key, value] : *table) {
                RawScalar scalar;
                scalar.value = node_to_string(value);
                scalar.line = node_line(value);
                anim_map.emplace(key.str(), std::move(scalar));
            }
            out.animation_configs.push_back(std::move(anim_map));
        } else {
            std::ostringstream oss;
            oss << "Invalid animation entry type at line " << node_line(element);
            warnings.push_back(oss.str());
        }
    }
}

void append_array_values(const std::string& key,
                         const toml::array& array,
                         RawConfig& out,
                         std::vector<std::string>& warnings) {
    RawArray raw_array;
    raw_array.line = array.source().begin.line;
    for (const toml::node& value : array) {
        if (value.is_array() || value.is_table()) {
            std::ostringstream oss;
            oss << "Unsupported nested value in array '" << key << "' at line " << node_line(value);
            warnings.push_back(oss.str());
            continue;
        }
        raw_array.values.push_back(node_to_string(value));
    }
    out.arrays[key] = std::move(raw_array);
}

void flatten_table(const toml::table& table,
                   const std::string& prefix,
                   RawConfig& out,
                   std::vector<std::string>& warnings) {
    for (const auto& [key, value] : table) {
        const std::string key_str = std::string{key.str()};
        const std::string full_key = prefix.empty() ? key_str : prefix + '.' + key_str;

        if (const auto* child_table = value.as_table()) {
            flatten_table(*child_table, full_key, out, warnings);
            continue;
        }

        if (const auto* array = value.as_array()) {
            if (full_key == "animations") {
                append_animation_configs(*array, out, warnings);
            } else {
                append_array_values(full_key, *array, out, warnings);
            }
            continue;
        }

        RawScalar scalar;
        scalar.value = node_to_string(value);
        scalar.line = node_line(value);
        out.scalars[full_key] = std::move(scalar);
    }
}

} // namespace

RawConfig parse_raw_config(const std::string& path,
                           std::vector<std::string>& warnings,
                           bool& loaded_file) {
    RawConfig raw;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return raw;
    }

    try {
        toml::table parsed = toml::parse_file(path);
        loaded_file = true;
        flatten_table(parsed, std::string{}, raw, warnings);
    } catch (const toml::parse_error& err) {
        std::ostringstream oss;
        oss << "Failed to parse '" << path << "': " << err.description()
            << " (line " << err.source().begin.line
            << ", column " << err.source().begin.column << ")";
        warnings.push_back(oss.str());
    }

    return raw;
}

std::string sanitize_string_value(const std::string& value) {
    std::string trimmed = trim(value);
    if (trimmed.length() >= 2) {
        const char first = trimmed.front();
        const char last = trimmed.back();
        if ((first == '\"' && last == '\"') || (first == '\'' && last == '\'')) {
            trimmed = trimmed.substr(1, trimmed.length() - 2);
        }
    }
    return trimmed;
}

} // namespace when::config::detail

