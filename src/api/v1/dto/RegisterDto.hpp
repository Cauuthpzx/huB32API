#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace hub32api::api::v1::dto {

struct RegisterRequest {
    std::string orgName;
    std::string username;       // login name (separate from email)
    std::string email;
    std::string password;
    std::string captchaId;      // signed token from GET /captcha
    std::string captchaAnswer;  // 6-digit string user typed
};

// Custom from_json: missing fields are left as empty strings (no throw).
inline void from_json(const nlohmann::json& j, RegisterRequest& r)
{
    if (j.contains("orgName")       && j["orgName"].is_string())       r.orgName       = j["orgName"].get<std::string>();
    if (j.contains("username")      && j["username"].is_string())      r.username      = j["username"].get<std::string>();
    if (j.contains("email")         && j["email"].is_string())         r.email         = j["email"].get<std::string>();
    if (j.contains("password")      && j["password"].is_string())      r.password      = j["password"].get<std::string>();
    if (j.contains("captchaId")     && j["captchaId"].is_string())     r.captchaId     = j["captchaId"].get<std::string>();
    if (j.contains("captchaAnswer") && j["captchaAnswer"].is_string()) r.captchaAnswer = j["captchaAnswer"].get<std::string>();
}

struct RegisterResponse {
    std::string message;
    std::string debugToken;  // only populated when not in production mode
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RegisterResponse, message, debugToken)

} // namespace hub32api::api::v1::dto
