#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace why::config::detail {

bool parse_bool(const std::string& value, bool& out);
bool parse_int64(const std::string& value, std::int64_t& out);
bool parse_uint64(const std::string& value, std::uint64_t& out);
bool parse_double(const std::string& value, double& out);
bool parse_uint32(const std::string& value, std::uint32_t& out);
bool parse_size(const std::string& value, std::size_t& out);
bool parse_float32(const std::string& value, float& out);
bool parse_int32(const std::string& value, int& out);

} // namespace why::config::detail

