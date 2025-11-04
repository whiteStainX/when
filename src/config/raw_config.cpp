#include "raw_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>

namespace why::config::detail {
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

std::string strip_inline_comment(const std::string& value) {
    bool in_quotes = false;
    char quote_char = '\0';
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if ((c == '\"' || c == '\'') && (i == 0 || value[i - 1] != '\\')) {
            if (!in_quotes) {
                in_quotes = true;
                quote_char = c;
            } else if (quote_char == c) {
                in_quotes = false;
            }
        }
        if (c == '#' && !in_quotes) {
            return trim(value.substr(0, i));
        }
    }
    return trim(value);
}

std::vector<std::string> parse_array_values(const std::string& raw,
                                            int line,
                                            std::vector<std::string>& warnings) {
    std::vector<std::string> values;
    std::string current;
    bool in_quotes = false;
    char quote_char = '\0';
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char c = raw[i];
        if (in_quotes) {
            if (c == quote_char && (i == 0 || raw[i - 1] != '\\')) {
                in_quotes = false;
            } else {
                current.push_back(c);
            }
            continue;
        }
        if (c == '\"' || c == '\'') {
            in_quotes = true;
            quote_char = c;
            continue;
        }
        if (c == ',') {
            std::string value = trim(current);
            if (!value.empty()) {
                values.push_back(value);
            }
            current.clear();
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(c))) {
            current.push_back(c);
        }
    }
    std::string value = trim(current);
    if (!value.empty()) {
        values.push_back(value);
    }
    if (in_quotes) {
        std::ostringstream oss;
        oss << "Unterminated string in array on line " << line;
        warnings.push_back(oss.str());
    }
    for (std::string& v : values) {
        if (!v.empty() && v.front() == '\"' && v.back() == '\"' && v.size() >= 2) {
            v = v.substr(1, v.size() - 2);
        } else if (!v.empty() && v.front() == '\'' && v.back() == '\'' && v.size() >= 2) {
            v = v.substr(1, v.size() - 2);
        }
    }
    return values;
}

void parse_file(const std::string& path,
                RawConfig& out,
                std::vector<std::string>& warnings,
                bool& loaded_file) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return;
    }
    loaded_file = true;

    std::string line;
    std::string current_section;
    std::unordered_map<std::string, RawScalar>* current_animation_map = nullptr;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            if (trimmed.front() == '[' && trimmed.at(1) == '[' && trimmed.back() == ']' &&
                trimmed.at(trimmed.size() - 2) == ']') {
                std::string array_name = trim(trimmed.substr(2, trimmed.size() - 4));
                if (array_name == "animations") {
                    out.animation_configs.emplace_back();
                    current_animation_map = &out.animation_configs.back();
                } else {
                    current_animation_map = nullptr;
                }
                current_section.clear();
            } else {
                current_section = trim(trimmed.substr(1, trimmed.size() - 2));
                current_animation_map = nullptr;
            }
            continue;
        }

        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            std::ostringstream oss;
            oss << "Ignoring line " << line_number << ": missing '='";
            warnings.push_back(oss.str());
            continue;
        }
        std::string key = trim(trimmed.substr(0, eq));
        std::string value = strip_inline_comment(trimmed.substr(eq + 1));

        if (current_animation_map) {
            RawScalar scalar;
            scalar.value = value;
            scalar.line = line_number;
            (*current_animation_map)[key] = scalar;
        } else {
            std::string full_key = key;
            if (!current_section.empty()) {
                full_key = current_section + "." + key;
            }
            if (value.size() >= 2 && value.front() == '[' && value.back() == ']') {
                std::string inner = trim(value.substr(1, value.size() - 2));
                RawArray array;
                array.values = parse_array_values(inner, line_number, warnings);
                array.line = line_number;
                out.arrays[full_key] = array;
            } else {
                RawScalar scalar;
                scalar.value = value;
                scalar.line = line_number;
                out.scalars[full_key] = scalar;
            }
        }
    }
}

} // namespace

RawConfig parse_raw_config(const std::string& path,
                           std::vector<std::string>& warnings,
                           bool& loaded_file) {
    RawConfig raw;
    parse_file(path, raw, warnings, loaded_file);
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

} // namespace why::config::detail

