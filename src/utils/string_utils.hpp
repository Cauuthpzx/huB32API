#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace hub32api::utils {

/// @brief Trims leading and trailing whitespace.
std::string trim(std::string_view s);

/// @brief Converts string to lowercase (ASCII only).
std::string to_lower(std::string_view s);

/// @brief Splits a string by delimiter.
std::vector<std::string> split(std::string_view s, char delimiter);

/// @brief Joins strings with a separator.
std::string join(const std::vector<std::string>& parts, std::string_view separator);

/// @brief Checks if string starts with prefix.
bool starts_with(std::string_view s, std::string_view prefix);

/// @brief Checks if string ends with suffix.
bool ends_with(std::string_view s, std::string_view suffix);

/// @brief Converts raw bytes to lowercase hex string.
std::string bytes_to_hex(const unsigned char* data, size_t len);

/// @brief Converts hex string to raw bytes. Returns empty on invalid input.
std::vector<unsigned char> hex_to_bytes(std::string_view hex);

} // namespace hub32api::utils
