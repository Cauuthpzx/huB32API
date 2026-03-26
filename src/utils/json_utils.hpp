#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json_fwd.hpp>
#include "hub32api/export.h"

namespace hub32api::utils {

/// @brief Safely gets a string from JSON. Returns nullopt if key missing or wrong type.
HUB32API_EXPORT std::optional<std::string> safe_get_string(const nlohmann::json& j, const std::string& key);

/// @brief Safely gets an int from JSON. Returns nullopt if key missing or wrong type.
HUB32API_EXPORT std::optional<int> safe_get_int(const nlohmann::json& j, const std::string& key);

/// @brief Safely gets a bool from JSON. Returns nullopt if key missing or wrong type.
HUB32API_EXPORT std::optional<bool> safe_get_bool(const nlohmann::json& j, const std::string& key);

/// @brief Checks that all specified fields exist in the JSON object.
/// @return List of missing field names (empty if all present).
HUB32API_EXPORT std::vector<std::string> missing_fields(const nlohmann::json& j,
                                                          const std::vector<std::string>& required);

} // namespace hub32api::utils
