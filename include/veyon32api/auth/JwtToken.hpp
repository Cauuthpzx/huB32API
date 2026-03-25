#pragma once

#include <string>
#include <chrono>
#include "veyon32api/export.h"

namespace veyon32api {

// -----------------------------------------------------------------------
// JwtToken — parsed JWT payload for per-request authentication.
// -----------------------------------------------------------------------
struct VEYON32API_EXPORT JwtToken
{
    std::string subject;       // typically: username
    std::string issuer;        // "veyon32api"
    std::string role;          // "admin" | "teacher" | "readonly"
    std::chrono::system_clock::time_point issuedAt;
    std::chrono::system_clock::time_point expiresAt;

    bool is_expired() const noexcept;
    bool has_role(const std::string& required) const noexcept;
};

} // namespace veyon32api
