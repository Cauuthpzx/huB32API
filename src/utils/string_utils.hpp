#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include "hub32api/export.h"

namespace hub32api::utils {

/// @brief Trims leading and trailing whitespace.
HUB32API_EXPORT std::string trim(std::string_view s);

/// @brief Converts string to lowercase (ASCII only).
HUB32API_EXPORT std::string to_lower(std::string_view s);

/// @brief Splits a string by delimiter.
HUB32API_EXPORT std::vector<std::string> split(std::string_view s, char delimiter);

/// @brief Joins strings with a separator.
HUB32API_EXPORT std::string join(const std::vector<std::string>& parts, std::string_view separator);

/// @brief Checks if string starts with prefix.
HUB32API_EXPORT bool starts_with(std::string_view s, std::string_view prefix);

/// @brief Checks if string ends with suffix.
HUB32API_EXPORT bool ends_with(std::string_view s, std::string_view suffix);

/// @brief Converts raw bytes to lowercase hex string.
HUB32API_EXPORT std::string bytes_to_hex(const unsigned char* data, size_t len);

/// @brief Converts hex string to raw bytes. Returns empty on invalid input.
HUB32API_EXPORT std::vector<unsigned char> hex_to_bytes(std::string_view hex);

} // namespace hub32api::utils
