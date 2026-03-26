#pragma once

#include <string>
#include <string_view>

namespace hub32api::utils {

/// @brief Validates a username: 1-64 chars, alphanumeric + underscore + hyphen + dot.
bool validate_username(std::string_view username);

/// @brief Validates a UUID-format ID string (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).
bool validate_id(std::string_view id);

/// @brief Strips HTML tags and trims whitespace. Returns sanitized string.
std::string sanitize_input(std::string_view input);

/// @brief Checks that a string length is within [minLen, maxLen].
bool check_length(std::string_view s, size_t minLen, size_t maxLen);

} // namespace hub32api::utils
