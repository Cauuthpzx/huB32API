#include "json_utils.hpp"

#include <nlohmann/json.hpp>

namespace hub32api::utils {

std::optional<std::string> safe_get_string(const nlohmann::json& j, const std::string& key)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return std::nullopt;
    return j[key].get<std::string>();
}

std::optional<int> safe_get_int(const nlohmann::json& j, const std::string& key)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_number_integer()) return std::nullopt;
    return j[key].get<int>();
}

std::optional<bool> safe_get_bool(const nlohmann::json& j, const std::string& key)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_boolean()) return std::nullopt;
    return j[key].get<bool>();
}

std::vector<std::string> missing_fields(const nlohmann::json& j,
                                         const std::vector<std::string>& required)
{
    std::vector<std::string> missing;
    if (!j.is_object()) return required;
    for (const auto& field : required) {
        if (!j.contains(field)) missing.push_back(field);
    }
    return missing;
}

} // namespace hub32api::utils
