#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include "hub32api/export.h"

namespace hub32api::utils {

/// @brief Returns current time as Unix epoch seconds.
HUB32API_EXPORT int64_t now_unix();

/// @brief Returns current time as Unix epoch milliseconds.
HUB32API_EXPORT int64_t now_unix_ms();

/// @brief Formats a time_point as ISO 8601 string (e.g., "2026-03-26T10:30:00Z").
HUB32API_EXPORT std::string format_iso8601(std::chrono::system_clock::time_point tp);

/// @brief Formats current time as ISO 8601 string.
HUB32API_EXPORT std::string format_iso8601_now();

} // namespace hub32api::utils
