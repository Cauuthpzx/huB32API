#pragma once

#include <string>
#include <nlohmann/json.hpp>

#include "hub32api/core/Constants.hpp"

namespace hub32api::api::v1::dto {

struct AuthRequest
{
    std::string method;       // "hub32-key" | "logon"
    std::string username;
    std::string password;
    std::string keyName;
    std::string keyData;
};

inline void from_json(const nlohmann::json& j, AuthRequest& r)
{
    j.at("method").get_to(r.method);
    if (j.contains("username")) j.at("username").get_to(r.username);
    if (j.contains("password")) j.at("password").get_to(r.password);
    if (j.contains("keyName"))  j.at("keyName").get_to(r.keyName);
    if (j.contains("keyData"))  j.at("keyData").get_to(r.keyData);
}
inline void to_json(nlohmann::json& j, const AuthRequest& r)
{
    j = nlohmann::json{{"method", r.method}, {"username", r.username},
                       {"password", r.password}, {"keyName", r.keyName},
                       {"keyData", r.keyData}};
}

struct AuthResponse
{
    std::string token;       // JWT Bearer token
    std::string tokenType = "Bearer";
    int         expiresIn = 3600;  // kDefaultTokenExpirySec — literal required by NLOHMANN macro
};

static_assert(hub32api::kDefaultTokenExpirySec == 3600, "Update AuthResponse::expiresIn default if this changes");
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AuthResponse, token, tokenType, expiresIn)

struct LogoutRequest
{
    std::string jti;         // token identifier to revoke
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LogoutRequest, jti)

} // namespace hub32api::api::v1::dto
