#include "value_parsers.h"

#include <algorithm>
#include <cctype>

namespace why::config::detail {

namespace {
std::string trim_and_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c);
    });
    auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c);
    }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}
} // namespace

bool parse_bool(const std::string& value, bool& out) {
    const std::string lower = trim_and_lower(value);
    if (lower == "true" || lower == "1" || lower == "yes") {
        out = true;
        return true;
    }
    if (lower == "false" || lower == "0" || lower == "no") {
        out = false;
        return true;
    }
    return false;
}

bool parse_int64(const std::string& value, std::int64_t& out) {
    try {
        size_t idx = 0;
        const long long parsed = std::stoll(value, &idx, 0);
        if (idx != value.size()) {
            return false;
        }
        out = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_uint64(const std::string& value, std::uint64_t& out) {
    try {
        size_t idx = 0;
        const unsigned long long parsed = std::stoull(value, &idx, 0);
        if (idx != value.size()) {
            return false;
        }
        out = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_double(const std::string& value, double& out) {
    try {
        size_t idx = 0;
        const double parsed = std::stod(value, &idx);
        if (idx != value.size()) {
            return false;
        }
        out = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_uint32(const std::string& value, std::uint32_t& out) {
    std::uint64_t parsed = 0;
    if (!parse_uint64(value, parsed)) {
        return false;
    }
    out = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parse_size(const std::string& value, std::size_t& out) {
    std::uint64_t parsed = 0;
    if (!parse_uint64(value, parsed)) {
        return false;
    }
    out = static_cast<std::size_t>(parsed);
    return true;
}

bool parse_float32(const std::string& value, float& out) {
    double parsed = 0.0;
    if (!parse_double(value, parsed)) {
        return false;
    }
    out = static_cast<float>(parsed);
    return true;
}

bool parse_int32(const std::string& value, int& out) {
    std::int64_t parsed = 0;
    if (!parse_int64(value, parsed)) {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

} // namespace why::config::detail

