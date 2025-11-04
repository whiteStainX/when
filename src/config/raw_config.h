#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace why::config::detail {

struct RawScalar {
    std::string value;
    int line = 0;
};

struct RawArray {
    std::vector<std::string> values;
    int line = 0;
};

struct RawConfig {
    std::unordered_map<std::string, RawScalar> scalars;
    std::unordered_map<std::string, RawArray> arrays;
    std::vector<std::unordered_map<std::string, RawScalar>> animation_configs;
};

RawConfig parse_raw_config(const std::string& path,
                           std::vector<std::string>& warnings,
                           bool& loaded_file);

std::string sanitize_string_value(const std::string& value);

} // namespace why::config::detail

