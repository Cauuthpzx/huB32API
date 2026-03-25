#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace veyon32api::api::v1::dto {

struct AuthRequest
{
    std::string method;       // "veyon-key" | "logon"
    std::string username;
    std::string password;
    std::string keyName;
    std::string keyData;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AuthRequest, method, username, password, keyName, keyData)

struct AuthResponse
{
    std::string token;       // JWT Bearer token
    std::string tokenType = "Bearer";
    int         expiresIn = 3600;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AuthResponse, token, tokenType, expiresIn)

struct LogoutRequest
{
    std::string jti;         // token identifier to revoke
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LogoutRequest, jti)

} // namespace veyon32api::api::v1::dto
