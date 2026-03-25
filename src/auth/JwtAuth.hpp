#pragma once

#include <string>
#include <memory>
#include "hub32api/export.h"
#include "hub32api/auth/JwtToken.hpp"
#include "hub32api/auth/AuthContext.hpp"
#include "hub32api/config/ServerConfig.hpp"
#include "hub32api/core/Result.hpp"

namespace hub32api::auth {

// -----------------------------------------------------------------------
// JwtAuth — issues and validates JWT Bearer tokens.
// Used by AuthController (issue) and AuthMiddleware (validate).
// -----------------------------------------------------------------------
class HUB32API_EXPORT JwtAuth
{
public:
    explicit JwtAuth(const ServerConfig& cfg);
    ~JwtAuth();

    // Issue a new token for a validated user
    Result<std::string> issueToken(
        const std::string& subject,
        const std::string& role) const;

    // Validate an incoming Bearer token → AuthContext
    Result<AuthContext> authenticate(const std::string& bearerToken) const;

    // Revoke a token (logout)
    void revokeToken(const std::string& jti);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hub32api::auth
