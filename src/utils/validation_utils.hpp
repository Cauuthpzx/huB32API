#pragma once

#include <string>
#include <string_view>
#include "hub32api/export.h"

namespace hub32api::utils {

/// @brief Validates a username: 1-64 chars, alphanumeric + underscore + hyphen + dot.
HUB32API_EXPORT bool validate_username(std::string_view username);

/// @brief Validates a UUID-format ID string (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).
HUB32API_EXPORT bool validate_id(std::string_view id);

/// @brief Strips HTML tags and trims whitespace. Returns sanitized string.
HUB32API_EXPORT std::string sanitize_input(std::string_view input);

/// @brief Checks that a string length is within [min_len, max_len].
HUB32API_EXPORT bool check_length(std::string_view s, size_t min_len, size_t max_len);

} // namespace hub32api::utils
