#pragma once

#include <string>
#include "hub32api/auth/JwtToken.hpp"
#include "hub32api/core/Result.hpp"

namespace hub32api::auth::internal {

// -----------------------------------------------------------------------
// JwtValidator — validates JWT signature and claims.
// Uses jwt-cpp library internally. Supports both HS256 and RS256 algorithms.
// -----------------------------------------------------------------------
class JwtValidator
{
public:
    explicit JwtValidator(const std::string& algorithm,
                          const std::string& secret,
                          const std::string& publicKey = {});

    Result<JwtToken> validate(const std::string& rawToken) const;

private:
    std::string m_algorithm;
    std::string m_secret;
    std::string m_publicKey;
};

} // namespace hub32api::auth::internal
