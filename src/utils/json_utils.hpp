#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json_fwd.hpp>

namespace hub32api::utils {

/// @brief Safely gets a string from JSON. Returns nullopt if key missing or wrong type.
std::optional<std::string> safe_get_string(const nlohmann::json& j, const std::string& key);

/// @brief Safely gets an int from JSON. Returns nullopt if key missing or wrong type.
std::optional<int> safe_get_int(const nlohmann::json& j, const std::string& key);

/// @brief Safely gets a bool from JSON. Returns nullopt if key missing or wrong type.
std::optional<bool> safe_get_bool(const nlohmann::json& j, const std::string& key);

/// @brief Checks that all specified fields exist in the JSON object.
/// @return List of missing field names (empty if all present).
std::vector<std::string> missing_fields(const nlohmann::json& j,
                                         const std::vector<std::string>& required);

} // namespace hub32api::utils
