#pragma once

#include <string>
#include "hub32api/auth/JwtToken.hpp"
#include "hub32api/core/Result.hpp"

namespace hub32api::auth::internal {

// -----------------------------------------------------------------------
// JwtValidator — validates JWT signature and claims.
// Uses jwt-cpp library internally.
// -----------------------------------------------------------------------
class JwtValidator
{
public:
    explicit JwtValidator(const std::string& secret);

    Result<JwtToken> validate(const std::string& rawToken) const;

private:
    std::string m_secret;
};

} // namespace hub32api::auth::internal
